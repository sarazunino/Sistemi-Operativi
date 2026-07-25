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

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t giorno_finito = 0;

static void handle_sigterm(int sig) { (void)sig; running = 0; }
static void handle_sigusr1(int sig) { (void)sig; giorno_finito = 1; }

static void attesa_inc(int semid, StatoStazioni *stato) {
    sem_wait_idx(semid, SEM_MUTEX_ATTESA);
    stato->utenti_in_attesa++;
    sem_signal_idx(semid, SEM_MUTEX_ATTESA);
}
static void attesa_dec(int semid, StatoStazioni *stato) {
    sem_wait_idx(semid, SEM_MUTEX_ATTESA);
    stato->utenti_in_attesa--;
    sem_signal_idx(semid, SEM_MUTEX_ATTESA);
}

static int ordina_da_stazione(int semid, StatoStazioni *stato, Statistiche *stats, int msgid,
                               TipoStazione staz, int scelta_idx, int tipo_caffe,
                               int *out_idx, int *out_prezzo) {
    MsgRichiestaCibo req;
    req.mtype = MSG_RICHIESTA;
    req.utente_pid = getpid();
    req.tipo_piatto = (staz == STAZIONE_PRIMI) ? TIPO_PRIMO :
                       (staz == STAZIONE_SECONDI) ? TIPO_SECONDO : TIPO_DOLCE_CAFFE;
    req.scelta_idx = scelta_idx;
    req.tipo_caffe = tipo_caffe;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    attesa_inc(semid, stato);

    if (msgsnd(msgid, &req, sizeof(req) - sizeof(long), 0) == -1) {
        attesa_dec(semid, stato);
        return 0;
    }

    MsgRispostaCibo resp;
    for (;;) {
        if (msgrcv(msgid, &resp, sizeof(resp) - sizeof(long), getpid(), 0) != -1) break;
        if (errno == EINTR) { if (giorno_finito || !running) { attesa_dec(semid, stato); return 0; } continue; }
        attesa_dec(semid, stato);
        return 0;
    }
    attesa_dec(semid, stato);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double wait_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    stats_record_utente_servito(stats, semid, staz, wait_ms);

    if (resp.esito == ESITO_TIPO_ESAURITO) return 0;
    *out_idx = resp.piatto_idx;
    *out_prezzo = resp.prezzo;
    return 1;
}

/* Passa dal terminale di lettura ticket (solo se ha_ticket), bloccante,
 * nessuna attesa attiva. */
static void leggi_ticket_se_serve(int semid, const Config *cfg, int ha_ticket) {
    if (!ha_ticket) return;
    sem_wait_idx(semid, SEM_POSTAZIONI_TICKET);
    int t = cfg->avg_srvc_ticket > 0 ? cfg->avg_srvc_ticket : 1;
    int delta = (t * 20) / 100;
    int mn = t - delta; if (mn < 1) mn = 1;
    int mx = t + delta; if (mx < mn) mx = mn;
    int durata = mn + rand() % (mx - mn + 1);
    usleep((useconds_t)durata * 1000);
    sem_signal_idx(semid, SEM_POSTAZIONI_TICKET);
}

int main(int argc, char *argv[]) {
    int capogruppo   = (argc > 1) ? atoi(argv[1]) : 1;
    int num_membri   = (argc > 2) ? atoi(argv[2]) : 1;
    pid_t capo_pid    = (argc > 3) ? (pid_t)atoi(argv[3]) : 0;
    if (num_membri < 1) num_membri = 1;
    if (num_membri > MAX_GRUPPO) num_membri = MAX_GRUPPO;

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
    int mq_primi   = msgq_primi_get(0);
    int mq_secondi = msgq_secondi_get(0);
    int mq_coffee  = msgq_coffee_get(0);
    int mq_cassa   = msgq_cassa_get(0);
    int mq_gruppo  = msgq_gruppo_get(0);

    int ha_ticket = (rand() % 100) < 80;

    sem_signal_idx(semid, SEM_BARRIERA_READY);

    while (running) {
        sem_wait_idx(semid, SEM_GATE_START);
        if (!running) break;
        giorno_finito = 0;

        if (rand() % 100 < 90) {
            int ha_primo = 0, ha_secondo = 0, ha_dolce = 0;
            int primo_idx = -1, secondo_idx = -1, dummy_idx = -1;
            int prezzo_primo = 0, prezzo_secondo = 0, prezzo_dolce = 0;
            int tipo_caffe = rand() % NOF_TIPI_CAFFE;

            if (!giorno_finito && running && stato->primi.num_piatti_oggi > 0) {
                int scelta = rand() % stato->primi.num_piatti_oggi;
                ha_primo = ordina_da_stazione(semid, stato, stats, mq_primi,
                                               STAZIONE_PRIMI, scelta, -1, &primo_idx, &prezzo_primo);
            }
            if (!giorno_finito && running && stato->secondi.num_piatti_oggi > 0) {
                int scelta = rand() % stato->secondi.num_piatti_oggi;
                ha_secondo = ordina_da_stazione(semid, stato, stats, mq_secondi,
                                                 STAZIONE_SECONDI, scelta, -1, &secondo_idx, &prezzo_secondo);
            }
            if (!giorno_finito && running && (ha_primo || ha_secondo) && (rand() % 100 < 70)) {
                ha_dolce = ordina_da_stazione(semid, stato, stats, mq_coffee,
                                               STAZIONE_COFFEE, -1, tipo_caffe, &dummy_idx, &prezzo_dolce);
            }
            (void)prezzo_primo; (void)prezzo_secondo; (void)prezzo_dolce;

            if (!giorno_finito && running && (ha_primo || ha_secondo)) {
                leggi_ticket_se_serve(semid, &cfg, ha_ticket);

                int totale_pagato = 0;

                if (!capogruppo) {
                    /* membro: mando l'ordine al capogruppo, poi aspetto
                     * SOLO la mia risposta di pagamento sulla coda cassa */
                    MsgOrdineGruppo ord;
                    ord.mtype = capo_pid;
                    ord.membro_pid = getpid();
                    ord.ha_ticket = ha_ticket;
                    ord.ha_primo = ha_primo;         ord.primo_idx = primo_idx;
                    ord.ha_secondo = ha_secondo;     ord.secondo_idx = secondo_idx;
                    ord.ha_dolce_caffe = ha_dolce;   ord.tipo_caffe = tipo_caffe;
                    msgsnd(mq_gruppo, &ord, sizeof(ord) - sizeof(long), 0);

                    attesa_inc(semid, stato);
                    MsgRispostaCassa resp; resp.totale_pagato = 0;
                    for (;;) {
                        if (msgrcv(mq_cassa, &resp, sizeof(resp) - sizeof(long), getpid(), 0) != -1) break;
                        if (errno == EINTR) { if (giorno_finito || !running) break; continue; }
                        break;
                    }
                    attesa_dec(semid, stato);
                    totale_pagato = resp.totale_pagato;

                } else {
                    /* capogruppo: raccoglie (num_membri - 1) ordini, poi
                     * costruisce e invia la richiesta aggregata */
                    MsgRichiestaCassa req;
                    req.mtype = MSG_RICHIESTA;
                    req.richiedente_pid = getpid();
                    req.num_membri = num_membri;

                    req.membri_pid[0] = getpid();
                    req.ha_ticket[0] = ha_ticket;
                    req.ha_primo[0] = ha_primo;         req.primo_idx[0] = primo_idx;
                    req.ha_secondo[0] = ha_secondo;     req.secondo_idx[0] = secondo_idx;
                    req.ha_dolce_caffe[0] = ha_dolce;   req.tipo_caffe[0] = tipo_caffe;

                    int raccolti = 1;
                    while (raccolti < num_membri && running) {
                        MsgOrdineGruppo ord;
                        if (msgrcv(mq_gruppo, &ord, sizeof(ord) - sizeof(long), getpid(), 0) == -1) {
                            if (errno == EINTR) {
                                /* fine giornata: chi non ha ancora ordinato
                                 * abbandona (sez. 5.5), procedo con i raccolti */
                                if (giorno_finito || !running) break;
                                continue;
                            }
                            break;
                        }
                        req.membri_pid[raccolti] = ord.membro_pid;
                        req.ha_ticket[raccolti] = ord.ha_ticket;
                        req.ha_primo[raccolti] = ord.ha_primo;         req.primo_idx[raccolti] = ord.primo_idx;
                        req.ha_secondo[raccolti] = ord.ha_secondo;     req.secondo_idx[raccolti] = ord.secondo_idx;
                        req.ha_dolce_caffe[raccolti] = ord.ha_dolce_caffe; req.tipo_caffe[raccolti] = ord.tipo_caffe;
                        raccolti++;
                    }
                    req.num_membri = raccolti;

                    attesa_inc(semid, stato);
                    if (msgsnd(mq_cassa, &req, sizeof(req) - sizeof(long), 0) != -1) {
                        MsgRispostaCassa resp; resp.totale_pagato = 0;
                        for (;;) {
                            if (msgrcv(mq_cassa, &resp, sizeof(resp) - sizeof(long), getpid(), 0) != -1) break;
                            if (errno == EINTR) { if (giorno_finito || !running) break; continue; }
                            break;
                        }
                        totale_pagato = resp.totale_pagato;
                    }
                    attesa_dec(semid, stato);
                }
                (void)totale_pagato;

                if (!giorno_finito && running) {
                    sem_wait_idx(semid, SEM_TAVOLI);
                    int num_piatti = ha_primo + ha_secondo + ha_dolce;
                    usleep((useconds_t)(300000L * (num_piatti > 0 ? num_piatti : 1)));
                    sem_signal_idx(semid, SEM_TAVOLI);
                }
            }

            if (!ha_primo && !ha_secondo) {
                stats_record_utente_non_servito(stats, semid);
            }
        }

        sem_signal_idx(semid, SEM_BARRIERA_READY);
        while (!giorno_finito && running) pause();
    }

    shm_stazioni_detach(stato);
    shm_stats_detach(stats);
    return EXIT_SUCCESS;
}