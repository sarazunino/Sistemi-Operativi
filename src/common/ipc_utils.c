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

/* union semun non e' dichiarata da <sys/sem.h> su glibc: va definita
 * a mano (richiesto da man semctl). */
union semun {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
};

/* Puntatori globali usati SOLO dal signal handler di cleanup, valorizzati
 * da ipc_track_for_cleanup() dopo la creazione delle risorse. */
static int g_shm_stazioni_id = -1;
static int g_shm_stats_id    = -1;
static int g_semid           = -1;
static int g_msgq_primi      = -1;
static int g_msgq_secondi    = -1;
static int g_msgq_coffee     = -1;
static int g_msgq_cassa      = -1;

static key_t genera_chiave(const char *path, int proj_id) {
    key_t k = ftok(path, proj_id);
    if (k == (key_t)-1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }
    return k;
}

/* ======================================================================
 * Shared memory: stato stazioni
 * ====================================================================== */

int shm_stazioni_get(int create) {
    key_t key = genera_chiave(FTOK_PATH_SHM_STAZIONI, FTOK_PROJ_ID_SHM_STAZIONI);
    int flags = create ? (IPC_CREAT | IPC_EXCL | 0600) : 0600;
    int shmid = shmget(key, sizeof(StatoStazioni), flags);
    if (shmid == -1) {
        perror("shmget stazioni");
        exit(EXIT_FAILURE);
    }
    return shmid;
}

StatoStazioni *shm_stazioni_attach(int shmid) {
    void *p = shmat(shmid, NULL, 0);
    if (p == (void *)-1) {
        perror("shmat stazioni");
        exit(EXIT_FAILURE);
    }
    return (StatoStazioni *)p;
}

void shm_stazioni_detach(StatoStazioni *ptr) {
    if (shmdt(ptr) == -1) {
        perror("shmdt stazioni");
    }
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
    int shmid = shmget(key, sizeof(Statistiche), flags);
    if (shmid == -1) {
        perror("shmget stats");
        exit(EXIT_FAILURE);
    }
    return shmid;
}

Statistiche *shm_stats_attach(int shmid) {
    void *p = shmat(shmid, NULL, 0);
    if (p == (void *)-1) {
        perror("shmat stats");
        exit(EXIT_FAILURE);
    }
    return (Statistiche *)p;
}

void shm_stats_detach(Statistiche *ptr) {
    if (shmdt(ptr) == -1) {
        perror("shmdt stats");
    }
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
    int semid = semget(key, NUM_SEMAFORI, flags);
    if (semid == -1) {
        perror("semget");
        exit(EXIT_FAILURE);
    }
    return semid;
}

void sem_set_init(int semid, const IpcSeatCounts *seats) {
    unsigned short valori[NUM_SEMAFORI];

    valori[SEM_MUTEX_STATS]         = 1;
    valori[SEM_MUTEX_STATO_PRIMI]   = 1;
    valori[SEM_MUTEX_STATO_SECONDI] = 1;
    valori[SEM_MUTEX_STATO_COFFEE]  = 1;
    valori[SEM_MUTEX_STATO_CASSA]   = 1;
    valori[SEM_POSTAZIONI_PRIMI]    = (unsigned short)seats->postazioni_primi;
    valori[SEM_POSTAZIONI_SECONDI]  = (unsigned short)seats->postazioni_secondi;
    valori[SEM_POSTAZIONI_COFFEE]   = (unsigned short)seats->postazioni_coffee;
    valori[SEM_POSTAZIONI_CASSA]    = (unsigned short)seats->postazioni_cassa;
    valori[SEM_TAVOLI]              = (unsigned short)seats->tavoli;

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
    op.sem_flg = 0; /* bloccante: nessuna attesa attiva */

    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) continue; /* riprova se interrotta da segnale */
        perror("semop");
        exit(EXIT_FAILURE);
    }
}

void sem_wait_idx(int semid, int idx)   { sem_op(semid, idx, -1); }
void sem_signal_idx(int semid, int idx) { sem_op(semid, idx, +1); }

/* ======================================================================
 * Code di messaggi
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
int msgq_coffee_get(int create) {
    return msgq_get_generic(FTOK_PATH_MSGQ_COFFEE, FTOK_PROJ_ID_MSGQ_COFFEE, create);
}
int msgq_cassa_get(int create) {
    return msgq_get_generic(FTOK_PATH_MSGQ_CASSA, FTOK_PROJ_ID_MSGQ_CASSA, create);
}

/* ======================================================================
 * Cleanup
 * ====================================================================== */

void ipc_cleanup_all(int shm_stazioni_id, int shm_stats_id, int semid,
                     int msgq_primi, int msgq_secondi,
                     int msgq_coffee, int msgq_cassa) {
    if (shm_stazioni_id != -1) shmctl(shm_stazioni_id, IPC_RMID, NULL);
    if (shm_stats_id    != -1) shmctl(shm_stats_id, IPC_RMID, NULL);
    if (semid           != -1) semctl(semid, 0, IPC_RMID);
    if (msgq_primi      != -1) msgctl(msgq_primi, IPC_RMID, NULL);
    if (msgq_secondi    != -1) msgctl(msgq_secondi, IPC_RMID, NULL);
    if (msgq_coffee     != -1) msgctl(msgq_coffee, IPC_RMID, NULL);
    if (msgq_cassa      != -1) msgctl(msgq_cassa, IPC_RMID, NULL);
}

void ipc_track_for_cleanup(int shm_stazioni_id, int shm_stats_id, int semid,
                            int msgq_primi, int msgq_secondi,
                            int msgq_coffee, int msgq_cassa) {
    g_shm_stazioni_id = shm_stazioni_id;
    g_shm_stats_id    = shm_stats_id;
    g_semid           = semid;
    g_msgq_primi      = msgq_primi;
    g_msgq_secondi    = msgq_secondi;
    g_msgq_coffee     = msgq_coffee;
    g_msgq_cassa      = msgq_cassa;
}

static void cleanup_signal_handler(int signo) {
    (void)signo;
    ipc_cleanup_all(g_shm_stazioni_id, g_shm_stats_id, g_semid,
                     g_msgq_primi, g_msgq_secondi, g_msgq_coffee, g_msgq_cassa);
    _exit(EXIT_FAILURE);
}

void ipc_register_cleanup_on_signal(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cleanup_signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}