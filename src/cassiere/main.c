#include "common.h"
#include "ipc_utils.h"
#include "config.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/sem.h>

/* Processo cassiere: gestisce i pagamenti degli utenti presso la
 * stazione cassa. Ricalca la struttura del processo operatore
 * (sez. 5.3/5.4 del testo): compete per una postazione cassa, lavora
 * per tutta la giornata con possibilita' di pause, e si ferma
 * automaticamente (senza attesa attiva) se SEM_CASSA_ATTIVA e' a 0
 * a causa di un communication_disorder in corso (versione completa). */

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t giorno_finito = 0;

static void handle_sigterm(int sig) { (void)sig; running = 0; }
static void handle_sigusr1(int sig) { (void)sig; giorno_finito = 1; }

static int rand_intorno(int media, int pct) {
    if (media <= 0) return 1;
    int delta = (media * pct) / 100;
    int mn = media - delta; if (mn < 1) mn = 1;
    int mx = media + delta; if (mx < mn) mx = mn;
    return mn + rand() % (mx - mn + 1);
}

static int prendi_postazione(int semid) {
    struct sembuf op = { (unsigned short)SEM_POSTAZIONI_CASSA, -1, 0 };
    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) { if (giorno_finito || !running) return 0; continue; }
        perror("semop postazioni cassa");
        return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    srand((unsigned)getpid() ^ (unsigned)time(NULL));

    struct sigaction sa_t, sa_u;
    memset(&sa_t, 0, sizeof(sa_t)); sa_t.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa_t, NULL); sigaction(SIGINT, &sa_t, NULL);
    memset(&sa_u, 0, sizeof(sa_u)); sa_u.sa_handler = handle_sigusr1;
    sigaction(SIGUSR1, &sa_u, NULL);

    Config cfg = config_load(CONFIG_PATH_DEFAULT);

    int semid = sem_set_get(0);
    StatoStazioni *stato = shm_stazioni_attach(shm_stazioni_get(0));
    Statistiche  *stats  = shm_stats_attach(shm_stats_get(0));
    int msgq_cassa = msgq_cassa_get(0);

    if (semid == -1 || msgq_cassa == -1) {
        fprintf(stderr, "cassiere: errore nell'acquisizione delle risorse IPC\n");
        exit(EXIT_FAILURE);
    }

    int avg_srvc = cfg.avg_srvc_cassa;
    int pct = 20; /* +-20%, come da specifica sez. 5 */

    /* barriera iniziale: segnala che l'inizializzazione e' completa */
    sem_signal_idx(semid, SEM_BARRIERA_READY);

    while (running) {
        sem_wait_idx(semid, SEM_GATE_START);
        if (!running) break;
        giorno_finito = 0;

        int ho_postazione = prendi_postazione(semid);
        int pause_fatte = 0;

        if (ho_postazione) {
            stats_record_operatore_attivo(stats, semid);
        }

        while (ho_postazione && !giorno_finito && running) {

            /* Se il communication_disorder ha azzerato SEM_CASSA_ATTIVA,
             * il cassiere si blocca qui (bloccante, no attesa attiva) fino
             * a quando il guasto non viene risolto; poi rilascia subito
             * il semaforo per non impedire agli altri di verificarlo. */
            sem_wait_idx(semid, SEM_CASSA_ATTIVA);
            sem_signal_idx(semid, SEM_CASSA_ATTIVA);

            MsgRichiestaCassa req;
            if (msgrcv(msgq_cassa, &req, sizeof(req) - sizeof(long), MSG_RICHIESTA, 0) == -1) {
                if (errno == EINTR) continue;
                if (errno == EIDRM) break;
                break;
            }

            int totale = 0;
            if (req.ha_primo)       totale += cfg.price_primi;
            if (req.ha_secondo)     totale += cfg.price_secondi;
            if (req.ha_dolce_caffe) totale += cfg.price_coffee;

            usleep((useconds_t)rand_intorno(avg_srvc, pct) * 1000);

            MsgRispostaCassa resp;
            resp.mtype = req.utente_pid;
            resp.totale_pagato = totale;
            msgsnd(msgq_cassa, &resp, sizeof(resp) - sizeof(long), 0);

            stats_record_ricavo(stats, semid, totale);

            /* possibile pausa, solo se resta almeno un altro cassiere attivo */
            if (pause_fatte < cfg.nof_pause && (rand() % 5 == 0)) {
                sem_wait_idx(semid, SEM_MUTEX_STATO_CASSA);
                int attivi = stato->operatori_assegnati[STAZIONE_CASSA]
                           - stato->operatori_in_pausa[STAZIONE_CASSA];
                if (attivi > 1) {
                    stato->operatori_in_pausa[STAZIONE_CASSA]++;
                    sem_signal_idx(semid, SEM_MUTEX_STATO_CASSA);

                    sem_signal_idx(semid, SEM_POSTAZIONI_CASSA);
                    stats_record_pausa(stats, semid);
                    pause_fatte++;

                    usleep((useconds_t)rand_intorno(avg_srvc, pct) * 3000);

                    int rientrato = prendi_postazione(semid);

                    sem_wait_idx(semid, SEM_MUTEX_STATO_CASSA);
                    stato->operatori_in_pausa[STAZIONE_CASSA]--;
                    sem_signal_idx(semid, SEM_MUTEX_STATO_CASSA);

                    if (!rientrato || giorno_finito || !running) { ho_postazione = 0; break; }
                } else {
                    sem_signal_idx(semid, SEM_MUTEX_STATO_CASSA);
                }
            }
        }

        if (ho_postazione) sem_signal_idx(semid, SEM_POSTAZIONI_CASSA);

        /* segnala al responsabile che ha terminato la giornata */
        sem_signal_idx(semid, SEM_BARRIERA_READY);
    }

    shm_stazioni_detach(stato);
    shm_stats_detach(stats);
    return EXIT_SUCCESS;
}