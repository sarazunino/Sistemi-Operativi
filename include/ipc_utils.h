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

 /**
 * @brief Ottiene l'identificatore della memoria condivisa contenente lo
 *        stato delle stazioni.
 *
 * @param create Valore diverso da zero per creare il segmento se assente,
 *               zero per aprire un segmento esistente.
 * @return Identificatore del segmento di memoria condivisa.
 */
int shm_stazioni_get(int create);

/**
 * @brief Collega il processo al segmento di memoria condivisa delle
 *        stazioni.
 *
 * @param shmid Identificatore della memoria condivisa.
 * @return Puntatore alla struttura StatoStazioni.
 */
StatoStazioni *shm_stazioni_attach(int shmid);

/**
 * @brief Scollega il processo dalla memoria condivisa delle stazioni.
 *
 * @param ptr Puntatore precedentemente ottenuto tramite
 *            shm_stazioni_attach().
 */
void shm_stazioni_detach(StatoStazioni *ptr);

/**
 * @brief Inizializza il contenuto della memoria condivisa delle stazioni.
 *
 * @param stato Puntatore alla struttura StatoStazioni.
 */
void shm_stazioni_init(StatoStazioni *stato);

/* --------------------------------------------------------------------
 * Shared memory: statistiche
 * -------------------------------------------------------------------- */

 /**
 * @brief Ottiene l'identificatore della memoria condivisa contenente le
 *        statistiche della simulazione.
 *
 * @param create Valore diverso da zero per creare il segmento se assente,
 *               zero per aprire un segmento esistente.
 * @return Identificatore della memoria condivisa.
 */
int shm_stats_get(int create);

/**
 * @brief Collega il processo alla memoria condivisa delle statistiche.
 *
 * @param shmid Identificatore della memoria condivisa.
 * @return Puntatore alla struttura Statistiche.
 */
Statistiche *shm_stats_attach(int shmid);

/**
 * @brief Scollega il processo dalla memoria condivisa delle statistiche.
 *
 * @param ptr Puntatore precedentemente ottenuto tramite
 *            shm_stats_attach().
 */
void shm_stats_detach(Statistiche *ptr);

/* --------------------------------------------------------------------
 * Semafori
 * -------------------------------------------------------------------- */

 /**
 * @brief Ottiene l'identificatore del set di semafori della simulazione.
 *
 * @param create Valore diverso da zero per creare il set se assente,
 *               zero per aprire un set esistente.
 * @return Identificatore del set di semafori.
 */
int sem_set_get(int create);

/**
 * @brief Inizializza il set di semafori della simulazione.
 *
 * @param semid Identificatore del set di semafori.
 * @param seats Numero di risorse disponibili da assegnare ai semafori.
 */
void sem_set_init(int semid, const IpcSeatCounts *seats);

/**
 * @brief Esegue l'operazione P (wait) sul semaforo indicato.
 *
 * @param semid Identificatore del set di semafori.
 * @param idx Indice del semaforo all'interno del set.
 */
void sem_wait_idx(int semid, int idx);

/**
 * @brief Esegue l'operazione V (signal) sul semaforo indicato.
 *
 * @param semid Identificatore del set di semafori.
 * @param idx Indice del semaforo all'interno del set.
 */
void sem_signal_idx(int semid, int idx);

/**
 * @brief Esegue l'operazione P (wait) sul semaforo indicato, ma ritorna
 *        se l'attesa viene interrotta da un segnale.
 *
 * @param semid Identificatore del set di semafori.
 * @param idx Indice del semaforo all'interno del set.
 * @return 0 se la risorsa e' stata acquisita, -1 se l'attesa e' stata
 *         interrotta da un segnale o la risorsa non esiste piu'.
 */
int sem_wait_idx_intr(int semid, int idx);

/**
 * @brief Legge il valore corrente di un semaforo senza modificarlo
 *        (operazione non invasiva, utile per monitorare uno stato senza
 *        competere per la risorsa).
 *
 * @param semid Identificatore del set di semafori.
 * @param idx Indice del semaforo all'interno del set.
 * @return Il valore corrente del semaforo, -1 se il set non esiste piu'.
 */
int sem_getval_idx(int semid, int idx);

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

 /**
 * @brief Restituisce l'identificatore della coda dei messaggi relativa
 *        alla stazione dei primi.
 *
 * @param create Valore diverso da zero per creare la coda se assente.
 * @return Identificatore della coda di messaggi.
 */
int msgq_primi_get(int create);

/**
 * @brief Restituisce l'identificatore della coda dei messaggi relativa
 *        alla stazione dei secondi.
 *
 * @param create Valore diverso da zero per creare la coda se assente.
 * @return Identificatore della coda di messaggi.
 */
int msgq_secondi_get(int create);

/**
 * @brief Restituisce l'identificatore della coda dei messaggi relativa
 *        alla stazione dei caffè.
 *
 * @param create Valore diverso da zero per creare la coda se assente.
 * @return Identificatore della coda di messaggi.
 */
int msgq_coffee_get(int create);

/**
 * @brief Restituisce l'identificatore della coda dei messaggi relativa
 *        all'operatore della cassa.
 *
 * @param create Valore diverso da zero per creare la coda se assente.
 * @return Identificatore della coda di messaggi.
 */
int msgq_cassa_get(int create);

/**
 * @brief Ottiene l'identificatore della coda di messaggi utilizzata per
 *        la comunicazione tra i membri di un gruppo di utenti.
 *
 * @param create Valore diverso da zero per creare la coda se assente,
 *               zero per aprire una coda esistente.
 * @return Identificatore della coda di messaggi.
 */
int msgq_gruppo_get(int create);

/**
 * @brief Ottiene la coda di messaggi usata da ./bin/add_users per
 *        richiedere al responsabile la creazione di nuovi utenti.
 *
 * @param create Valore diverso da zero per creare la coda se assente.
 * @return Identificatore della coda di messaggi.
 */
int msgq_addusers_get(int create);

/**
 * @brief Restituisce il numero di messaggi attualmente presenti in coda.
 *
 * @param msgid Identificatore della coda di messaggi.
 * @return Numero di messaggi in coda, 0 se la coda non e' interrogabile.
 */
long msgq_num_messaggi(int msgid);

/**
 * @brief Svuota completamente una coda di messaggi (non bloccante).
 *
 * @param msgid Identificatore della coda di messaggi.
 * @return Numero di messaggi scartati.
 */
int msgq_svuota(int msgid);

/* --------------------------------------------------------------------
 * Cleanup
 * -------------------------------------------------------------------- */

 /**
 * @brief Rimuove tutte le risorse IPC create dalla simulazione.
 *
 * @param shm_stazioni_id Identificatore della memoria condivisa delle stazioni.
 * @param shm_stats_id Identificatore della memoria condivisa delle statistiche.
 * @param semid Identificatore del set di semafori.
 * @param msgq_primi Coda dei messaggi dei primi.
 * @param msgq_secondi Coda dei messaggi dei secondi.
 * @param msgq_coffee Coda dei messaggi del coffee.
 * @param msgq_cassa Coda dei messaggi della cassa.
 * @param msgq_gruppo Coda dei messaggi dei gruppi.
 * @param msgq_addusers Coda delle richieste di aggiunta utenti.
 */
void ipc_cleanup_all(int shm_stazioni_id, int shm_stats_id, int semid, int msgq_primi, int msgq_secondi, int msgq_coffee, int msgq_cassa, int msgq_gruppo, int msgq_addusers);

/**
 * @brief Registra gli handler dei segnali per la pulizia automatica delle risorse IPC.
 */
void ipc_register_cleanup_on_signal(void);

/**
 * @brief Memorizza gli identificatori delle risorse IPC da rilasciare
 *        automaticamente in caso di terminazione.
 *
 * @param shm_stazioni_id Identificatore della memoria condivisa delle stazioni.
 * @param shm_stats_id Identificatore della memoria condivisa delle statistiche.
 * @param semid Identificatore del set di semafori.
 * @param msgq_primi Coda dei messaggi dei primi.
 * @param msgq_secondi Coda dei messaggi dei secondi.
 * @param msgq_coffee Coda dei messaggi del coffee.
 * @param msgq_cassa Coda dei messaggi della cassa.
 * @param msgq_gruppo Coda dei messaggi dei gruppi.
 * @param msgq_addusers Coda delle richieste di aggiunta utenti.
 */
void ipc_track_for_cleanup(int shm_stazioni_id, int shm_stats_id, int semid, int msgq_primi, int msgq_secondi, int msgq_coffee, int msgq_cassa, int msgq_gruppo, int msgq_addusers);

#endif /* IPC_UTILS_H */