#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include "common.h"

/* Conteggi necessari per inizializzare i semafori a conteggio
 * (postazioni per stazione + tavoli). Passati dal responsabile_mensa,
 * che li ha letti dalla configurazione. */
typedef struct {
    int postazioni_primi;
    int postazioni_secondi;
    int postazioni_coffee;
    int postazioni_cassa;
    int tavoli;
} IpcSeatCounts;

/* --------------------------------------------------------------------
 * Shared memory: stato stazioni
 * -------------------------------------------------------------------- */

/* Crea (create=1) o si aggancia (create=0) al segmento di shared memory
 * che contiene StatoStazioni. Ritorna lo shmid, -1 su errore. */
int shm_stazioni_get(int create);

/* Attacca il segmento allo spazio di indirizzi del processo chiamante. */
StatoStazioni *shm_stazioni_attach(int shmid);

void shm_stazioni_detach(StatoStazioni *ptr);

/* Da chiamare una sola volta, dal responsabile_mensa, dopo la creazione. */
void shm_stazioni_init(StatoStazioni *stato);

/* --------------------------------------------------------------------
 * Shared memory: statistiche
 * -------------------------------------------------------------------- */

int shm_stats_get(int create);
Statistiche *shm_stats_attach(int shmid);
void shm_stats_detach(Statistiche *ptr);
void shm_stats_init(Statistiche *stats);

/* --------------------------------------------------------------------
 * Semafori (un solo set, indicizzato con IndiceSemaforo)
 * -------------------------------------------------------------------- */

int sem_set_get(int create);

/* Da chiamare una sola volta, dal responsabile_mensa, dopo la creazione:
 * imposta i valori iniziali di tutti i semafori del set. */
void sem_set_init(int semid, const IpcSeatCounts *seats);

/* wait (P) / signal (V) sul semaforo di indice idx (IndiceSemaforo).
 * Bloccanti: nessuna attesa attiva. */
void sem_wait_idx(int semid, int idx);
void sem_signal_idx(int semid, int idx);

/* Indice del semaforo mutex/postazioni per la stazione data. */
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
 * Code di messaggi (una per stazione + una per la cassa)
 * -------------------------------------------------------------------- */

int msgq_primi_get(int create);
int msgq_secondi_get(int create);
int msgq_coffee_get(int create);
int msgq_cassa_get(int create);

/* --------------------------------------------------------------------
 * Cleanup: da chiamare SOLO dal responsabile_mensa a fine simulazione
 * (o da un signal handler in caso di terminazione anomala).
 * -------------------------------------------------------------------- */

void ipc_cleanup_all(int shm_stazioni_id, int shm_stats_id, int semid,
                      int msgq_primi, int msgq_secondi,
                      int msgq_coffee, int msgq_cassa);

/* Da registrare con sigaction() in responsabile_mensa: intercetta
 * SIGINT/SIGTERM e chiama ipc_cleanup_all() prima di uscire, cosi'
 * niente IPC rimane orfana anche in caso di interruzione manuale. */
void ipc_register_cleanup_on_signal(void);

/* Da chiamare (anche piu' volte, sovrascrive) ogni volta che si crea
 * una nuova risorsa IPC, cosi' il signal handler installato da
 * ipc_register_cleanup_on_signal() sa sempre quali id dealloca'ate
 * in caso di interruzione (es. Ctrl+C) a meta' inizializzazione. */
void ipc_track_for_cleanup(int shm_stazioni_id, int shm_stats_id, int semid,
                            int msgq_primi, int msgq_secondi,
                            int msgq_coffee, int msgq_cassa);

int msgq_gruppo_get(int create);

/* firme aggiornate con il nuovo msgq */
void ipc_cleanup_all(int shm_stazioni_id, int shm_stats_id, int semid,
                      int msgq_primi, int msgq_secondi, int msgq_coffee,
                      int msgq_cassa, int msgq_gruppo);

void ipc_track_for_cleanup(int shm_stazioni_id, int shm_stats_id, int semid,
                            int msgq_primi, int msgq_secondi, int msgq_coffee,
                            int msgq_cassa, int msgq_gruppo);

#endif /* IPC_UTILS_H */