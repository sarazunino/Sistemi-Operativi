#include "stats.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ======================================================================
 * Inizializzazione statistiche
 * ====================================================================== */

void stats_init_global(Statistiche *stats) {
    if (!stats) return;

    memset(stats, 0, sizeof(Statistiche));
    stats->giorno_corrente = 0;
}

void stats_init_daily(Statistiche *stats) {
    if (!stats) return;

    stats->utenti_serviti_oggi = 0;
    stats->utenti_non_serviti_oggi = 0;

    for (int i = 0; i < NUM_TIPI_PIATTO; i++) {
        stats->piatti_distribuiti_oggi[i] = 0;
        stats->piatti_avanzati_oggi[i] = 0;
    }
    for (int i = 0; i < NUM_STAZIONI; i++) {
        stats->somma_attesa_oggi[i] = 0.0;
        stats->conteggio_attesa_oggi[i] = 0;
    }

    stats->somma_attesa_complessiva_oggi = 0.0;
    stats->conteggio_attesa_complessivo_oggi = 0;

    stats->operatori_attivi_oggi = 0;

    memset(stats->operatore_contato_oggi, 0, sizeof(stats->operatore_contato_oggi));

    stats->pause_oggi = 0;
    stats->ricavo_oggi = 0;

    stats->utenti_con_ticket_oggi = 0;
    stats->utenti_senza_ticket_oggi = 0;
    stats->sconto_ticket_oggi = 0;

    stats->somma_attesa_ticket_oggi = 0;
    stats->conteggio_attesa_ticket_oggi = 0;

    stats->giorno_corrente++;
}

/* ======================================================================
 * Registrazione utenti
 * ====================================================================== */

void stats_record_utente_servito(Statistiche *stats, int semid, TipoStazione stazione, double wait_time) {
    if (!stats) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);

    stats->utenti_serviti_oggi++;
    stats->utenti_serviti_tot++;
    stats->somma_attesa_complessiva_oggi += wait_time;
    stats->conteggio_attesa_complessivo_oggi++;
    stats->somma_attesa_complessiva_tot += wait_time;
    stats->conteggio_attesa_complessivo_tot++;
    if (stazione >= 0 && stazione < NUM_STAZIONI) {
        stats->somma_attesa_oggi[stazione] += wait_time;
        stats->conteggio_attesa_oggi[stazione]++;
        stats->somma_attesa_tot[stazione] += wait_time;
        stats->conteggio_attesa_tot[stazione]++;
    }
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_utente_non_servito(Statistiche *stats, int semid) {
    if (!stats) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    stats->utenti_non_serviti_oggi++;
    stats->utenti_non_serviti_tot++;
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

/* ======================================================================
 * Piatti
 * ====================================================================== */

void stats_record_piatto_distribuito(Statistiche *stats, int semid, TipoPiatto tipo) {
    if (!stats) return;
    if (tipo < 0 || tipo >= NUM_TIPI_PIATTO) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    stats->piatti_distribuiti_oggi[tipo]++;
    stats->piatti_distribuiti_tot[tipo]++;
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_piatto_avanzato(Statistiche *stats, int semid, TipoPiatto tipo, int quantita) {
    if (!stats) return;
    if (tipo < 0 || tipo >= NUM_TIPI_PIATTO) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    stats->piatti_avanzati_oggi[tipo] += quantita;
    stats->piatti_avanzati_tot[tipo] += quantita;
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

/* ======================================================================
 * Operatori e pause
 * ====================================================================== */

void stats_record_pausa(Statistiche *stats,int semid) {
    if (!stats) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    stats->pause_oggi++;
    stats->pause_tot++;
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_operatore_attivo(Statistiche *stats, int semid, int operatore_id) {
    if (!stats) return;
    if (operatore_id < 0 || operatore_id >= NOF_WORKERS_MAX) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    if (!stats->operatore_contato_oggi[operatore_id]) {
        stats->operatore_contato_oggi[operatore_id] = true;
        stats->operatori_attivi_oggi++;
    }
    if (!stats->operatore_contato_tot[operatore_id]) {
        stats->operatore_contato_tot[operatore_id] = true;
        stats->operatori_attivi_tot++;
    }

    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

/* ======================================================================
 * Ricavi e ticket
 * ====================================================================== */

void stats_record_ricavo(Statistiche *stats, int semid, long importo_centesimi) {
    if (!stats) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    stats->ricavo_oggi += importo_centesimi;
    stats->ricavo_tot += importo_centesimi;
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_ticket(Statistiche *stats, int semid, bool ha_ticket, long sconto) {
    if (!stats) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);

    if (ha_ticket) {
        stats->utenti_con_ticket_oggi++;
        stats->utenti_con_ticket_tot++;
        stats->sconto_ticket_oggi += sconto;
        stats->sconto_ticket_tot += sconto;
    } else {
        stats->utenti_senza_ticket_oggi++;
        stats->utenti_senza_ticket_tot++;
    }
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

/* ======================================================================
 * Stampa statistiche
 * ====================================================================== */

void stats_print_daily(const Statistiche *stats) {
    if (!stats) return;

    printf("\n=== STATISTICHE GIORNALIERE (Giorno %d) ===\n", stats->giorno_corrente);
    printf("Utenti serviti oggi: %ld\n", stats->utenti_serviti_oggi);
    printf("Utenti non serviti oggi: %ld\n", stats->utenti_non_serviti_oggi);
    printf("Ricavo giornaliero: %.2f EUR\n", (double)stats->ricavo_oggi / 100.0);
    printf("Pause operatori oggi: %ld\n", stats->pause_oggi);

    for (int i = 0; i < NUM_TIPI_PIATTO; i++) {
        printf("Tipo piatto %d - distribuiti: %ld - avanzati: %ld\n", i, stats->piatti_distribuiti_oggi[i], stats->piatti_avanzati_oggi[i]);
    }

    if (stats->conteggio_attesa_complessivo_oggi > 0) {
        printf("Tempo medio attesa totale: %.2f\n",stats->somma_attesa_complessiva_oggi / stats->conteggio_attesa_complessivo_oggi);
    }
}

void stats_print_total(const Statistiche *stats) {
    if (!stats) return;

    printf("\n=== STATISTICHE TOTALI SIMULAZIONE ===\n");
    printf("Giorni simulati: %d\n", stats->giorno_corrente);
    printf("Utenti serviti totali: %ld\n", stats->utenti_serviti_tot);
    printf("Utenti non serviti totali: %ld\n", stats->utenti_non_serviti_tot);
    printf("Ricavo totale: %.2f EUR\n", (double)stats->ricavo_tot / 100.0);
    printf("Pause totali: %ld\n", stats->pause_tot);

    if (stats->conteggio_attesa_complessivo_tot > 0) {
        printf("Tempo medio attesa totale: %.2f\n", stats->somma_attesa_complessiva_tot / stats->conteggio_attesa_complessivo_tot);
    }
}

/* ======================================================================
 * CSV
 * ====================================================================== */

static void crea_cartella_output_se_necessario(const char *filepath) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", filepath);
    char *p = strrchr(tmp, '/');
    if (p != NULL) {
        *p = '\0';
        struct stat st = {0};
        if (stat(tmp, &st) == -1) {
            mkdir(tmp, 0700);
        }
    }
}

void stats_export_to_csv(const Statistiche *stats, const char *filepath, int giorno) {
    if (!stats || !filepath) return;
    crea_cartella_output_se_necessario(filepath);

    FILE *f = fopen(filepath, "a");
    if (!f) {
        fprintf(stderr,"stats: errore apertura CSV '%s': %s\n",filepath,strerror(errno));
        return;
    }

    fprintf(f,"%d,%ld,%ld,%.2f,%ld\n", giorno,stats->utenti_serviti_oggi, stats->utenti_non_serviti_oggi,
            (double)stats->ricavo_oggi / 100.0, stats->pause_oggi);

    fclose(f);
}