#include "common.h"
#include "config.h"
#include "ipc_utils.h"
#include "simtime.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/msg.h>

/* ======================================================================
 * Processo utente (sez. 5.5 e sez. 6).
 *
 * Ogni giornata l'utente decide se recarsi in mensa, compone il proprio
 * menu a partire dai piatti pubblicati in memoria condivisa dal
 * responsabile, sceglie in quale coda mettersi in base alla lunghezza
 * delle code, ritira i piatti, paga alla cassa e consuma il pasto a un
 * tavolo.
 *
 * Versione completa:
 *  - l'utente e' di tipo "con ticket" (TICKET_PERCENTAGE% dei casi) o
 *    "senza ticket": il primo ha diritto a uno sconto ma deve passare
 *    dalla fila per la lettura del ticket (NOF_TICKET_READERS lettori);
 *  - gli utenti sono raggruppati in gruppi da 1 a MAX_USERS_PER_GROUP
 *    membri, decisi dal responsabile alla creazione dei processi. I
 *    membri di un gruppo pranzano insieme: passano in cassa solo quando
 *    tutti si sono riforniti, tramite il capogruppo che raccoglie gli
 *    ordini e presenta un'unica richiesta di pagamento.
 * ====================================================================== */

/* ======================================================================
 * Flag di controllo
 * ====================================================================== */

static volatile sig_atomic_t giorno_finito      = 0; /* SIGUSR1 */
static volatile sig_atomic_t simulazione_finita = 0; /* SIGTERM */

static void handler_fine_giorno(int signo)      { (void)signo; giorno_finito = 1; }
static void handler_fine_simulazione(int signo) { (void)signo; simulazione_finita = 1; }

static int devo_fermarmi(void) { return giorno_finito || simulazione_finita; }

/* ======================================================================
 * Contesto del processo utente
 * ====================================================================== */

typedef struct {
    const Config  *cfg;
    int            semid;
    StatoStazioni *stato;
    Statistiche   *stats;
    int            mq[NUM_STAZIONI];   /* code delle 4 stazioni */
    int            mq_gruppo;
    int            ha_ticket;
} Ctx;

/* Menu scelto e piatti effettivamente ottenuti nella giornata. */
typedef struct {
    int vuole_primo, vuole_secondo, vuole_dolce, vuole_caffe;
    int ha_primo, ha_secondo, ha_dolce, ha_caffe;
    int primo_idx, secondo_idx, contorno_idx, dolce_idx, tipo_caffe;
    int primi_esauriti, secondi_esauriti;
} Ordine;

/* ======================================================================
 * Contabilita' degli utenti in coda: serve al responsabile per decidere
 * la terminazione per overload (sez. 5.6). Chi e' ancora in coda quando
 * la mensa chiude rinuncia al pasto e viene conteggiato a parte.
 * ====================================================================== */

static void coda_entra(const Ctx *c) {
    sem_wait_idx(c->semid, SEM_MUTEX_ATTESA);
    c->stato->utenti_in_attesa++;
    sem_signal_idx(c->semid, SEM_MUTEX_ATTESA);
}

static void coda_esci(const Ctx *c, int abbandonata) {
    sem_wait_idx(c->semid, SEM_MUTEX_ATTESA);
    c->stato->utenti_in_attesa--;
    if (abbandonata) c->stato->utenti_in_coda_fine_giornata++;
    sem_signal_idx(c->semid, SEM_MUTEX_ATTESA);
}

static double ms_trascorsi(const struct timespec *t0, const struct timespec *t1) {
    return (t1->tv_sec - t0->tv_sec) * 1000.0 + (t1->tv_nsec - t0->tv_nsec) / 1e6;
}

/* ======================================================================
 * Fila per la lettura del ticket (sez. 6): solo gli utenti con ticket la
 * affrontano, ed e' modellata come un insieme di NOF_TICKET_READERS
 * lettori (semaforo contatore) su cui si attende passivamente.
 * ====================================================================== */

static int leggi_ticket(const Ctx *c) {
    if (!c->ha_ticket) return 1;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    coda_entra(c);

    if (sem_wait_idx_intr(c->semid, SEM_POSTAZIONI_TICKET) == -1) {
        coda_esci(c, 1);
        return 0;   /* mensa chiusa mentre ero in fila: rinuncio */
    }

    sim_sleep_sec(c->cfg->n_nano_secs, sim_rand_intorno(c->cfg->avg_srvc_ticket, 20));
    sem_signal_idx(c->semid, SEM_POSTAZIONI_TICKET);

    coda_esci(c, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    stats_record_attesa_ticket(c->stats, c->semid, ms_trascorsi(&t0, &t1));
    return 1;
}

/* ======================================================================
 * Composizione del menu (sez. 5.5): il menu del giorno e' quello caricato
 * dal responsabile in memoria condivisa; l'utente ne estrae la propria
 * combinazione secondo le probabilita' di configurazione, garantendo
 * sempre almeno un primo o un secondo.
 * ====================================================================== */

static void componi_menu(const Ctx *c, Ordine *o) {
    memset(o, 0, sizeof(*o));
    o->primo_idx = o->secondo_idx = o->contorno_idx = o->dolce_idx = o->tipo_caffe = -1;

    int n_primi    = c->stato->primi.num_piatti_oggi;
    int n_secondi  = c->stato->secondi.num_piatti_oggi;
    int n_contorni = c->stato->secondi.num_contorni;
    int n_dolci    = c->stato->coffee.num_dolci;
    int n_caffe    = c->stato->coffee.num_caffe;

    o->vuole_primo   = (n_primi   > 0) && (sim_rand_range(0, 99) < c->cfg->prob_primo);
    o->vuole_secondo = (n_secondi > 0) && (sim_rand_range(0, 99) < c->cfg->prob_secondo_contorno);

    /* il menu deve includere almeno un piatto principale */
    if (!o->vuole_primo && !o->vuole_secondo) {
        if (n_primi > 0 && (n_secondi == 0 || sim_rand_range(0, 1) == 0)) o->vuole_primo = 1;
        else if (n_secondi > 0)                                          o->vuole_secondo = 1;
    }

    if (o->vuole_primo)   o->primo_idx   = sim_rand_range(0, n_primi - 1);
    if (o->vuole_secondo) o->secondo_idx = sim_rand_range(0, n_secondi - 1);
    if (o->vuole_secondo && n_contorni > 0) o->contorno_idx = sim_rand_range(0, n_contorni - 1);

    o->vuole_dolce = (n_dolci > 0) && (sim_rand_range(0, 99) < c->cfg->prob_dolce);
    o->vuole_caffe = (n_caffe > 0) && (sim_rand_range(0, 99) < c->cfg->prob_caffe);

    if (o->vuole_dolce) o->dolce_idx  = sim_rand_range(0, n_dolci - 1);
    /* almeno 4 tipi di caffe': normale, macchiato, decaffeinato, ginseng */
    if (o->vuole_caffe) o->tipo_caffe = sim_rand_range(0, n_caffe - 1);
}

/* ======================================================================
 * Visita a una stazione di distribuzione: invio della richiesta sulla
 * coda della stazione e attesa passiva della risposta dell'operatore.
 * ====================================================================== */

static int visita_stazione(const Ctx *c, TipoStazione staz, Ordine *o) {
    MsgRichiestaCibo req;
    memset(&req, 0, sizeof(req));
    req.mtype        = MSG_RICHIESTA;
    req.utente_pid   = getpid();
    req.contorno_idx = -1;
    req.dolce_idx    = -1;
    req.tipo_caffe   = -1;

    switch (staz) {
        case STAZIONE_PRIMI:
            req.tipo_piatto = TIPO_PRIMO;
            req.scelta_idx  = o->primo_idx;
            break;
        case STAZIONE_SECONDI:
            req.tipo_piatto  = TIPO_SECONDO;
            req.scelta_idx   = o->secondo_idx;
            req.contorno_idx = o->contorno_idx;
            break;
        default:
            req.tipo_piatto = TIPO_DOLCE;   /* visita alla stazione coffee */
            req.scelta_idx  = -1;
            req.dolce_idx   = o->vuole_dolce ? o->dolce_idx  : -1;
            req.tipo_caffe  = o->vuole_caffe ? o->tipo_caffe : -1;
            break;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    coda_entra(c);

    while (msgsnd(c->mq[staz], &req, sizeof(req) - sizeof(long), 0) == -1) {
        if (errno == EINTR && !devo_fermarmi()) continue;
        coda_esci(c, devo_fermarmi());
        return 0;
    }

    MsgRispostaCibo resp;
    for (;;) {
        if (msgrcv(c->mq[staz], &resp, sizeof(resp) - sizeof(long), (long)getpid(), 0) != -1)
            break;
        if (errno == EINTR && !devo_fermarmi()) continue;
        /* la mensa ha chiuso mentre ero in coda: rinuncio (sez. 5.5) */
        coda_esci(c, 1);
        return 0;
    }

    coda_esci(c, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    stats_record_attesa(c->stats, c->semid, staz, ms_trascorsi(&t0, &t1));

    if (staz == STAZIONE_COFFEE) {
        o->ha_dolce = o->vuole_dolce;
        o->ha_caffe = o->vuole_caffe;
        if (o->ha_dolce && resp.piatto_idx >= 0) o->dolce_idx = resp.piatto_idx;
        return 1;
    }

    if (resp.esito == ESITO_TIPO_ESAURITO) {
        if (staz == STAZIONE_PRIMI) o->primi_esauriti   = 1;
        else                        o->secondi_esauriti = 1;
        return 0;
    }

    /* il piatto puo' essere stato sostituito con un altro dello stesso
     * tipo perche' quello scelto era terminato (sez. 5.5) */
    if (staz == STAZIONE_PRIMI) {
        o->ha_primo  = 1;
        o->primo_idx = resp.piatto_idx;
    } else {
        o->ha_secondo   = 1;
        o->secondo_idx  = resp.piatto_idx;
        o->contorno_idx = resp.contorno_idx;
    }
    return 1;
}

/* ======================================================================
 * Giro delle stazioni di distribuzione: fra le stazioni ancora da
 * visitare l'utente sceglie ogni volta quella con la coda piu' corta
 * (sez. 5.5), rivalutando la situazione dopo ogni piatto ottenuto.
 * ====================================================================== */

static void giro_stazioni(const Ctx *c, Ordine *o) {
    TipoStazione da_visitare[2];
    int n = 0;
    if (o->vuole_primo)   da_visitare[n++] = STAZIONE_PRIMI;
    if (o->vuole_secondo) da_visitare[n++] = STAZIONE_SECONDI;

    while (n > 0 && !devo_fermarmi()) {
        int scelto = 0;
        long coda_min = msgq_num_messaggi(c->mq[da_visitare[0]]);
        for (int i = 1; i < n; i++) {
            long q = msgq_num_messaggi(c->mq[da_visitare[i]]);
            if (q < coda_min) { coda_min = q; scelto = i; }
        }

        visita_stazione(c, da_visitare[scelto], o);

        for (int i = scelto; i < n - 1; i++) da_visitare[i] = da_visitare[i + 1];
        n--;
    }

    /* se il tipo desiderato e' finito, ripiega sull'altro tipo di piatto
     * principale (sez. 5.5: "mangera' solo secondo o solo primo") */
    if (!devo_fermarmi() && !o->ha_primo && !o->ha_secondo) {
        if (o->primi_esauriti && !o->vuole_secondo && c->stato->secondi.num_piatti_oggi > 0) {
            o->vuole_secondo = 1;
            o->secondo_idx   = sim_rand_range(0, c->stato->secondi.num_piatti_oggi - 1);
            if (c->stato->secondi.num_contorni > 0)
                o->contorno_idx = sim_rand_range(0, c->stato->secondi.num_contorni - 1);
            visita_stazione(c, STAZIONE_SECONDI, o);
        } else if (o->secondi_esauriti && !o->vuole_primo && c->stato->primi.num_piatti_oggi > 0) {
            o->vuole_primo = 1;
            o->primo_idx   = sim_rand_range(0, c->stato->primi.num_piatti_oggi - 1);
            visita_stazione(c, STAZIONE_PRIMI, o);
        }
    }

    /* dolce e caffe' si prendono per ultimi, e solo se non si e' desistito */
    if (!devo_fermarmi() && (o->ha_primo || o->ha_secondo) &&
        (o->vuole_dolce || o->vuole_caffe)) {
        visita_stazione(c, STAZIONE_COFFEE, o);
    }
}

/* ======================================================================
 * Pagamento (sez. 5.5 / 5.4). Un utente singolo e' il capogruppo di un
 * gruppo di un solo membro: la logica e' quindi unica.
 * ====================================================================== */

static void ordine_in_messaggio(const Ordine *o, int ha_ticket, MsgOrdineGruppo *m) {
    m->partecipa    = (o->ha_primo || o->ha_secondo);
    m->ha_ticket    = ha_ticket;
    m->ha_primo     = o->ha_primo;
    m->primo_idx    = o->primo_idx;
    m->ha_secondo   = o->ha_secondo;
    m->secondo_idx  = o->secondo_idx;
    m->contorno_idx = o->contorno_idx;
    m->ha_dolce     = o->ha_dolce;
    m->dolce_idx    = o->dolce_idx;
    m->ha_caffe     = o->ha_caffe;
    m->tipo_caffe   = o->tipo_caffe;
}

static void aggiungi_a_richiesta(MsgRichiestaCassa *req, int i, pid_t pid, int ha_ticket,
                                 const MsgOrdineGruppo *m) {
    req->membri_pid[i]  = pid;
    req->ha_ticket[i]   = ha_ticket;
    req->ha_primo[i]    = m->ha_primo;
    req->primo_idx[i]   = m->primo_idx;
    req->ha_secondo[i]  = m->ha_secondo;
    req->secondo_idx[i] = m->secondo_idx;
    req->contorno_idx[i]= m->contorno_idx;
    req->ha_dolce[i]    = m->ha_dolce;
    req->dolce_idx[i]   = m->dolce_idx;
    req->ha_caffe[i]    = m->ha_caffe;
    req->tipo_caffe[i]  = m->tipo_caffe;
}

/* Attende la propria ricevuta dalla cassa. Ritorna 1 se il pagamento e'
 * andato a buon fine. Come per le altre code, alla chiusura della mensa
 * l'attesa viene abbandonata: cosi' nessun utente puo' restare bloccato
 * oltre la fine della giornata. */
static int attendi_ricevuta(const Ctx *c) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    coda_entra(c);

    MsgRispostaCassa resp;
    for (;;) {
        if (msgrcv(c->mq[STAZIONE_CASSA], &resp, sizeof(resp) - sizeof(long),
                   (long)getpid(), 0) != -1)
            break;
        if (errno == EINTR && !devo_fermarmi()) continue;
        coda_esci(c, 1);
        return 0;
    }

    coda_esci(c, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    stats_record_attesa(c->stats, c->semid, STAZIONE_CASSA, ms_trascorsi(&t0, &t1));
    return 1;
}

/* Membro non capogruppo: comunica il proprio ordine al capogruppo e, se
 * partecipa al pranzo, attende la ricevuta dalla cassa. */
static int paga_come_membro(const Ctx *c, const Ordine *o, pid_t capo_pid) {
    MsgOrdineGruppo m;
    memset(&m, 0, sizeof(m));
    m.mtype      = (long)capo_pid;
    m.membro_pid = getpid();
    ordine_in_messaggio(o, c->ha_ticket, &m);

    /* il messaggio viene inviato SEMPRE, anche quando non si partecipa:
     * cosi' il capogruppo sa esattamente quanti ordini attendere */
    while (msgsnd(c->mq_gruppo, &m, sizeof(m) - sizeof(long), 0) == -1) {
        if (errno == EINTR && !simulazione_finita) continue;
        return 0;
    }

    if (devo_fermarmi()) return 0;

    if (!m.partecipa) return 0;
    return attendi_ricevuta(c);
}

/* Capogruppo: raccoglie gli ordini degli altri membri, costruisce l'unica
 * richiesta di pagamento del gruppo e attende la propria ricevuta. */
static int paga_come_capogruppo(const Ctx *c, const Ordine *o, int num_membri) {
    MsgRichiestaCassa req;
    memset(&req, 0, sizeof(req));
    req.mtype           = MSG_RICHIESTA;
    req.richiedente_pid = getpid();

    MsgOrdineGruppo mio;
    memset(&mio, 0, sizeof(mio));
    ordine_in_messaggio(o, c->ha_ticket, &mio);

    int partecipanti = 0;
    int io_partecipo = mio.partecipa;
    if (io_partecipo)
        aggiungi_a_richiesta(&req, partecipanti++, getpid(), c->ha_ticket, &mio);

    /* ogni membro invia esattamente un ordine al giorno: la raccolta
     * termina sempre, anche se qualcuno ha rinunciato al pasto */
    for (int raccolti = 0; raccolti < num_membri - 1; raccolti++) {
        MsgOrdineGruppo m;
        while (msgrcv(c->mq_gruppo, &m, sizeof(m) - sizeof(long), (long)getpid(), 0) == -1) {
            if (errno == EINTR && !devo_fermarmi()) continue;
            return 0;   /* mensa chiusa: si abbandona la raccolta */
        }
        if (m.partecipa && partecipanti < MAX_GRUPPO)
            aggiungi_a_richiesta(&req, partecipanti++, m.membro_pid, m.ha_ticket, &m);
    }

    if (partecipanti == 0) return 0;   /* nessuno del gruppo pranza oggi */
    req.num_membri = partecipanti;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    coda_entra(c);

    while (msgsnd(c->mq[STAZIONE_CASSA], &req, sizeof(req) - sizeof(long), 0) == -1) {
        if (errno == EINTR && !devo_fermarmi()) continue;
        coda_esci(c, 1);
        return 0;
    }

    if (!io_partecipo) {
        /* ho pagato per il gruppo pur non pranzando: non attendo ricevuta */
        coda_esci(c, 0);
        return 0;
    }

    MsgRispostaCassa resp;
    for (;;) {
        if (msgrcv(c->mq[STAZIONE_CASSA], &resp, sizeof(resp) - sizeof(long),
                   (long)getpid(), 0) != -1)
            break;
        if (errno == EINTR && !devo_fermarmi()) continue;
        coda_esci(c, 1);
        return 0;
    }

    coda_esci(c, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    stats_record_attesa(c->stats, c->semid, STAZIONE_CASSA, ms_trascorsi(&t0, &t1));
    return 1;
}

/* ======================================================================
 * Consumo del pasto: serve un posto libero fra gli NOF_TABLE_SEATS
 * complessivi, altrimenti si attende che se ne liberi uno (sez. 5.5).
 * Il tempo di consumo e' proporzionale al numero di piatti acquistati.
 * ====================================================================== */

static void consuma_pasto(const Ctx *c, const Ordine *o) {
    int num_piatti = o->ha_primo + o->ha_secondo + o->ha_dolce + o->ha_caffe;
    if (num_piatti < 1) num_piatti = 1;

    coda_entra(c);
    if (sem_wait_idx_intr(c->semid, SEM_TAVOLI) == -1) {
        coda_esci(c, 1);
        return;
    }
    coda_esci(c, 0);

    sim_sleep_sec(c->cfg->n_nano_secs, c->cfg->avg_consumption_time * num_piatti);

    sem_signal_idx(c->semid, SEM_TAVOLI);
    stats_record_utente_ha_mangiato(c->stats, c->semid);
}

/* ======================================================================
 * Una giornata dell'utente
 * ====================================================================== */

static void giornata(const Ctx *c, int capogruppo, int num_membri, pid_t capo_pid) {
    Ordine o;
    memset(&o, 0, sizeof(o));
    o.primo_idx = o.secondo_idx = o.contorno_idx = o.dolce_idx = o.tipo_caffe = -1;

    /* sez. 5.5: ogni giorno l'utente decide se recarsi in mensa */
    int vado_in_mensa = !devo_fermarmi() &&
                        sim_rand_range(0, 99) < c->cfg->prob_vado_in_mensa;

    if (vado_in_mensa && leggi_ticket(c)) {
        componi_menu(c, &o);
        giro_stazioni(c, &o);
    }

    int ha_pagato = 0;
    if (capogruppo)
        ha_pagato = paga_come_capogruppo(c, &o, num_membri);
    else
        ha_pagato = paga_come_membro(c, &o, capo_pid);

    if (ha_pagato) {
        consuma_pasto(c, &o);
        stats_record_utente_servito(c->stats, c->semid);
    } else if (vado_in_mensa) {
        /* si e' recato in mensa ma non ha potuto pranzare: piatti esauriti
         * oppure mensa chiusa mentre era ancora in coda (sez. 5.5/5.6) */
        stats_record_utente_non_servito(c->stats, c->semid);
    }
}

/* ======================================================================
 * MAIN
 * ====================================================================== */

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "uso: %s <config_path> <capogruppo 0|1> <num_membri> <pid_capogruppo>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *config_path = argv[1];
    int   capogruppo = atoi(argv[2]);
    int   num_membri = atoi(argv[3]);
    pid_t capo_pid   = (pid_t)atoi(argv[4]);

    if (num_membri < 1)          num_membri = 1;
    if (num_membri > MAX_GRUPPO) num_membri = MAX_GRUPPO;
    if (capogruppo) capo_pid = getpid();

    Config cfg = config_load(config_path);
    sim_srand();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_fine_giorno;
    sa.sa_flags = 0;   /* NIENTE SA_RESTART: le attese devono interrompersi */
    sigaction(SIGUSR1, &sa, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_fine_simulazione;
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    Ctx c;
    memset(&c, 0, sizeof(c));
    c.cfg   = &cfg;
    c.semid = sem_set_get(0);
    c.stato = shm_stazioni_attach(shm_stazioni_get(0));
    c.stats = shm_stats_attach(shm_stats_get(0));
    c.mq[STAZIONE_PRIMI]   = msgq_primi_get(0);
    c.mq[STAZIONE_SECONDI] = msgq_secondi_get(0);
    c.mq[STAZIONE_COFFEE]  = msgq_coffee_get(0);
    c.mq[STAZIONE_CASSA]   = msgq_cassa_get(0);
    c.mq_gruppo            = msgq_gruppo_get(0);

    /* sez. 6: l'80% degli utenti (TICKET_PERCENTAGE) e' di tipo
     * "utente w ticket"; il tipo e' una caratteristica del processo e non
     * cambia da un giorno all'altro */
    c.ha_ticket = sim_rand_range(0, 99) < cfg.ticket_percentage;

    /* maschera usata per attendere senza corse la fine della giornata */
    sigset_t mask_fine, mask_orig;
    sigemptyset(&mask_fine);
    sigaddset(&mask_fine, SIGUSR1);
    sigaddset(&mask_fine, SIGTERM);

    /* fine inizializzazione */
    sem_signal_idx(c.semid, SEM_BARRIERA_READY);

    while (!simulazione_finita) {
        sem_wait_idx(c.semid, SEM_GATE_START);
        if (simulazione_finita) break;

        giorno_finito = 0;

        giornata(&c, capogruppo, num_membri, capo_pid);

        /* barriera di fine giornata: si segnala al responsabile di aver
         * concluso e si attende (passivamente, senza corse fra il test dei
         * flag e la sospensione) il SIGUSR1 che chiude la giornata */
        sigprocmask(SIG_BLOCK, &mask_fine, &mask_orig);
        sem_signal_idx(c.semid, SEM_BARRIERA_READY);
        while (!giorno_finito && !simulazione_finita)
            sigsuspend(&mask_orig);
        sigprocmask(SIG_SETMASK, &mask_orig, NULL);
    }

    shm_stazioni_detach(c.stato);
    shm_stats_detach(c.stats);
    return EXIT_SUCCESS;
}
