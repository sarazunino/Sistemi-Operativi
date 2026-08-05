#ifndef STATS_H
#define STATS_H

#include "common.h"

/**
 * @brief Inizializza (azzera) le statistiche cumulative dell'intera
 *        simulazione.
 *
 * @param stats Puntatore alla struttura Statistiche in memoria condivisa.
 */
void stats_init_global(Statistiche *stats);

/**
 * @brief Inizializza (azzera) le statistiche relative alla singola
 *        giornata di simulazione.
 *
 * @param stats Puntatore alla struttura Statistiche in memoria condivisa.
 */
void stats_init_daily(Statistiche *stats);

/**
 * @brief Registra il tempo di attesa di un utente presso una stazione.
 *
 * Va chiamata a ogni singola coda affrontata dall'utente; alimenta sia la
 * media per stazione sia la media complessiva.
 *
 * @param stats     Puntatore alla struttura Statistiche condivisa.
 * @param semid     Identificatore del set di semafori per la mutua
 *                  esclusione sull'aggiornamento.
 * @param stazione  Stazione presso cui il tempo di attesa è stato rilevato
 *                  (primi, secondi, coffee, cassa).
 * @param wait_time Tempo di attesa dell'utente presso quella stazione, in
 *                  millisecondi reali.
 */
void stats_record_attesa(Statistiche *stats, int semid, TipoStazione stazione, double wait_time);

/**
 * @brief Registra il tempo di attesa alla fila per la lettura del ticket.
 *
 * La lettura del ticket non e' una delle 4 stazioni della mensa, quindi
 * viene contabilizzata a parte.
 *
 * @param stats     Puntatore alla struttura Statistiche condivisa.
 * @param semid     Identificatore del set di semafori.
 * @param wait_time Tempo di attesa in millisecondi reali.
 */
void stats_record_attesa_ticket(Statistiche *stats, int semid, double wait_time);

/**
 * @brief Registra un utente servito, cioè che ha ottenuto almeno un piatto,
 *        ha pagato alla cassa e ha consumato il pasto.
 *
 * Da chiamare una sola volta per utente e per giornata.
 *
 * @param stats Puntatore alla struttura Statistiche condivisa.
 * @param semid Identificatore del set di semafori.
 */
void stats_record_utente_servito(Statistiche *stats, int semid);

/**
 * @brief Registra un utente che ha terminato di consumare il pasto al tavolo.
 *
 * @param stats Puntatore alla struttura Statistiche condivisa.
 * @param semid Identificatore del set di semafori.
 */
void stats_record_utente_ha_mangiato(Statistiche *stats, int semid);

/**
 * @brief Registra un utente che abbandona la mensa senza essere stato
 *        servito (fine coda a fine giornata, oppure piatti esauriti).
 *
 * @param stats Puntatore alla struttura Statistiche condivisa.
 * @param semid Identificatore del set di semafori.
 */
void stats_record_utente_non_servito(Statistiche *stats, int semid);

/**
 * @brief Registra l'erogazione di un piatto (o del caffè) a un utente.
 *
 * @param stats Puntatore alla struttura Statistiche condivisa.
 * @param semid Identificatore del set di semafori.
 * @param tipo  Tipologia di piatto distribuito.
 */
void stats_record_piatto_distribuito(Statistiche *stats, int semid, TipoPiatto tipo);

/**
 * @brief Registra le porzioni di un piatto avanzate (non consumate) a
 *        fine giornata.
 *
 * @param stats    Puntatore alla struttura Statistiche condivisa.
 * @param semid    Identificatore del set di semafori.
 * @param tipo     Tipologia di piatto avanzato.
 * @param quantita Numero di porzioni avanzate per quel tipo di piatto.
 */
void stats_record_piatto_avanzato(Statistiche *stats, int semid, TipoPiatto tipo, int quantita);

/**
 * @brief Registra l'inizio di una pausa da parte di un operatore.
 *
 * @param stats Puntatore alla struttura Statistiche condivisa.
 * @param semid Identificatore del set di semafori.
 */
void stats_record_pausa(Statistiche *stats, int semid);

/**
 * @brief Registra un incasso presso la cassa e lo somma al ricavo
 *        giornaliero e totale.
 *
 * @param stats           Puntatore alla struttura Statistiche condivisa.
 * @param semid           Identificatore del set di semafori.
 * @param importo_centesimi Importo pagato dall'utente, in centesimi.
 */
void stats_record_ricavo(Statistiche *stats, int semid, long importo_centesimi);

/**
 * @brief Registra un operatore come "attivo" nella giornata corrente,
 *        se non è già stato conteggiato.
 *
 * @param stats        Puntatore alla struttura Statistiche condivisa.
 * @param semid        Identificatore del set di semafori per la mutua
 *                     esclusione sull'aggiornamento.
 * @param operatore_id Identificativo univoco dell'operatore che ha
 *                     ottenuto l'accesso alla postazione.
 */
void stats_record_operatore_attivo(Statistiche *stats, int semid, int operatore_id);

/**
 * @brief Registra le informazioni relative all'utilizzo del ticket da parte
 *        di un utente, aggiornando i contatori dei ticket e gli eventuali
 *        sconti applicati.
 *
 * @param stats      Puntatore alla struttura Statistiche condivisa.
 * @param semid      Identificatore del set di semafori 
 * @param ha_ticket  Indica se l'utente possiede un ticket (true) oppure no
 *                   (false).
 * @param sconto     Valore dello sconto applicato all'utente, espresso in
 *                   centesimi.
 */
void stats_record_ticket(Statistiche *stats, int semid, bool ha_ticket, long sconto);

/**
 * @brief Stampa a schermo le statistiche della giornata appena conclusa.
 *
 * Da chiamare ogni sera, prima di stats_init_daily() per la giornata
 * successiva (altrimenti i contatori "oggi" vengono azzerati prima
 * della stampa).
 *
 * @param stats Puntatore alla struttura Statistiche condivisa (const).
 */
void stats_print_daily(const Statistiche *stats);

/**
 * @brief Stampa a schermo le statistiche cumulative della simulazione,
 *        calcolate sui giorni trascorsi fino ad ora incluso.
 *
 * Da chiamare ogni sera, subito dopo stats_print_daily(), riportando:
 * utenti serviti/non serviti totali e medi al giorno; piatti
 * distribuiti/avanzati totali e per tipo, totali e medi al giorno;
 * tempo medio di attesa (complessivo e per stazione) su tutta la
 * simulazione; numero di operatori distinti attivi nella simulazione;
 * totale pause effettuate; ricavo totale e medio per giornata.
 *
 * @param stats Puntatore alla struttura Statistiche condivisa (const).
 */
void stats_print_total(const Statistiche *stats);

/**
 * @brief Esporta in append, come nuova riga, le statistiche della
 *        giornata indicata su un file CSV.
 *
 * @param stats    Puntatore alla struttura Statistiche condivisa (const).
 * @param filepath Percorso del file CSV di destinazione.
 * @param giorno   Numero della giornata a cui si riferiscono i dati
 *                 esportati (1-based), usato per popolare la colonna
 *                 corrispondente nel CSV.
 */
void stats_export_to_csv(const Statistiche *stats, const char *filepath, int giorno);

#endif /* STATS_H */

