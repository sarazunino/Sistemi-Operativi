#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include "common.h"

typedef struct {
    int postazioni_primi;
    int postazioni_secondi;
    int postazioni_coffee;
    int postazioni_cassa;
    int lettori_ticket;
    int tavoli;
} IpcSeatCounts;

/* --------------------------------------------------------------------
 * Shared memory: stato stazioni
 * -------------------------------------------------------------------- */

int shm_stazioni_get(int create);
StatoStazioni *shm_stazioni_attach(int shmid);
void shm_stazioni_detach(StatoStazioni *ptr);
void shm_stazioni_init(StatoStazioni *stato);

/* --------------------------------------------------------------------
 * Shared memory: statistiche
 * -------------------------------------------------------------------- */

int shm_stats_get(int create);
Statistiche *shm_stats_attach(int shmid);
void shm_stats_detach(Statistiche *ptr);
void shm_stats_init(Statistiche *stats);

/* --------------------------------------------------------------------
 * Semafori
 * -------------------------------------------------------------------- */

int sem_set_get(int create);
void sem_set_init(int semid, const IpcSeatCounts *seats);
void sem_wait_idx(int semid, int idx);
void sem_signal_idx(int semid, int idx);

static inline int sem_mutex_stato_di(TipoStazione s) {
    switch (s) {
        case STAZIONE_PRIMI:   return SEM_MUTEX_STATO_PRIMI;
        case STAZIONE_SECONDI: return SEM_MUTEX_STATO_SECONDI;
        case STAZIONE_COFFEE:  return SEM_MUTEX_STATO_COFFEE;
        case STAZIONE_CASSA:   return SEM_MUTEX_STATO_CASSA;
        default: return -1;
    }
}

static inline int sem_postazioni_di(TipoStazione s) {
    switch (s) {
        case STAZIONE_PRIMI:   return SEM_POSTAZIONI_PRIMI;
        case STAZIONE_SECONDI: return SEM_POSTAZIONI_SECONDI;
        case STAZIONE_COFFEE:  return SEM_POSTAZIONI_COFFEE;
        case STAZIONE_CASSA:   return SEM_POSTAZIONI_CASSA;
        default: return -1;
    }
}

/* --------------------------------------------------------------------
 * Code di messaggi
 * -------------------------------------------------------------------- */

int msgq_primi_get(int create);
int msgq_secondi_get(int create);
int msgq_coffee_get(int create);
int msgq_cassa_get(int create);
int msgq_gruppo_get(int create);

/* --------------------------------------------------------------------
 * Cleanup
 * -------------------------------------------------------------------- */

void ipc_cleanup_all(int shm_stazioni_id, int shm_stats_id, int semid,
                      int msgq_primi, int msgq_secondi, int msgq_coffee,
                      int msgq_cassa, int msgq_gruppo);

void ipc_register_cleanup_on_signal(void);

void ipc_track_for_cleanup(int shm_stazioni_id, int shm_stats_id, int semid,
                            int msgq_primi, int msgq_secondi, int msgq_coffee,
                            int msgq_cassa, int msgq_gruppo);

#endif /* IPC_UTILS_H */