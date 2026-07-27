#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static int g_shm_stazioni_id = -1;
static int g_shm_stats_id    = -1;
static int g_semid           = -1;

static int g_msgq_primi      = -1;
static int g_msgq_secondi    = -1;
static int g_msgq_coffee     = -1;
static int g_msgq_cassa      = -1;
static int g_msgq_gruppo     = -1;
static int g_msgq_addusers   = -1;

/* ======================================================================
 * Utility
 * ====================================================================== */

static key_t genera_chiave(const char *path, int proj_id) {
    key_t key = ftok(path, proj_id);
    if (key == (key_t)-1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }
    return key;
}

/* ======================================================================
 * Shared memory: stato stazioni
 * ====================================================================== */

int shm_stazioni_get(int create) {
    key_t key = genera_chiave(FTOK_PATH_SHM_STAZIONI, FTOK_PROJ_ID_SHM_STAZIONI);
    int flags = create ? (IPC_CREAT | IPC_EXCL | 0600) : 0600;

    int shmid = shmget(key, sizeof(StatoStazioni), flags);
    if (shmid == -1) {
        perror("shmget stato stazioni");
        exit(EXIT_FAILURE);
    }
    return shmid;
}

StatoStazioni *shm_stazioni_attach(int shmid) {
    void *ptr = shmat(shmid, NULL, 0);
    if (ptr == (void *)-1) {
        perror("shmat stato stazioni");
        exit(EXIT_FAILURE);
    }
    return (StatoStazioni *)ptr;
}

void shm_stazioni_detach(StatoStazioni *ptr) {
    if (shmdt(ptr) == -1)
        perror("shmdt stato stazioni");
}

void shm_stazioni_init(StatoStazioni *stato) {
    memset(stato, 0, sizeof(*stato));
}

/* ======================================================================
 * Shared memory: statistiche
 * ====================================================================== */

int shm_stats_get(int create) {
    key_t key = genera_chiave(FTOK_PATH_SHM_STATS, FTOK_PROJ_ID_SHM_STATS);
    int flags = create ? (IPC_CREAT | IPC_EXCL | 0600) : 0600;

    int shmid = shmget(key, sizeof(Statistiche),flags);
    if (shmid == -1) {
        perror("shmget statistiche");
        exit(EXIT_FAILURE);
    }
    return shmid;
}

Statistiche *shm_stats_attach(int shmid) {
    void *ptr = shmat(shmid, NULL, 0);
    if (ptr == (void *)-1) {
        perror("shmat statistiche");
        exit(EXIT_FAILURE);
    }
    return (Statistiche *)ptr;
}

void shm_stats_detach(Statistiche *ptr) {
    if (shmdt(ptr) == -1)
        perror("shmdt statistiche");
}

void shm_stats_init(Statistiche *stats) {
    memset(stats, 0, sizeof(*stats));
}

/* ======================================================================
 * Semafori
 * ====================================================================== */

int sem_set_get(int create) {
    key_t key = genera_chiave(FTOK_PATH_SEM, FTOK_PROJ_ID_SEM);

    int flags = create ? (IPC_CREAT | IPC_EXCL | 0600) : 0600;
    int semid = semget(key,  NUM_SEMAFORI, flags);
    if (semid == -1) {
        perror("semget");
        exit(EXIT_FAILURE);
    }
    return semid;
}

void sem_set_init(int semid, const IpcSeatCounts *seats) {
    unsigned short valori[NUM_SEMAFORI];

    valori[SEM_MUTEX_STATS]          = 1;
    valori[SEM_MUTEX_STATO_PRIMI]    = 1;
    valori[SEM_MUTEX_STATO_SECONDI]  = 1;
    valori[SEM_MUTEX_STATO_COFFEE]   = 1;
    valori[SEM_MUTEX_STATO_CASSA]    = 1;

    valori[SEM_POSTAZIONI_PRIMI] = seats->postazioni_primi;
    valori[SEM_POSTAZIONI_SECONDI] = seats->postazioni_secondi;
    valori[SEM_POSTAZIONI_COFFEE] = seats->postazioni_coffee;
    valori[SEM_POSTAZIONI_CASSA] = seats->postazioni_cassa;
    valori[SEM_POSTAZIONI_TICKET] = seats->lettori_ticket;

    valori[SEM_TAVOLI] = seats->tavoli;

    valori[SEM_GATE_START] = 0;
    valori[SEM_BARRIERA_READY] = 0;
    valori[SEM_MUTEX_ATTESA] = 1;
    valori[SEM_CASSA_ATTIVA] = 1;

    union semun arg;
    arg.array = valori;

    if (semctl(semid, 0, SETALL, arg) == -1) {
        perror("semctl SETALL");
        exit(EXIT_FAILURE);
    }
}

static void sem_op(int semid, int idx, int delta) {
    struct sembuf op;
    op.sem_num = (unsigned short)idx;
    op.sem_op  = (short)delta;
    op.sem_flg = 0;

    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR)
            continue;
        /* il set di semafori non esiste piu': la simulazione e' finita e
         * il responsabile ha gia' deallocato le IPC, quindi non e' un
         * errore ma la normale conclusione del processo */
        if (errno == EIDRM || errno == EINVAL)
            _exit(EXIT_SUCCESS);
        perror("semop");
        exit(EXIT_FAILURE);
    }
}

void sem_wait_idx(int semid, int idx) {
    sem_op(semid, idx, -1);
}

void sem_signal_idx(int semid, int idx) {
    sem_op(semid, idx, +1);
}

int sem_wait_idx_intr(int semid, int idx) {
    struct sembuf op;
    op.sem_num = (unsigned short)idx;
    op.sem_op  = -1;
    op.sem_flg = 0;

    if (semop(semid, &op, 1) == -1) {
        if (errno != EINTR && errno != EIDRM && errno != EINVAL)
            perror("semop (interrompibile)");
        return -1;
    }
    return 0;
}

/* ======================================================================
 * Code messaggi
 * ====================================================================== */

static int msgq_get_generic(const char *path, int proj_id, int create) {
    key_t key = genera_chiave(path, proj_id);
    int flags = create ? (IPC_CREAT | IPC_EXCL | 0600) : 0600;
    int id = msgget(key, flags);

    if (id == -1) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }

    return id;
}

int msgq_primi_get(int create) {
    return msgq_get_generic(FTOK_PATH_MSGQ_PRIMI, FTOK_PROJ_ID_MSGQ_PRIMI, create);
}

int msgq_secondi_get(int create) {
    return msgq_get_generic(FTOK_PATH_MSGQ_SECONDI, FTOK_PROJ_ID_MSGQ_SECONDI, create);
}

int msgq_coffee_get(int create){
    return msgq_get_generic(FTOK_PATH_MSGQ_COFFEE, FTOK_PROJ_ID_MSGQ_COFFEE, create);
}

int msgq_cassa_get(int create) {
    return msgq_get_generic(FTOK_PATH_MSGQ_CASSA, FTOK_PROJ_ID_MSGQ_CASSA, create);
}

int msgq_gruppo_get(int create) {
    return msgq_get_generic(FTOK_PATH_MSGQ_GRUPPO,FTOK_PROJ_ID_MSGQ_GRUPPO,create);
}

int msgq_addusers_get(int create) {
    return msgq_get_generic(FTOK_PATH_MSGQ_ADDUSERS, FTOK_PROJ_ID_MSGQ_ADDUSERS, create);
}

long msgq_num_messaggi(int msgid) {
    struct msqid_ds info;
    if (msgctl(msgid, IPC_STAT, &info) == -1)
        return 0;
    return (long)info.msg_qnum;
}

int msgq_svuota(int msgid) {
    /* buffer dimensionato sul piu' grande dei messaggi del progetto,
     * altrimenti msgrcv fallirebbe con E2BIG senza consumare nulla */
    union {
        MsgRichiestaCibo  richiesta_cibo;
        MsgRispostaCibo   risposta_cibo;
        MsgRichiestaCassa richiesta_cassa;
        MsgRispostaCassa  risposta_cassa;
        MsgOrdineGruppo   ordine;
    } buf;

    int scartati = 0;
    while (msgrcv(msgid, &buf, sizeof(buf) - sizeof(long), 0, IPC_NOWAIT) != -1)
        scartati++;

    return scartati;
}

/* ======================================================================
 * Cleanup
 * ====================================================================== */

void ipc_cleanup_all(int shm_stazioni_id, int shm_stats_id, int semid, int msgq_primi, int msgq_secondi, int msgq_coffee, int msgq_cassa, int msgq_gruppo, int msgq_addusers) {
    if (shm_stazioni_id != -1)
        shmctl(shm_stazioni_id, IPC_RMID, NULL);
    if (shm_stats_id != -1)
        shmctl(shm_stats_id, IPC_RMID, NULL);
    if (semid != -1)
        semctl(semid, 0, IPC_RMID);
    if (msgq_primi != -1)
        msgctl(msgq_primi, IPC_RMID, NULL);
    if (msgq_secondi != -1)
        msgctl(msgq_secondi, IPC_RMID, NULL);
    if (msgq_coffee != -1)
        msgctl(msgq_coffee, IPC_RMID, NULL);
    if (msgq_cassa != -1)
        msgctl(msgq_cassa, IPC_RMID, NULL);
    if (msgq_gruppo != -1)
        msgctl(msgq_gruppo, IPC_RMID, NULL);
    if (msgq_addusers != -1)
        msgctl(msgq_addusers, IPC_RMID, NULL);
}

void ipc_track_for_cleanup(int shm_stazioni_id, int shm_stats_id, int semid, int msgq_primi, int msgq_secondi, int msgq_coffee, int msgq_cassa, int msgq_gruppo, int msgq_addusers) {
    g_shm_stazioni_id = shm_stazioni_id;
    g_shm_stats_id    = shm_stats_id;
    g_semid = semid;
    g_msgq_primi     = msgq_primi;
    g_msgq_secondi   = msgq_secondi;
    g_msgq_coffee    = msgq_coffee;
    g_msgq_cassa     = msgq_cassa;
    g_msgq_gruppo    = msgq_gruppo;
    g_msgq_addusers  = msgq_addusers;
}

/* Il responsabile e' l'unico processo che crea risorse IPC: se viene
 * interrotto (Ctrl-C) porta con se' tutti i figli e poi le dealloca. */
static void cleanup_signal_handler(int signo) {
    (void)signo;
    killpg(0, SIGTERM);
    ipc_cleanup_all(g_shm_stazioni_id, g_shm_stats_id, g_semid, g_msgq_primi,
                    g_msgq_secondi, g_msgq_coffee, g_msgq_cassa, g_msgq_gruppo,
                    g_msgq_addusers);
    _exit(EXIT_FAILURE);
}

void ipc_register_cleanup_on_signal(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cleanup_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTERM);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}