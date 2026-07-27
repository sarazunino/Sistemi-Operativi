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
    stats->motivo_terminazione = TERMINAZIONE_IN_CORSO;
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
    stats->utenti_che_hanno_mangiato_oggi = 0;
    stats->utenti_con_ticket_oggi = 0;
    stats->utenti_senza_ticket_oggi = 0;
    stats->sconto_ticket_oggi = 0;
    stats->somma_attesa_ticket_oggi = 0.0;
    stats->conteggio_attesa_ticket_oggi = 0;
    stats->giorno_corrente++;
}

/* ======================================================================
 * Registrazione utenti
 * ====================================================================== */

void stats_record_attesa(Statistiche *stats, int semid, TipoStazione stazione, double wait_time) {
    if (!stats) return;
    if (stazione < 0 || stazione >= NUM_STAZIONI) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);

    stats->somma_attesa_complessiva_oggi += wait_time;
    stats->conteggio_attesa_complessivo_oggi++;
    stats->somma_attesa_complessiva_tot += wait_time;
    stats->conteggio_attesa_complessivo_tot++;

    stats->somma_attesa_oggi[stazione] += wait_time;
    stats->conteggio_attesa_oggi[stazione]++;
    stats->somma_attesa_tot[stazione] += wait_time;
    stats->conteggio_attesa_tot[stazione]++;

    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_attesa_ticket(Statistiche *stats, int semid, double wait_time) {
    if (!stats) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    stats->somma_attesa_ticket_oggi += wait_time;
    stats->conteggio_attesa_ticket_oggi++;
    stats->somma_attesa_ticket_tot += wait_time;
    stats->conteggio_attesa_ticket_tot++;
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_utente_servito(Statistiche *stats, int semid) {
    if (!stats) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    stats->utenti_serviti_oggi++;
    stats->utenti_serviti_tot++;
    sem_signal_idx(semid, SEM_MUTEX_STATS);
}

void stats_record_utente_ha_mangiato(Statistiche *stats, int semid) {
    if (!stats) return;

    sem_wait_idx(semid, SEM_MUTEX_STATS);
    stats->utenti_che_hanno_mangiato_oggi++;
    stats->utenti_che_hanno_mangiato_tot++;
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

void stats_record_pausa(Statistiche *stats, int semid) {
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
 * Helper di stampa
 * ====================================================================== */

static const char *nome_piatto(TipoPiatto tipo) {
    switch (tipo) {
        case TIPO_PRIMO:    return "primo";
        case TIPO_SECONDO:  return "secondo";
        case TIPO_CONTORNO: return "contorno";
        case TIPO_DOLCE:    return "dolce";
        case TIPO_CAFFE:    return "caffe'";
        default:            return "?";
    }
}

static double media(double totale, long conteggio) {
    return (conteggio > 0) ? (totale / (double)conteggio) : 0.0;
}

/* ======================================================================
 * Stampa statistiche giornaliere (sez. 5.1: "quelle della giornata")
 * ====================================================================== */

void stats_print_daily(const Statistiche *stats) {
    if (!stats) return;

    printf("\n=== STATISTICHE GIORNALIERE (Giorno %d) ===\n", stats->giorno_corrente);

    printf("Utenti serviti oggi: %ld\n", stats->utenti_serviti_oggi);
    printf("Utenti non serviti oggi: %ld\n", stats->utenti_non_serviti_oggi);

    long distribuiti_oggi_tot = 0, avanzati_oggi_tot = 0;
    printf("Piatti distribuiti oggi:\n");
    for (int i = 0; i < NUM_TIPI_PIATTO; i++) {
        printf("  - %-9s: distribuiti=%-5ld avanzati=%-5ld\n",
               nome_piatto((TipoPiatto)i), stats->piatti_distribuiti_oggi[i], stats->piatti_avanzati_oggi[i]);
        distribuiti_oggi_tot += stats->piatti_distribuiti_oggi[i];
        avanzati_oggi_tot    += stats->piatti_avanzati_oggi[i];
    }
    printf("  - TOTALE  : distribuiti=%-5ld avanzati=%-5ld\n", distribuiti_oggi_tot, avanzati_oggi_tot);

    printf("Tempo medio di attesa oggi (complessivo): %.2f\n",
           media(stats->somma_attesa_complessiva_oggi, stats->conteggio_attesa_complessivo_oggi));
    for (int i = 0; i < NUM_STAZIONI; i++) {
        printf("  - stazione %-8s: %.2f\n", nome_stazione((TipoStazione)i),
               media(stats->somma_attesa_oggi[i], stats->conteggio_attesa_oggi[i]));
    }

    printf("Operatori attivi oggi: %d\n", stats->operatori_attivi_oggi);
    printf("Pause effettuate oggi: %ld (media per operatore attivo: %.2f)\n",
           stats->pause_oggi,
           stats->operatori_attivi_oggi > 0 ? (double)stats->pause_oggi / stats->operatori_attivi_oggi : 0.0);

    printf("Utenti che hanno consumato il pasto al tavolo oggi: %ld\n",
           stats->utenti_che_hanno_mangiato_oggi);

    printf("Ricavo giornaliero: %.2f EUR\n", (double)stats->ricavo_oggi / 100.0);

    if (stats->utenti_con_ticket_oggi + stats->utenti_senza_ticket_oggi > 0) {
        printf("Utenti con ticket oggi: %ld (sconto totale: %.2f EUR) - senza ticket: %ld\n",
               stats->utenti_con_ticket_oggi, (double)stats->sconto_ticket_oggi / 100.0,
               stats->utenti_senza_ticket_oggi);
        printf("  - attesa media alla lettura ticket: %.2f\n",
               media(stats->somma_attesa_ticket_oggi, stats->conteggio_attesa_ticket_oggi));
    }
}

/* ======================================================================
 * Stampa statistiche totali (sez. 5.1, elenco riportato nell'header)
 * ====================================================================== */

void stats_print_total(const Statistiche *stats) {
    if (!stats) return;

    int giorni = stats->giorno_corrente > 0 ? stats->giorno_corrente : 1;

    printf("\n=== STATISTICHE TOTALI SIMULAZIONE (giorni trascorsi: %d) ===\n", stats->giorno_corrente);

    printf("Utenti serviti totali: %ld (media/giorno: %.2f)\n",
           stats->utenti_serviti_tot, (double)stats->utenti_serviti_tot / giorni);
    printf("Utenti non serviti totali: %ld (media/giorno: %.2f)\n",
           stats->utenti_non_serviti_tot, (double)stats->utenti_non_serviti_tot / giorni);

    long distribuiti_tot = 0, avanzati_tot = 0;
    printf("Piatti distribuiti totali (e media/giorno):\n");
    for (int i = 0; i < NUM_TIPI_PIATTO; i++) {
        printf("  - %-9s: distribuiti=%-6ld (%.2f/g)  avanzati=%-6ld (%.2f/g)\n",
               nome_piatto((TipoPiatto)i),
               stats->piatti_distribuiti_tot[i], (double)stats->piatti_distribuiti_tot[i] / giorni,
               stats->piatti_avanzati_tot[i],    (double)stats->piatti_avanzati_tot[i] / giorni);
        distribuiti_tot += stats->piatti_distribuiti_tot[i];
        avanzati_tot    += stats->piatti_avanzati_tot[i];
    }
    printf("  - TOTALE  : distribuiti=%-6ld (%.2f/g)  avanzati=%-6ld (%.2f/g)\n",
           distribuiti_tot, (double)distribuiti_tot / giorni,
           avanzati_tot,    (double)avanzati_tot / giorni);

    printf("Tempo medio di attesa sulla simulazione (complessivo): %.2f\n",
           media(stats->somma_attesa_complessiva_tot, stats->conteggio_attesa_complessivo_tot));
    for (int i = 0; i < NUM_STAZIONI; i++) {
        printf("  - stazione %-8s: %.2f\n", nome_stazione((TipoStazione)i),
               media(stats->somma_attesa_tot[i], stats->conteggio_attesa_tot[i]));
    }

    printf("Operatori distinti attivi durante la simulazione: %d\n", stats->operatori_attivi_tot);
    printf("Pause totali effettuate nella simulazione: %ld\n", stats->pause_tot);

    printf("Ricavo totale: %.2f EUR (media/giorno: %.2f EUR)\n",
           (double)stats->ricavo_tot / 100.0, (double)stats->ricavo_tot / 100.0 / giorni);

    if (stats->utenti_con_ticket_tot + stats->utenti_senza_ticket_tot > 0) {
        printf("Utenti con ticket totali: %ld (sconto totale: %.2f EUR) - senza ticket: %ld\n",
               stats->utenti_con_ticket_tot, (double)stats->sconto_ticket_tot / 100.0,
               stats->utenti_senza_ticket_tot);
        printf("  - attesa media alla lettura ticket: %.2f\n",
               media(stats->somma_attesa_ticket_tot, stats->conteggio_attesa_ticket_tot));
    }

    if (stats->motivo_terminazione != TERMINAZIONE_IN_CORSO) {
        printf("Motivo terminazione: %s\n",
               stats->motivo_terminazione == TERMINAZIONE_OVERLOAD ? "OVERLOAD" : "TIMEOUT");
    }
}

/* ======================================================================
 * CSV (sez. 6: tutte le statistiche prodotte devono essere salvate in CSV)
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

static int file_vuoto(const char *filepath) {
    struct stat st;
    if (stat(filepath, &st) != 0) return 1; /* non esiste ancora -> va creato con header */
    return st.st_size == 0;
}

void stats_export_to_csv(const Statistiche *stats, const char *filepath, int giorno) {
    if (!stats || !filepath) return;
    crea_cartella_output_se_necessario(filepath);

    int serve_header = file_vuoto(filepath);

    FILE *f = fopen(filepath, "a");
    if (!f) {
        fprintf(stderr, "stats: errore apertura CSV '%s': %s\n", filepath, strerror(errno));
        return;
    }

    if (serve_header) {
        fprintf(f,
            "giorno,utenti_serviti,utenti_non_serviti,utenti_che_hanno_mangiato,"
            "primi_distribuiti,secondi_distribuiti,contorni_distribuiti,dolci_distribuiti,caffe_distribuiti,"
            "primi_avanzati,secondi_avanzati,contorni_avanzati,dolci_avanzati,caffe_avanzati,"
            "attesa_media_primi,attesa_media_secondi,attesa_media_coffee,attesa_media_cassa,"
            "attesa_media_complessiva,attesa_media_ticket,"
            "operatori_attivi,pause,utenti_con_ticket,utenti_senza_ticket,sconto_ticket_eur,ricavo_eur,"
            "utenti_serviti_tot,utenti_non_serviti_tot,piatti_distribuiti_tot,piatti_avanzati_tot,"
            "operatori_attivi_tot,pause_tot,ricavo_tot_eur\n");
    }

    fprintf(f, "%d,%ld,%ld,%ld,", giorno, stats->utenti_serviti_oggi,
            stats->utenti_non_serviti_oggi, stats->utenti_che_hanno_mangiato_oggi);

    for (int i = 0; i < NUM_TIPI_PIATTO; i++) fprintf(f, "%ld,", stats->piatti_distribuiti_oggi[i]);
    for (int i = 0; i < NUM_TIPI_PIATTO; i++) fprintf(f, "%ld,", stats->piatti_avanzati_oggi[i]);

    for (int i = 0; i < NUM_STAZIONI; i++)
        fprintf(f, "%.2f,", media(stats->somma_attesa_oggi[i], stats->conteggio_attesa_oggi[i]));
    fprintf(f, "%.2f,", media(stats->somma_attesa_complessiva_oggi, stats->conteggio_attesa_complessivo_oggi));
    fprintf(f, "%.2f,", media(stats->somma_attesa_ticket_oggi, stats->conteggio_attesa_ticket_oggi));

    fprintf(f, "%d,%ld,%ld,%ld,%.2f,%.2f,",
            stats->operatori_attivi_oggi, stats->pause_oggi,
            stats->utenti_con_ticket_oggi, stats->utenti_senza_ticket_oggi,
            (double)stats->sconto_ticket_oggi / 100.0, (double)stats->ricavo_oggi / 100.0);

    long distribuiti_tot = 0, avanzati_tot = 0;
    for (int i = 0; i < NUM_TIPI_PIATTO; i++) {
        distribuiti_tot += stats->piatti_distribuiti_tot[i];
        avanzati_tot    += stats->piatti_avanzati_tot[i];
    }
    fprintf(f, "%ld,%ld,%ld,%ld,%d,%ld,%.2f\n",
            stats->utenti_serviti_tot, stats->utenti_non_serviti_tot,
            distribuiti_tot, avanzati_tot,
            stats->operatori_attivi_tot, stats->pause_tot,
            (double)stats->ricavo_tot / 100.0);

    fclose(f);
}