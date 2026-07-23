#include "stats.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

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

    stats->operatori_attivi_oggi = 0;
    stats->pause_oggi = 0;
    stats->ricavo_oggi = 0;
    stats->giorno_corrente++;
}

void stats_record_utente_servito(Statistiche *stats, int semid, TipoStazione stazione, double wait_time) {
    if (!stats) return;
    sem_wait_idx(semid, SEM_MUTEX_STATS);
    
    stats->utenti_serviti_oggi++;
    stats->utenti_serviti_tot++;
    
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

void stats_record_piatto_distribuito(Statistiche *stats, int semid, TipoPiatto tipo) {
    if (!stats || tipo < 0 || tipo >= NUM_TIPI_PIATTO) return;
    sem_wait_idx(semid, SEM_MUTEX_STATS);
    
    stats->piatti_distribuiti_oggi[tipo]++;
    stats->piatti_distribuiti_tot[tipo]++;
    
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_piatto_avanzato(Statistiche *stats, int semid, TipoPiatto tipo, int quantita) {
    if (!stats || tipo < 0 || tipo >= NUM_TIPI_PIATTO) return;
    sem_wait_idx(semid, SEM_MUTEX_STATS);
    
    stats->piatti_avanzati_oggi[tipo] += quantita;
    stats->piatti_avanzati_tot[tipo] += quantita;
    
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_pausa(Statistiche *stats, int semid) {
    if (!stats) return;
    sem_wait_idx(semid, SEM_MUTEX_STATS);
    
    stats->pause_oggi++;
    stats->pause_tot++;
    
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_ricavo(Statistiche *stats, int semid, long importo_centesimi) {
    if (!stats) return;
    sem_wait_idx(semid, SEM_MUTEX_STATS);
    
    stats->ricavo_oggi += importo_centesimi;
    stats->ricavo_tot += importo_centesimi;
    
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_operatore_attivo(Statistiche *stats, int semid) {
    if (!stats) return;
    sem_wait_idx(semid, SEM_MUTEX_STATS);
    
    stats->operatori_attivi_oggi++;
    stats->operatori_attivi_tot++;
    
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_print_daily(const Statistiche *stats) {
    if (!stats) return;
    printf("\n=== STATISTICHE GIORNALIERE (Giorno %d) ===\n", stats->giorno_corrente);
    printf("Utenti serviti oggi: %ld\n", stats->utenti_serviti_oggi);
    printf("Utenti non serviti oggi: %ld\n", stats->utenti_non_serviti_oggi);
    printf("Ricavo giornaliero: %.2f EUR\n", (double)stats->ricavo_oggi / 100.0);
    printf("Pause operatori oggi: %ld\n", stats->pause_oggi);
    
    for (int i = 0; i < NUM_TIPI_PIATTO; i++) {
        printf("Piatti tipo %d - Distribuiti: %ld, Avanzati: %ld\n", 
               i, stats->piatti_distribuiti_oggi[i], stats->piatti_avanzati_oggi[i]);
    }
}

void stats_print_total(const Statistiche *stats) {
    if (!stats) return;
    printf("\n=== STATISTICHE TOTALI SIMULAZIONE ===\n");
    printf("Giorni totali simulati: %d\n", stats->giorno_corrente);
    printf("Utenti serviti totali: %ld\n", stats->utenti_serviti_tot);
    printf("Utenti non serviti totali: %ld\n", stats->utenti_non_serviti_tot);
    printf("Ricavo totale: %.2f EUR\n", (double)stats->ricavo_tot / 100.0);
    printf("Pause operatori totali: %ld\n", stats->pause_tot);
}

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

void stats_export_to_csv(const Statistiche *stats, const char *filepath) {
    if (!stats || !filepath) return;
    
    crea_cartella_output_se_necessario(filepath);

    FILE *f = fopen(filepath, "w");
    if (!f) {
        fprintf(stderr, "stats: errore nell'apertura del file CSV '%s': %s\n", filepath, strerror(errno));
        return;
    }

    fprintf(f, "Metrica,Valore_Giornaliero,Valore_Totale\n");
    fprintf(f, "Giorni_Simulati,%d,%d\n", stats->giorno_corrente, stats->giorno_corrente);
    fprintf(f, "Utenti_Serviti,%ld,%ld\n", stats->utenti_serviti_oggi, stats->utenti_serviti_tot);
    fprintf(f, "Utenti_Non_Serviti,%ld,%ld\n", stats->utenti_non_serviti_oggi, stats->utenti_non_serviti_tot);
    fprintf(f, "Ricavo_EUR,%.2f,%.2f\n", (double)stats->ricavo_oggi / 100.0, (double)stats->ricavo_tot / 100.0);
    fprintf(f, "Pause_Operatori,%ld,%ld\n", stats->pause_oggi, stats->pause_tot);

    for (int i = 0; i < NUM_TIPI_PIATTO; i++) {
        fprintf(f, "Piatti_Distribuiti_Tipo_%d,%ld,%ld\n", i, stats->piatti_distribuiti_oggi[i], stats->piatti_distribuiti_tot[i]);
        fprintf(f, "Piatti_Avanzati_Tipo_%d,%ld,%ld\n", i, stats->piatti_avanzati_oggi[i], stats->piatti_avanzati_tot[i]);
    }

    for (int i = 0; i < NUM_STAZIONI; i++) {
        double avg_oggi = stats->conteggio_attesa_oggi[i] > 0 ? stats->somma_attesa_oggi[i] / stats->conteggio_attesa_oggi[i] : 0.0;
        double avg_tot = stats->conteggio_attesa_tot[i] > 0 ? stats->somma_attesa_tot[i] / stats->conteggio_attesa_tot[i] : 0.0;
        fprintf(f, "Tempo_Medio_Attesa_%s,%.2f,%.2f\n", nome_stazione((TipoStazione)i), avg_oggi, avg_tot);
    }

    fclose(f);
}
