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

static int prendi_postazione(int semid, int sem_postazioni) {
    struct sembuf op = { (unsigned short)sem_postazioni, -1, 0 };
    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) { if (giorno_finito || !running) return 0; continue; }
        perror("semop postazioni"); return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Uso: %s <tipo_stazione 0-3>\n", argv[0]); exit(EXIT_FAILURE); }
    TipoStazione tipo = (TipoStazione)atoi(argv[1]);
    if (tipo < 0 || tipo >= NUM_STAZIONI) { fprintf(stderr, "operatore: tipo non valido\n"); exit(EXIT_FAILURE); }

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

    int msgid = -1;
    switch (tipo) {
        case STAZIONE_PRIMI:   msgid = msgq_primi_get(0); break;
        case STAZIONE_SECONDI: msgid = msgq_secondi_get(0); break;
        case STAZIONE_COFFEE:  msgid = msgq_coffee_get(0); break;
        case STAZIONE_CASSA:   msgid = msgq_cassa_get(0); break;
        default: exit(EXIT_FAILURE);
    }

    int sem_postazioni  = sem_postazioni_di(tipo);
    int sem_mutex_stato = sem_mutex_stato_di(tipo);

    int avg_srvc = 10, pct = 0;
    switch (tipo) {
        case STAZIONE_PRIMI:   avg_srvc = cfg.avg_srvc_primi;       pct = 50; break;
        case STAZIONE_SECONDI: avg_srvc = cfg.avg_srvc_main_course; pct = 50; break;
        case STAZIONE_COFFEE:  avg_srvc = cfg.avg_srvc_coffee;      pct = 80; break;
        case STAZIONE_CASSA:   avg_srvc = cfg.avg_srvc_cassa;       pct = 20; break;
        default: break;
    }

    /* barriera iniziale: segnala che l'inizializzazione è completa */
    sem_signal_idx(semid, SEM_BARRIERA_READY);

    while (running) {
        sem_wait_idx(semid, SEM_GATE_START);
        if (!running) break;
        giorno_finito = 0;

        int ho_postazione = prendi_postazione(semid, sem_postazioni);
        int pause_fatte = 0;

        if (ho_postazione) {
            stats_record_operatore_attivo(stats, semid);
        }

        while (ho_postazione && !giorno_finito && running) {
            if (tipo == STAZIONE_CASSA) {
                /* checkpoint: se communication_disorder ha bloccato la cassa,
                 * qui l'operatore resta bloccato senza attesa attiva */
                sem_wait_idx(semid, SEM_CASSA_ATTIVA);
                sem_signal_idx(semid, SEM_CASSA_ATTIVA);

                MsgRichiestaCassa req;
                if (msgrcv(msgid, &req, sizeof(req) - sizeof(long), MSG_RICHIESTA, 0) == -1) {
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
                msgsnd(msgid, &resp, sizeof(resp) - sizeof(long), 0);

                stats_record_ricavo(stats, semid, totale);

            } else {
                MsgRichiestaCibo req;
                if (msgrcv(msgid, &req, sizeof(req) - sizeof(long), MSG_RICHIESTA, 0) == -1) {
                    if (errno == EINTR) continue;
                    if (errno == EIDRM) break;
                    break;
                }

                MsgRispostaCibo resp;
                resp.mtype = req.utente_pid;
                resp.esito = ESITO_OK;
                resp.piatto_idx = req.scelta_idx;
                resp.prezzo = 0;

                if (tipo == STAZIONE_COFFEE) {
                    resp.prezzo = cfg.price_coffee;
                } else {
                    StatoStazionePrimiSecondi *ss = (tipo == STAZIONE_PRIMI) ? &stato->primi : &stato->secondi;
                    int prezzo_base = (tipo == STAZIONE_PRIMI) ? cfg.price_primi : cfg.price_secondi;

                    sem_wait_idx(semid, sem_mutex_stato);
                    int idx = req.scelta_idx;
                    if (idx < 0 || idx >= ss->num_piatti_oggi || ss->porzioni_disponibili[idx] <= 0) {
                        int trovato = -1;
                        for (int i = 0; i < ss->num_piatti_oggi; i++)
                            if (ss->porzioni_disponibili[i] > 0) { trovato = i; break; }

                        if (trovato == -1) {
                            resp.esito = ESITO_TIPO_ESAURITO;
                            resp.piatto_idx = -1;
                            ss->esaurito = 1;
                        } else {
                            resp.esito = ESITO_SOSTITUITO;
                            resp.piatto_idx = trovato;
                            ss->porzioni_disponibili[trovato]--;
                            resp.prezzo = prezzo_base;
                        }
                    } else {
                        ss->porzioni_disponibili[idx]--;
                        resp.prezzo = prezzo_base;
                    }
                    sem_signal_idx(semid, sem_mutex_stato);
                }

                usleep((useconds_t)rand_intorno(avg_srvc, pct) * 1000);
                msgsnd(msgid, &resp, sizeof(resp) - sizeof(long), 0);

                if (resp.esito != ESITO_TIPO_ESAURITO) {
                    TipoPiatto tp = (tipo == STAZIONE_PRIMI) ? TIPO_PRIMO :
                                     (tipo == STAZIONE_SECONDI) ? TIPO_SECONDO : TIPO_DOLCE_CAFFE;
                    stats_record_piatto_distribuito(stats, semid, tp);
                }
            }

            /* possibile pausa, solo se resta almeno un altro operatore attivo */
            if (pause_fatte < cfg.nof_pause && (rand() % 5 == 0)) {
                sem_wait_idx(semid, sem_mutex_stato);
                int attivi = stato->operatori_assegnati[tipo] - stato->operatori_in_pausa[tipo];
                if (attivi > 1) {
                    stato->operatori_in_pausa[tipo]++;
                    sem_signal_idx(semid, sem_mutex_stato);

                    sem_signal_idx(semid, sem_postazioni);
                    stats_record_pausa(stats, semid);
                    pause_fatte++;

                    usleep((useconds_t)rand_intorno(avg_srvc, pct) * 3000);

                    int rientrato = prendi_postazione(semid, sem_postazioni);

                    sem_wait_idx(semid, sem_mutex_stato);
                    stato->operatori_in_pausa[tipo]--;
                    sem_signal_idx(semid, sem_mutex_stato);

                    if (!rientrato || giorno_finito || !running) { ho_postazione = 0; break; }
                } else {
                    sem_signal_idx(semid, sem_mutex_stato);
                }
            }
        }

        if (ho_postazione) sem_signal_idx(semid, sem_postazioni);

        /* segnala al responsabile che ha terminato la giornata */
        sem_signal_idx(semid, SEM_BARRIERA_READY);
    }

    shm_stazioni_detach(stato);
    shm_stats_detach(stats);
    return EXIT_SUCCESS;
}