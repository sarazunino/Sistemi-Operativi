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
#include <errno.h>
#include <sys/msg.h>


/* Flag di controllo  */

static volatile sig_atomic_t giorno_finito = 0; /* SIGUSR1: fine giornata */
static volatile sig_atomic_t simulazione_finita = 0; /* SIGTERM: fine simulazione */

static void handler_fine_giorno(int signo) { (void)signo; giorno_finito = 1; }
static void handler_fine_simulazione(int signo) { (void)signo; simulazione_finita = 1; }

static int devo_fermarmi(void) { return giorno_finito || simulazione_finita; }

/* Code di messaggi delle 4 stazioni */

typedef struct {
    int primi;
    int secondi;
    int coffee;
    int cassa;
} CodeStazioni;

static int mq_di(const CodeStazioni *code, TipoStazione tipo) {
    switch (tipo) {
        case STAZIONE_PRIMI: return code->primi;
        case STAZIONE_SECONDI: return code->secondi;
        case STAZIONE_COFFEE: return code->coffee;
        default:             return code->cassa;
    }
}

/* Acquisizione / rilascio di una postazione */

static int acquisisci_postazione(int semid, StatoStazioni *stato, TipoStazione tipo) {
    if (sem_wait_idx_intr(semid, sem_postazioni_di(tipo)) == -1)
        return 0;

    int mutex = sem_mutex_stato_di(tipo);
    sem_wait_idx(semid, mutex);
    stato->postazioni_libere[tipo]--;
    sem_signal_idx(semid, mutex);
    return 1;
}

static void rilascia_postazione(int semid, StatoStazioni *stato, TipoStazione tipo) {
    int mutex = sem_mutex_stato_di(tipo);
    sem_wait_idx(semid, mutex);
    stato->postazioni_libere[tipo]++;
    sem_signal_idx(semid, mutex);
    sem_signal_idx(semid, sem_postazioni_di(tipo));
}

/* Gestione pause */

static int prova_a_mettersi_in_pausa(int semid, StatoStazioni *stato, TipoStazione tipo) {
    int mutex = sem_mutex_stato_di(tipo);
    int concesso;

    sem_wait_idx(semid, mutex);
    int occupate = stato->postazioni_totali[tipo] - stato->postazioni_libere[tipo];
    concesso = (occupate > 1); 
    if (concesso) stato->operatori_in_pausa[tipo]++;
    sem_signal_idx(semid, mutex);

    return concesso;
}

static void rientra_dalla_pausa(int semid, StatoStazioni *stato, TipoStazione tipo) {
    int mutex = sem_mutex_stato_di(tipo);
    sem_wait_idx(semid, mutex);
    stato->operatori_in_pausa[tipo]--;
    sem_signal_idx(semid, mutex);
}

/* Erogazione di un primo o di un secondo*/

static void eroga_primo_o_secondo(int semid, StatoStazioni *stato, TipoStazione tipo,
                                  int scelta_idx, int *out_idx, int *out_prezzo, int *out_esito) {
    int mutex = sem_mutex_stato_di(tipo);
    sem_wait_idx(semid, mutex);

    int num_piatti, *porzioni, *esaurito;
    Piatto *piatti;
    if (tipo == STAZIONE_PRIMI) {
        num_piatti = stato->primi.num_piatti_oggi;
        porzioni = stato->primi.porzioni_disponibili;
        piatti = stato->primi.piatti;
        esaurito = &stato->primi.esaurito;
    } else {
        num_piatti = stato->secondi.num_piatti_oggi;
        porzioni = stato->secondi.porzioni_disponibili;
        piatti = stato->secondi.piatti;
        esaurito = &stato->secondi.esaurito;
    }

    int idx   = (scelta_idx >= 0 && scelta_idx < num_piatti) ? scelta_idx : -1;
    int esito = ESITO_OK;

    if (idx < 0 || porzioni[idx] <= 0) {
        int trovato = -1;
        for (int i = 0; i < num_piatti; i++) {
            if (porzioni[i] > 0) { trovato = i; break; }
        }
        if (trovato >= 0) {
            idx = trovato;
            esito = ESITO_SOSTITUITO;
        } else {
            idx = -1;
            esito = ESITO_TIPO_ESAURITO;
        }
    }

    int prezzo = 0;
    if (esito != ESITO_TIPO_ESAURITO) {
        porzioni[idx]--;
        prezzo = piatti[idx].prezzo;
    }

    int tutte_esaurite = 1;
    for (int i = 0; i < num_piatti; i++) {
        if (porzioni[i] > 0) { tutte_esaurite = 0; break; }
    }
    *esaurito = tutte_esaurite;

    sem_signal_idx(semid, mutex);

    *out_idx = idx;
    *out_prezzo = prezzo;
    *out_esito = esito;
}

static void eroga_coffee(int semid, StatoStazioni *stato, const MsgRichiestaCibo *req, int *out_prezzo, int *out_dolce_servito, int *out_caffe_servito) {
    int mutex = sem_mutex_stato_di(STAZIONE_COFFEE);
    sem_wait_idx(semid, mutex);

    int prezzo = 0;
    int dolce = -1;
    int caffe = -1;

    if (req->dolce_idx >= 0 && stato->coffee.num_dolci > 0) {
        dolce = (req->dolce_idx < stato->coffee.num_dolci) ? req->dolce_idx : 0;
        prezzo += stato->coffee.dolci[dolce].prezzo;
    }
    if (req->tipo_caffe >= 0 && stato->coffee.num_caffe > 0) {
        caffe = (req->tipo_caffe < stato->coffee.num_caffe) ? req->tipo_caffe : 0;
        prezzo += stato->coffee.caffe[caffe].prezzo;
    }

    sem_signal_idx(semid, mutex);

    *out_prezzo = prezzo;
    *out_dolce_servito = dolce;
    *out_caffe_servito = caffe;
}

/* Servizio di una singola richiesta di cibo (primi / secondi / coffee) */

static void servi_richiesta_cibo(const Config *cfg, int semid, int mq, StatoStazioni *stato,
                                 Statistiche *stats, TipoStazione tipo, const MsgRichiestaCibo *req) {
    int avg_srvc, variabilita;
    switch (tipo) {
        case STAZIONE_PRIMI: avg_srvc = cfg->avg_srvc_primi; variabilita = 50; break;
        case STAZIONE_SECONDI: avg_srvc = cfg->avg_srvc_main_course; variabilita = 50; break;
        default:               avg_srvc = cfg->avg_srvc_coffee; variabilita = 80; break;
    }

    sim_sleep_sec(cfg->n_nano_secs, sim_rand_intorno(avg_srvc, variabilita));

    MsgRispostaCibo risposta;
    memset(&risposta, 0, sizeof(risposta));
    risposta.mtype = (long)req->utente_pid;
    risposta.contorno_idx = -1;

    if (tipo == STAZIONE_PRIMI || tipo == STAZIONE_SECONDI) {
        eroga_primo_o_secondo(semid, stato, tipo, req->scelta_idx, &risposta.piatto_idx, &risposta.prezzo, &risposta.esito);

        if (risposta.esito != ESITO_TIPO_ESAURITO) {
            stats_record_piatto_distribuito(stats, semid, (tipo == STAZIONE_PRIMI) ? TIPO_PRIMO : TIPO_SECONDO);

            if (tipo == STAZIONE_SECONDI && req->contorno_idx >= 0 && req->contorno_idx < stato->secondi.num_contorni) {
                risposta.contorno_idx = req->contorno_idx;
                stats_record_piatto_distribuito(stats, semid, TIPO_CONTORNO);
            }
        }
    } else {
        int dolce_servito, caffe_servito;
        eroga_coffee(semid, stato, req, &risposta.prezzo, &dolce_servito, &caffe_servito);

        risposta.esito = ESITO_OK;
        risposta.piatto_idx = dolce_servito;
        if (dolce_servito >= 0) stats_record_piatto_distribuito(stats, semid, TIPO_DOLCE);
        if (caffe_servito >= 0) stats_record_piatto_distribuito(stats, semid, TIPO_CAFFE);
    }

    while (msgsnd(mq, &risposta, sizeof(risposta) - sizeof(long), 0) == -1) {
        if (errno == EINTR) continue;   /* l'utente sta aspettando: riprova */
        if (errno != EIDRM && errno != EINVAL) perror("operatore: msgsnd risposta cibo");
        break;
    }
}

/* Cassa */

static int importo_membro(const Config *cfg, const MsgRichiestaCassa *req, int i) {
    int totale = 0;
    if (req->ha_primo[i])   totale += cfg->price_primi;
    if (req->ha_secondo[i]) totale += cfg->price_secondi;   
    if (req->ha_dolce[i])   totale += cfg->price_dolce;
    if (req->ha_caffe[i])   totale += cfg->price_coffee;
    return totale;
}

static void incassa_gruppo(const Config *cfg, int semid, int mq_cassa,
                           Statistiche *stats, const MsgRichiestaCassa *req) {
    int num = req->num_membri;
    if (num < 1) num = 1;
    if (num > MAX_GRUPPO) num = MAX_GRUPPO;

    for (int i = 0; i < num; i++) {
        sem_wait_idx(semid, SEM_CASSA_ATTIVA);
        sem_signal_idx(semid, SEM_CASSA_ATTIVA);

        sim_sleep_sec(cfg->n_nano_secs, sim_rand_intorno(cfg->avg_srvc_cassa, 20));

        int totale = importo_membro(cfg, req, i);
        int sconto = 0;
        if (req->ha_ticket[i] && cfg->ticket_discount_pct > 0)
            sconto = (totale * cfg->ticket_discount_pct) / 100;

        int pagato = totale - sconto;

        stats_record_ricavo(stats, semid, pagato);
        stats_record_ticket(stats, semid, req->ha_ticket[i] != 0, sconto);

        MsgRispostaCassa resp;
        memset(&resp, 0, sizeof(resp));
        resp.mtype            = (long)req->membri_pid[i];
        resp.totale_pagato    = pagato;
        resp.sconto_applicato = sconto;

        while (msgsnd(mq_cassa, &resp, sizeof(resp) - sizeof(long), 0) == -1) {
            if (errno == EINTR) continue;
            if (errno != EIDRM && errno != EINVAL) perror("operatore: msgsnd ricevuta cassa");
            break;
        }
    }
}

/* Svuotamento di fine giornata */

static void svuota_coda_cibo(int mq) {
    MsgRichiestaCibo richiesta;
    while (msgrcv(mq, &richiesta, sizeof(richiesta) - sizeof(long), MSG_RICHIESTA, IPC_NOWAIT) != -1) {
        MsgRispostaCibo risposta;
        memset(&risposta, 0, sizeof(risposta));
        risposta.mtype = (long)richiesta.utente_pid;
        risposta.esito = ESITO_TIPO_ESAURITO;
        risposta.piatto_idx = -1;
        risposta.contorno_idx = -1;
        risposta.prezzo = 0;
        while (msgsnd(mq, &risposta, sizeof(risposta) - sizeof(long), 0) == -1) {
            if (errno == EINTR) continue;
            break;
        }
    }
}

static void svuota_coda_cassa(int mq_cassa) {
    MsgRichiestaCassa req;
    while (msgrcv(mq_cassa, &req, sizeof(req) - sizeof(long), MSG_RICHIESTA, IPC_NOWAIT) != -1) {
        int num = req.num_membri;
        if (num < 1) num = 1;
        if (num > MAX_GRUPPO) num = MAX_GRUPPO;

        for (int i = 0; i < num; i++) {
            MsgRispostaCassa resp;
            memset(&resp, 0, sizeof(resp));
            resp.mtype = (long)req.membri_pid[i];
            resp.totale_pagato = -1;  
            resp.sconto_applicato = 0;
            while (msgsnd(mq_cassa, &resp, sizeof(resp) - sizeof(long), 0) == -1) {
                if (errno == EINTR) continue;
                break;
            }
        }
    }
}

/* Una giornata di servizio, alla stazione assegnata per oggi */

static void giornata_di_servizio(const Config *cfg, int semid, StatoStazioni *stato, Statistiche *stats, const CodeStazioni *code,
                                 TipoStazione tipo, int id) {
    int pause_fatte = 0;
    int mq = mq_di(code, tipo);

    int postazione_occupata = acquisisci_postazione(semid, stato, tipo);
    if (postazione_occupata) stats_record_operatore_attivo(stats, semid, id);

    while (postazione_occupata && !devo_fermarmi()) {
        if (tipo == STAZIONE_CASSA) {
            MsgRichiestaCassa req;
            if (msgrcv(mq, &req, sizeof(req) - sizeof(long), MSG_RICHIESTA, 0) == -1) {
                if (errno == EINTR) continue; 
                if (errno != EIDRM && errno != EINVAL) perror("operatore: msgrcv cassa");
                break;
            }
            incassa_gruppo(cfg, semid, mq, stats, &req);
        } else {
            MsgRichiestaCibo richiesta;
            if (msgrcv(mq, &richiesta, sizeof(richiesta) - sizeof(long), MSG_RICHIESTA, 0) == -1) {
                if (errno == EINTR) continue;
                if (errno != EIDRM && errno != EINVAL) perror("operatore: msgrcv cibo");
                break;
            }
            servi_richiesta_cibo(cfg, semid, mq, stato, stats, tipo, &richiesta);
        }

        if (!devo_fermarmi() && pause_fatte < cfg->nof_pause && sim_rand_range(0, 99) < cfg->pause_probability &&
            prova_a_mettersi_in_pausa(semid, stato, tipo)) {

            rilascia_postazione(semid, stato, tipo);
            postazione_occupata = 0;
            pause_fatte++;
            stats_record_pausa(stats, semid);

            sim_sleep_sec(cfg->n_nano_secs, sim_rand_range(cfg->min_pause_time, cfg->max_pause_time));

            rientra_dalla_pausa(semid, stato, tipo);

            if (!devo_fermarmi()) { 
                postazione_occupata = acquisisci_postazione(semid, stato, tipo);
                if (postazione_occupata)
                    stats_record_operatore_attivo(stats, semid, id); 
            }
        }
    }

    if (postazione_occupata) {
        if (tipo == STAZIONE_CASSA) svuota_coda_cassa(mq);
        else svuota_coda_cibo(mq);
        rilascia_postazione(semid, stato, tipo);
    }
}

/* MAIN */

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "uso: %s <config_path> <id_operatore>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *config_path = argv[1];
    int id = atoi(argv[2]);

    Config cfg = config_load(config_path);
    sim_srand();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_fine_giorno;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_fine_simulazione;
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    int shm_st_id = shm_stazioni_get(0);
    int shm_stats_id = shm_stats_get(0);
    int semid = sem_set_get(0);

    CodeStazioni code;
    code.primi = msgq_primi_get(0);
    code.secondi = msgq_secondi_get(0);
    code.coffee = msgq_coffee_get(0);
    code.cassa = msgq_cassa_get(0);

    StatoStazioni *stato = shm_stazioni_attach(shm_st_id);
    Statistiche *stats = shm_stats_attach(shm_stats_id);

    sem_signal_idx(semid, SEM_BARRIERA_READY);

    while (!simulazione_finita) {
        sem_wait_idx(semid, SEM_GATE_START);
        if (simulazione_finita) break;

        giorno_finito = 0;

        TipoStazione tipo = stato->stazione_operatore_oggi[id];

        giornata_di_servizio(&cfg, semid, stato, stats, &code, tipo, id);

        sem_signal_idx(semid, SEM_BARRIERA_READY);
    }

    shm_stazioni_detach(stato);
    shm_stats_detach(stats);
    return EXIT_SUCCESS;
}
