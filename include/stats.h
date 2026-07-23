#ifndef STATS_H
#define STATS_H

#include "common.h"

/* Inizializza le statistiche all'avvio della simulazione o di una nuova giornata */
void stats_init_global(Statistiche *stats);
void stats_init_daily(Statistiche *stats);

/* Funzioni di aggiornamento thread/process-safe protette da semafori */
void stats_record_utente_servito(Statistiche *stats, int semid, TipoStazione stazione, double wait_time);
void stats_record_utente_non_servito(Statistiche *stats, int semid);
void stats_record_piatto_distribuito(Statistiche *stats, int semid, TipoPiatto tipo);
void stats_record_piatto_avanzato(Statistiche *stats, int semid, TipoPiatto tipo, int quantita);
void stats_record_pausa(Statistiche *stats, int semid);
void stats_record_ricavo(Statistiche *stats, int semid, long importo_centesimi);
void stats_record_operatore_attivo(Statistiche *stats, int semid);

/* Stampa a schermo delle statistiche giornaliere e totali (richiesto dal responsabile mensa) */
void stats_print_daily(const Statistiche *stats);
void stats_print_total(const Statistiche *stats);

/* Esportazione in formato CSV (Requisito chiave per la versione "completa" da 30) */
void stats_export_to_csv(const Statistiche *stats, const char *filepath);

#endif /* STATS_H */

