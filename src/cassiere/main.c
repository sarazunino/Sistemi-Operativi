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

/* ======================================================================
 * Processo operatore cassa (sez. 5.4).
 *
 * Ricalca la struttura del processo operatore: ogni giornata compete per
 * una postazione della stazione cassa, serve le richieste di pagamento e
 * puo' concedersi fino a NOF_PAUSE pause senza mai lasciare la cassa
 * sguarnita.
 *
 * Ogni richiesta di pagamento arriva dal capogruppo e contiene l'ordine
 * di tutti i membri del gruppo (sez. 6: si paga insieme, dopo essersi
 * riforniti di tutti i piatti). Il cassiere calcola l'importo di ciascun
 * membro, applica lo sconto agli utenti con ticket, aggiorna il ricavo e
 * risponde singolarmente a ogni membro.
 *
 * Se il processo communication_disorder ha azzerato SEM_CASSA_ATTIVA, il
 * cassiere si blocca in attesa passiva su quel semaforo prima di
 * incassare, e riprende non appena il guasto e' risolto.
 * ====================================================================== */

static volatile sig_atomic_t giorno_finito      = 0;
static volatile sig_atomic_t simulazione_finita = 0;

static void handler_fine_giorno(int signo)      { (void)signo; giorno_finito = 1; }
static void handler_fine_simulazione(int signo) { (void)signo; simulazione_finita = 1; }

static int devo_fermarmi(void) { return giorno_finito || simulazione_finita; }

/* ======================================================================
 * Postazioni e pause: stessa politica degli operatori di distribuzione
 * ====================================================================== */

static int acquisisci_postazione(int semid, StatoStazioni *stato) {
    if (sem_wait_idx_intr(semid, SEM_POSTAZIONI_CASSA) == -1)
        return 0;

    sem_wait_idx(semid, SEM_MUTEX_STATO_CASSA);
    stato->postazioni_libere[STAZIONE_CASSA]--;
    sem_signal_idx(semid, SEM_MUTEX_STATO_CASSA);
    return 1;
}

static void rilascia_postazione(int semid, StatoStazioni *stato) {
    sem_wait_idx(semid, SEM_MUTEX_STATO_CASSA);
    stato->postazioni_libere[STAZIONE_CASSA]++;
    sem_signal_idx(semid, SEM_MUTEX_STATO_CASSA);
    sem_signal_idx(semid, SEM_POSTAZIONI_CASSA);
}

static int prova_a_mettersi_in_pausa(int semid, StatoStazioni *stato) {
    int concesso;

    sem_wait_idx(semid, SEM_MUTEX_STATO_CASSA);
    int occupate = stato->postazioni_totali[STAZIONE_CASSA] - stato->postazioni_libere[STAZIONE_CASSA];
    concesso = (occupate > 1);
    if (concesso) stato->operatori_in_pausa[STAZIONE_CASSA]++;
    sem_signal_idx(semid, SEM_MUTEX_STATO_CASSA);

    return concesso;
}

static void rientra_dalla_pausa(int semid, StatoStazioni *stato) {
    sem_wait_idx(semid, SEM_MUTEX_STATO_CASSA);
    stato->operatori_in_pausa[STAZIONE_CASSA]--;
    sem_signal_idx(semid, SEM_MUTEX_STATO_CASSA);
}

/* ======================================================================
 * Calcolo dell'importo dovuto da un singolo membro del gruppo
 * ====================================================================== */

static int importo_membro(const Config *cfg, const MsgRichiestaCassa *req, int i) {
    int totale = 0;
    if (req->ha_primo[i])   totale += cfg->price_primi;
    if (req->ha_secondo[i]) totale += cfg->price_secondi;   /* contorno incluso */
    if (req->ha_dolce[i])   totale += cfg->price_dolce;
    if (req->ha_caffe[i])   totale += cfg->price_coffee;
    return totale;
}

/* ======================================================================
 * Incasso di un intero gruppo
 * ====================================================================== */

static void incassa_gruppo(const Config *cfg, int semid, int mq_cassa,
                           Statistiche *stats, const MsgRichiestaCassa *req) {
    int num = req->num_membri;
    if (num < 1) num = 1;
    if (num > MAX_GRUPPO) num = MAX_GRUPPO;

    for (int i = 0; i < num; i++) {
        /* il guasto ai pagamenti automatici blocca la cassa: attesa
         * passiva sul semaforo, nessuna attesa attiva (sez. 6) */
        sem_wait_idx(semid, SEM_CASSA_ATTIVA);
        sem_signal_idx(semid, SEM_CASSA_ATTIVA);

        /* sez. 5: tempo di servizio nell'intorno +-20% di AVG_SRVC_CASSA */
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
            if (errno != EIDRM && errno != EINVAL) perror("cassiere: msgsnd ricevuta");
            break;
        }
    }
}

/* ======================================================================
 * MAIN
 * ====================================================================== */

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
    sa.sa_flags = 0;  /* NIENTE SA_RESTART */
    sigaction(SIGUSR1, &sa, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_fine_simulazione;
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    int semid      = sem_set_get(0);
    int mq_cassa   = msgq_cassa_get(0);
    StatoStazioni *stato = shm_stazioni_attach(shm_stazioni_get(0));
    Statistiche   *stats = shm_stats_attach(shm_stats_get(0));

    /* fine inizializzazione */
    sem_signal_idx(semid, SEM_BARRIERA_READY);

    while (!simulazione_finita) {
        sem_wait_idx(semid, SEM_GATE_START);
        if (simulazione_finita) break;

        giorno_finito = 0;
        int pause_fatte = 0;

        int postazione_occupata = acquisisci_postazione(semid, stato);
        if (postazione_occupata) stats_record_operatore_attivo(stats, semid, id);

        while (postazione_occupata && !devo_fermarmi()) {
            MsgRichiestaCassa req;
            if (msgrcv(mq_cassa, &req, sizeof(req) - sizeof(long), MSG_RICHIESTA, 0) == -1) {
                if (errno == EINTR) continue;
                if (errno != EIDRM && errno != EINVAL) perror("cassiere: msgrcv");
                break;
            }

            incassa_gruppo(&cfg, semid, mq_cassa, stats, &req);

            if (!devo_fermarmi() && pause_fatte < cfg.nof_pause &&
                sim_rand_range(0, 99) < cfg.pause_probability &&
                prova_a_mettersi_in_pausa(semid, stato)) {

                rilascia_postazione(semid, stato);
                postazione_occupata = 0;
                pause_fatte++;
                stats_record_pausa(stats, semid);

                sim_sleep_sec(cfg.n_nano_secs, sim_rand_range(cfg.min_pause_time, cfg.max_pause_time));

                rientra_dalla_pausa(semid, stato);

                if (!devo_fermarmi()) {
                    postazione_occupata = acquisisci_postazione(semid, stato);
                    if (postazione_occupata)
                        stats_record_operatore_attivo(stats, semid, id);
                }
            }
        }

        /* Chi ha gia' inviato una richiesta di pagamento prima della
         * chiusura resta bloccato in un msgrcv che attende proprio la
         * nostra ricevuta (ogni membro del gruppo, mtype = proprio pid):
         * se ce ne andiamo senza rispondere, quegli utenti resterebbero in
         * attesa per sempre. Svuotiamo quindi la coda (non bloccante)
         * rispondendo con un pagamento "non riuscito" (sentinella -1) a
         * ciascun membro delle richieste rimaste in sospeso: rinunciano al
         * pasto, esattamente come chi resta in coda a una stazione di
         * distribuzione (sez. 5.5), e vengono conteggiati fra gli utenti
         * non serviti. */
        if (postazione_occupata) {
            MsgRichiestaCassa req;
            while (msgrcv(mq_cassa, &req, sizeof(req) - sizeof(long),
                          MSG_RICHIESTA, IPC_NOWAIT) != -1) {
                int num = req.num_membri;
                if (num < 1) num = 1;
                if (num > MAX_GRUPPO) num = MAX_GRUPPO;

                for (int i = 0; i < num; i++) {
                    MsgRispostaCassa resp;
                    memset(&resp, 0, sizeof(resp));
                    resp.mtype            = (long)req.membri_pid[i];
                    resp.totale_pagato    = -1;   /* sentinella: pagamento non riuscito */
                    resp.sconto_applicato = 0;
                    while (msgsnd(mq_cassa, &resp, sizeof(resp) - sizeof(long), 0) == -1) {
                        if (errno == EINTR) continue;
                        break;
                    }
                }
            }
            rilascia_postazione(semid, stato);
        }

        sem_signal_idx(semid, SEM_BARRIERA_READY);
    }

    shm_stazioni_detach(stato);
    shm_stats_detach(stats);
    return EXIT_SUCCESS;
}
