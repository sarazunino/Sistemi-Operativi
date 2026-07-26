#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <stdbool.h>
#include <string.h>

/* ======================================================================
 * Percorsi / chiavi IPC
 * ====================================================================== */

#define CONFIG_PATH_DEFAULT   "config/config.conf"

#define FTOK_PATH_SHM_STAZIONI  "config/config.conf"
#define FTOK_PATH_SHM_STATS     "config/menu.txt"
#define FTOK_PATH_SEM           "Makefile"
#define FTOK_PATH_MSGQ_PRIMI    "src/common/config.c"
#define FTOK_PATH_MSGQ_SECONDI  "src/common/config.c"
#define FTOK_PATH_MSGQ_COFFEE   "src/common/stats.c"
#define FTOK_PATH_MSGQ_CASSA    "src/common/ipc_utils.c"
#define FTOK_PATH_MSGQ_TICKET     "src/common/ipc_utils.c"
#define FTOK_PATH_MSGQ_GRUPPO   "src/common/stats.c"

#define FTOK_PROJ_ID_SHM_STAZIONI 'A'
#define FTOK_PROJ_ID_SHM_STATS    'B'
#define FTOK_PROJ_ID_SEM          'C'
#define FTOK_PROJ_ID_MSGQ_PRIMI   'D'
#define FTOK_PROJ_ID_MSGQ_SECONDI 'E'
#define FTOK_PROJ_ID_MSGQ_COFFEE  'F'
#define FTOK_PROJ_ID_MSGQ_CASSA   'G'
#define FTOK_PROJ_ID_MSGQ_TICKET  'I'
#define FTOK_PROJ_ID_MSGQ_GRUPPO  'H'

/* ======================================================================
 * Limiti statici (per dimensionare array in shared memory / messaggi)
 * ====================================================================== */

#define MAX_PIATTI_PRIMI     4  
#define MAX_PIATTI_SECONDI   4
#define MAX_CONTORNI         4
#define NOF_TIPI_CAFFE       4   
#define MAX_DOLCI            4
#define MAX_NOME_PIATTO      32
#define MAX_GRUPPO           8   
#define NOF_WORKERS_MAX      32

#define TERMINAZIONE_IN_CORSO 0
#define TERMINAZIONE_TIMEOUT  1
#define TERMINAZIONE_OVERLOAD 2

/* ======================================================================
 * Tipi di stazione / piatto
 * ====================================================================== */

typedef enum {
    STAZIONE_PRIMI = 0,
    STAZIONE_SECONDI,
    STAZIONE_COFFEE,
    STAZIONE_CASSA,
    NUM_STAZIONI
} TipoStazione;

typedef enum {
    TIPO_PRIMO = 0,
    TIPO_SECONDO,
    TIPO_CONTORNO,
    TIPO_DOLCE,
    TIPO_CAFFE,
    NUM_TIPI_PIATTO
} TipoPiatto;

typedef enum {
    CAFFE_NORMALE = 0,
    CAFFE_MACCHIATO,
    CAFFE_DECAFFEINATO,
    CAFFE_GINSENG
} TipoCaffe;

typedef enum {
    ESITO_OK = 0,
    ESITO_SOSTITUITO,
    ESITO_TIPO_ESAURITO
} EsitoServizio;

typedef struct {
    char nome[MAX_NOME_PIATTO];
    int  prezzo;
} Piatto;

/* ======================================================================
 * Shared memory: stato "vivo" delle stazioni di distribuzione
 * ====================================================================== */

typedef struct {
    Piatto piatti[MAX_PIATTI_PRIMI];
    int num_piatti_oggi;
    int porzioni_disponibili[MAX_PIATTI_PRIMI];
    int max_porzioni;
    int esaurito;
} StatoStazionePrimi;

typedef struct {
    Piatto piatti[MAX_PIATTI_SECONDI];
    int num_piatti_oggi;
    int porzioni_disponibili[MAX_PIATTI_SECONDI];
    Piatto contorni[MAX_CONTORNI];
    int num_contorni;
    int contorno_associato[MAX_PIATTI_SECONDI];
    int max_porzioni;
    int esaurito;
} StatoStazioneSecondi;

typedef struct {
    Piatto caffe[NOF_TIPI_CAFFE];
    int num_caffe;
    Piatto dolci[MAX_DOLCI];
    int num_dolci;
} StatoStazioneCoffee;

typedef struct {
    StatoStazionePrimi        primi;
    StatoStazioneSecondi      secondi;
    StatoStazioneCoffee       coffee;

    int postazioni_libere[NUM_STAZIONI];
    int postazioni_totali[NUM_STAZIONI];

    int operatori_assegnati[NUM_STAZIONI];
    int operatori_in_pausa[NUM_STAZIONI];

    int utenti_in_attesa;
    int nof_children_totali;
    int processi_pronti;
} StatoStazioni;

/* ======================================================================
 * Shared memory: statistiche
 * ====================================================================== */

typedef struct {
    long utenti_serviti_oggi;
    long utenti_non_serviti_oggi;
    long utenti_serviti_tot;
    long utenti_non_serviti_tot;

    long piatti_distribuiti_oggi[NUM_TIPI_PIATTO];
    long piatti_avanzati_oggi[NUM_TIPI_PIATTO];
    long piatti_distribuiti_tot[NUM_TIPI_PIATTO];
    long piatti_avanzati_tot[NUM_TIPI_PIATTO];

    double somma_attesa_oggi[NUM_STAZIONI];
    long   conteggio_attesa_oggi[NUM_STAZIONI];
    double somma_attesa_tot[NUM_STAZIONI];
    long   conteggio_attesa_tot[NUM_STAZIONI];

    int  operatori_attivi_oggi;
    int  operatori_attivi_tot;

    bool operatore_contato_oggi[NOF_WORKERS_MAX];
    bool operatore_contato_tot[NOF_WORKERS_MAX];

    int tavoli_occupati;
    int utenti_in_attesa_tavolo;
    long utenti_che_hanno_mangiato;

    long pause_oggi;
    long pause_tot;

    long ricavo_oggi;
    long ricavo_tot;

    long utenti_con_ticket_oggi;
    long utenti_con_ticket_tot;
    long utenti_senza_ticket_oggi;
    long utenti_senza_ticket_tot;

    long sconto_ticket_oggi;
    long sconto_ticket_tot;

    long somma_attesa_ticket_oggi;
    long conteggio_attesa_ticket_oggi;
    long somma_attesa_ticket_tot;
    long conteggio_attesa_ticket_tot;

    double somma_attesa_complessiva_oggi;
    long conteggio_attesa_complessivo_oggi;
    double somma_attesa_complessiva_tot;
    long conteggio_attesa_complessivo_tot;

    int giorno_corrente;
    int motivo_terminazione;
} Statistiche;

/* ======================================================================
 * Messaggi per le code (utente <-> operatore stazione)
 * ====================================================================== */

#define MSG_RICHIESTA 1L

typedef struct {
    long mtype;
    pid_t  utente_pid;
    int    tipo_piatto;
    int    scelta_idx;
    int    contorno_idx;
    int    tipo_caffe;
    int    dolce_idx;
} MsgRichiestaCibo;

typedef struct {
    long mtype;
    int  esito;
    int  piatto_idx;
    int  prezzo;
} MsgRispostaCibo;

/* ======================================================================
 * Messaggi per la coda cassa
 * ====================================================================== */

typedef struct {
    long   mtype;
    pid_t  richiedente_pid;
    int    num_membri;
    pid_t  membri_pid[MAX_GRUPPO];
    int    ha_ticket[MAX_GRUPPO];
    int    ha_primo[MAX_GRUPPO];
    int    primo_idx[MAX_GRUPPO];
    int    ha_secondo[MAX_GRUPPO];
    int    secondo_idx[MAX_GRUPPO];
    int    contorno_idx[MAX_GRUPPO];
    int    ha_dolce[MAX_GRUPPO];
    int    dolce_idx[MAX_GRUPPO];
    int    ha_caffe[MAX_GRUPPO];
    int    tipo_caffe[MAX_GRUPPO];
} MsgRichiestaCassa;

typedef struct {
    long mtype;
    int  totale_pagato;
} MsgRispostaCassa;

/* ======================================================================
 * Messaggio: membro di un gruppo -> capogruppo (pre-cassa)
 * ====================================================================== */

typedef struct {
    long  mtype;
    pid_t membro_pid;
    int   ha_ticket;
    int   ha_primo;
    int   primo_idx;
    int   ha_secondo;
    int   secondo_idx;
    int   contorno_idx;
    int   ha_dolce;
    int   dolce_idx;
    int   ha_caffe;
    int   tipo_caffe;
} MsgOrdineGruppo;

/* ======================================================================
 * Semafori
 * ====================================================================== */

typedef enum {
    SEM_MUTEX_STATS = 0,
    SEM_MUTEX_STATO_PRIMI,
    SEM_MUTEX_STATO_SECONDI,
    SEM_MUTEX_STATO_COFFEE,
    SEM_MUTEX_STATO_CASSA,
    SEM_POSTAZIONI_PRIMI,
    SEM_POSTAZIONI_SECONDI,
    SEM_POSTAZIONI_COFFEE,
    SEM_POSTAZIONI_CASSA,
    SEM_POSTAZIONI_TICKET,
    SEM_TAVOLI,
    SEM_GATE_START,
    SEM_BARRIERA_READY,
    SEM_MUTEX_ATTESA,
    SEM_CASSA_ATTIVA,
    NUM_SEMAFORI
} IndiceSemaforo;

/* ======================================================================
 * Helper di conversione stazione <-> stringa
 * ====================================================================== */

static inline TipoStazione stazione_da_nome(const char *nome) {
    if (strcmp(nome, "PRIMI") == 0)   return STAZIONE_PRIMI;
    if (strcmp(nome, "SECONDI") == 0) return STAZIONE_SECONDI;
    if (strcmp(nome, "COFFEE") == 0)  return STAZIONE_COFFEE;
    if (strcmp(nome, "CASSA") == 0)   return STAZIONE_CASSA;
    return (TipoStazione)-1;
}

static inline const char *nome_stazione(TipoStazione s) {
    switch (s) {
        case STAZIONE_PRIMI:   return "PRIMI";
        case STAZIONE_SECONDI: return "SECONDI";
        case STAZIONE_COFFEE:  return "COFFEE";
        case STAZIONE_CASSA:   return "CASSA";
        default:                 return "?";
    }
}

#endif /* COMMON_H */