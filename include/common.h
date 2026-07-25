#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <string.h>

/* Durata di una giornata lavorativa della mensa, in minuti simulati.
 * Condivisa da responsabile_mensa, operatore, cassiere e utente, cosi'
 * tutti concordano su quando termina il turno giornaliero. */
#define MINUTI_GIORNO 360  /* es. 6 ore di apertura */

/* ======================================================================
 * Percorsi / chiavi IPC
 * ====================================================================== */

#define CONFIG_PATH_DEFAULT   "config/config.conf"
#define MENU_PATH_DEFAULT     "config/menu.txt"
#define STATS_CSV_PATH         "output/stats.csv"

/* File "fantasma" usati con ftok() per generare le chiavi IPC.
 * Devono esistere sul filesystem (basta che siano vuoti). */
#define FTOK_PATH_SHM_STAZIONI  "config/config.conf"
#define FTOK_PATH_SHM_STATS     "config/menu.txt"
#define FTOK_PATH_SEM           "Makefile"
#define FTOK_PATH_MSGQ_PRIMI    "src/common/config.c"
#define FTOK_PATH_MSGQ_SECONDI  "src/common/config.c"
#define FTOK_PATH_MSGQ_COFFEE   "src/common/stats.c"
#define FTOK_PATH_MSGQ_CASSA    "src/common/ipc_utils.c"
#define FTOK_PATH_MSGQ_GRUPPO    "src/common/common.h"

#define FTOK_PROJ_ID_SHM_STAZIONI 'A'
#define FTOK_PROJ_ID_SHM_STATS    'B'
#define FTOK_PROJ_ID_SEM          'C'
#define FTOK_PROJ_ID_MSGQ_PRIMI   'D'
#define FTOK_PROJ_ID_MSGQ_SECONDI 'E'
#define FTOK_PROJ_ID_MSGQ_COFFEE  'F'
#define FTOK_PROJ_ID_MSGQ_CASSA   'G'
#define FTOK_PROJ_ID_MSGQ_GRUPPO 'H'

/* ======================================================================
 * Limiti statici (per dimensionare array in shared memory)
 * ====================================================================== */

#define MAX_PIATTI_PRIMI     4   /* menu.txt ne richiede almeno 2 */
#define MAX_PIATTI_SECONDI   4
#define NOF_TIPI_CAFFE        4   /* normale, macchiato, decaffeinato, ginseng */
#define MAX_NOME_PIATTO      32

/* ======================================================================
 * Tipi di stazione / piatto
 * ====================================================================== */

typedef enum {
    STAZIONE_PRIMI = 0,
    STAZIONE_SECONDI,
    STAZIONE_COFFEE,
    STAZIONE_CASSA,
    NUM_STAZIONI          /* usalo per dimensionare array indicizzati per stazione */
} TipoStazione;

typedef enum {
    TIPO_PRIMO = 0,
    TIPO_SECONDO,          /* comprende il contorno */
    TIPO_DOLCE_CAFFE,
    NUM_TIPI_PIATTO
} TipoPiatto;

typedef enum {
    CAFFE_NORMALE = 0,
    CAFFE_MACCHIATO,
    CAFFE_DECAFFEINATO,
    CAFFE_GINSENG
} TipoCaffe;

typedef enum {
    ESITO_OK = 0,           /* piatto assegnato con successo */
    ESITO_SOSTITUITO,       /* piatto richiesto finito, assegnata alternativa */
    ESITO_TIPO_ESAURITO,    /* tutti i piatti di quel TIPO sono finiti */
} EsitoServizio;

/* Un piatto del giorno, come letto da menu.txt */
typedef struct {
    char nome[MAX_NOME_PIATTO];
    int  prezzo;             /* in centesimi, es. 350 = 3.50 euro */
} Piatto;

/* ======================================================================
 * Messaggio: membro di un gruppo -> capogruppo (pre-cassa)
 * ====================================================================== */
typedef struct {
    long  mtype;            /* = capogruppo_pid */
    pid_t membro_pid;
    int   ha_ticket;
    int   ha_primo;
    int   primo_idx;
    int   ha_secondo;
    int   secondo_idx;
    int   ha_dolce_caffe;
    int   tipo_caffe;
} MsgOrdineGruppo;

/* ======================================================================
 * Shared memory: stato "vivo" delle stazioni di distribuzione
 * (porzioni residue, postazioni operatore, lunghezza code)
 * Protetta da SEM_MUTEX_STAZIONE_PRIMI / SECONDI (vedi sotto);
 * la stazione coffee ha quantita' illimitata quindi non necessita
 * di contatore porzioni.
 * ====================================================================== */

typedef struct {
    Piatto piatti[MAX_PIATTI_PRIMI];
    int    num_piatti_oggi;              /* quanti piatti offerti oggi, <= MAX_PIATTI_PRIMI */
    int    porzioni_disponibili[MAX_PIATTI_PRIMI];
    int    max_porzioni;                 /* = MAX_PORZIONI_PRIMI da config */
    int    esaurito;                     /* 1 se tutti i piatti di questo tipo sono a 0 */
} StatoStazionePrimiSecondi;

typedef struct {
    /* quantita' illimitata: nessun contatore porzioni */
    int placeholder;  /* evita struct vuota, eventualmente statistiche locali */
} StatoStazioneCoffee;

typedef struct {
    StatoStazionePrimiSecondi primi;
    StatoStazionePrimiSecondi secondi;
    StatoStazioneCoffee       coffee;

    /* Postazioni libere/occupate per ciascuna stazione: contatore usato
     * solo a scopo statistico/debug, la mutua esclusione vera e' data
     * dai semafori a conteggio SEM_POSTAZIONI_* */
    int postazioni_libere[NUM_STAZIONI];
    int postazioni_totali[NUM_STAZIONI];

    /* Quanti operatori sono assegnati a ciascuna stazione oggi, e
     * quanti di questi sono attualmente in pausa: servono per la
     * regola "la stazione non puo' restare scoperta" (vedi 5.3).
     * Impostati/azzerati dal responsabile_mensa a inizio giornata,
     * poi aggiornati dagli operatori stessi (protetti dal mutex
     * della singola stazione). */
    int operatori_assegnati[NUM_STAZIONI];
    int operatori_in_pausa[NUM_STAZIONI];

    int utenti_in_attesa;       /* utenti attualmente in una coda qualsiasi (per overload) */
    int nof_children_totali;
    int processi_pronti;
} StatoStazioni;

/* ======================================================================
 * Shared memory: statistiche (scritte da operatori/cassiere/utenti,
 * lette SOLO dal responsabile_mensa). Protetta da SEM_MUTEX_STATS.
 * ====================================================================== */

typedef struct {
    /* utenti */
    long utenti_serviti_oggi;
    long utenti_non_serviti_oggi;
    long utenti_serviti_tot;
    long utenti_non_serviti_tot;

    /* piatti: indice 0=primo,1=secondo,2=dolce_caffe (vedi TipoPiatto) */
    long piatti_distribuiti_oggi[NUM_TIPI_PIATTO];
    long piatti_avanzati_oggi[NUM_TIPI_PIATTO];
    long piatti_distribuiti_tot[NUM_TIPI_PIATTO];
    long piatti_avanzati_tot[NUM_TIPI_PIATTO];

    /* tempi di attesa: somma + contatore per fare la media, per stazione */
    double somma_attesa_oggi[NUM_STAZIONI];
    long   conteggio_attesa_oggi[NUM_STAZIONI];
    double somma_attesa_tot[NUM_STAZIONI];
    long   conteggio_attesa_tot[NUM_STAZIONI];

    /* operatori */
    int  operatori_attivi_oggi;     /* almeno una volta hanno preso una postazione */
    int  operatori_attivi_tot;
    long pause_oggi;
    long pause_tot;

    /* ricavo (in centesimi) */
    long ricavo_oggi;
    long ricavo_tot;

    /* contatore giorni simulati, usato per fare le medie a fine sim. */
    int giorno_corrente;
} Statistiche;

/* ======================================================================
 * Messaggi per le code (utente <-> operatore stazione)
 * mtype = 1              -> richiesta dell'utente (letta dall'operatore)
 * mtype = pid dell'utente -> risposta dell'operatore (letta dall'utente)
 * ====================================================================== */

#define MSG_RICHIESTA 1L

typedef struct {
    long mtype;
    pid_t  utente_pid;
    int    tipo_piatto;        /* TipoPiatto: cosa sta chiedendo */
    int    scelta_idx;         /* indice preferito in piatti[], -1 = indifferente */
    int    tipo_caffe;         /* valido solo se tipo_piatto == TIPO_DOLCE_CAFFE, TipoCaffe o -1 */
} MsgRichiestaCibo;

typedef struct {
    long mtype;                /* = utente_pid del destinatario */
    int  esito;                /* EsitoServizio */
    int  piatto_idx;           /* piatto realmente assegnato, -1 se ESITO_TIPO_ESAURITO */
    int  prezzo;                /* prezzo del piatto assegnato (0 se esaurito) */
} MsgRispostaCibo;

/* ======================================================================
 * Messaggi per la coda cassa
 * ====================================================================== */

typedef struct {
    long   mtype;              /* MSG_RICHIESTA per la richiesta utente->cassa */
    pid_t  utente_pid;
    int    ha_primo;
    int    primo_idx;
    int    ha_secondo;
    int    secondo_idx;
    int    ha_dolce_caffe;
    int    tipo_caffe;         /* TipoCaffe, se applicabile */
} MsgRichiestaCassa;

typedef struct {
    long mtype;                /* = utente_pid del destinatario */
    int  totale_pagato;        /* in centesimi */
} MsgRispostaCassa;

/* ======================================================================
 * Semafori: un solo set System V, indicizzato con questo enum.
 * I semafori "postazioni" sono a conteggio (valore iniziale = NOF_WK_SEATS_*),
 * quelli "mutex" sono binari (valore iniziale = 1).
 * ====================================================================== */

typedef enum {
    SEM_MUTEX_STATS = 0,          /* protegge Statistiche */
    SEM_MUTEX_STATO_PRIMI,        /* protegge lo stato (porzioni/pause) della stazione primi */
    SEM_MUTEX_STATO_SECONDI,      /* idem, stazione secondi */
    SEM_MUTEX_STATO_COFFEE,       /* idem, stazione coffee (solo pause, no porzioni) */
    SEM_MUTEX_STATO_CASSA,        /* idem, stazione cassa (solo pause) */
    SEM_POSTAZIONI_PRIMI,         /* conteggio = NOF_WK_SEATS_PRIMI */
    SEM_POSTAZIONI_SECONDI,       /* conteggio = NOF_WK_SEATS_SECONDI */
    SEM_POSTAZIONI_COFFEE,        /* conteggio = NOF_WK_SEATS_COFFEE */
    SEM_POSTAZIONI_CASSA,         /* conteggio = NOF_WK_SEATS_CASSA */
    SEM_TAVOLI,                   /* conteggio = NOF_TABLE_SEATS */
    SEM_GATE_START,       /* conteggio: rilascia operatori/utenti a inizio giornata */
    SEM_BARRIERA_READY,   /* conteggio: sync iniziale + fine giornata */
    SEM_MUTEX_ATTESA,     /* protegge utenti_in_attesa e nof_children_totali */
    SEM_CASSA_ATTIVA, 
    NUM_SEMAFORI
} IndiceSemaforo;

/* ======================================================================
 * Helper di conversione stazione <-> stringa (usati da riga di comando
 * e nei log). static inline: header-only, nessun file .c dedicato.
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