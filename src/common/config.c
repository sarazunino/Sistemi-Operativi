#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define MAX_RIGA 256

typedef enum {
    CONFIG_INT,
    CONFIG_LONG,
    CONFIG_STRING
} TipoCampoConfig;

typedef struct {
    const char *chiave;
    int  *dest_int;
    long *dest_long;
    char *dest_string;
    TipoCampoConfig tipo;
} CampoConfig;

// Rimuove gli spazi iniziali e finali da una stringa.
static void trim(char *s) {
    char *start = s;

    while (isspace((unsigned char)*start))
        start++;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

Config config_load(const char *path) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    CampoConfig tabella[] = {
        /* Parametri simulazione */
        {"NOF_WORKERS", &cfg.nof_workers, NULL, NULL, CONFIG_INT},
        {"NOF_USERS", &cfg.nof_users, NULL, NULL, CONFIG_INT},
        {"NOF_TABLE_SEATS", &cfg.nof_table_seats, NULL, NULL, CONFIG_INT},
        {"AVG_CONSUMPTION_TIME", &cfg.avg_consumption_time, NULL, NULL, CONFIG_INT},

        /* Postazioni */
        {"NOF_WK_SEATS_PRIMI", &cfg.nof_wk_seats_primi, NULL, NULL, CONFIG_INT},
        {"NOF_WK_SEATS_SECONDI", &cfg.nof_wk_seats_secondi, NULL, NULL, CONFIG_INT},
        {"NOF_WK_SEATS_COFFEE", &cfg.nof_wk_seats_coffee, NULL, NULL, CONFIG_INT},
        {"NOF_WK_SEATS_CASSA", &cfg.nof_wk_seats_cassa, NULL, NULL, CONFIG_INT},

        /* Tempi servizio */
        {"AVG_SRVC_PRIMI", &cfg.avg_srvc_primi, NULL, NULL, CONFIG_INT},
        {"AVG_SRVC_MAIN_COURSE", &cfg.avg_srvc_main_course, NULL, NULL, CONFIG_INT},
        {"AVG_SRVC_COFFEE", &cfg.avg_srvc_coffee, NULL, NULL, CONFIG_INT},
        {"AVG_SRVC_CASSA", &cfg.avg_srvc_cassa, NULL, NULL, CONFIG_INT},

        /* Gestione porzioni */
        {"AVG_REFILL_PRIMI", &cfg.avg_refill_primi, NULL, NULL, CONFIG_INT},
        {"AVG_REFILL_SECONDI", &cfg.avg_refill_secondi, NULL, NULL, CONFIG_INT},
        {"MAX_PORZIONI_PRIMI", &cfg.max_porzioni_primi, NULL, NULL, CONFIG_INT},
        {"MAX_PORZIONI_SECONDI", &cfg.max_porzioni_secondi, NULL, NULL, CONFIG_INT},
        {"AVG_REFILL_TIME", &cfg.avg_refill_time, NULL, NULL, CONFIG_INT},
        {"REFILL_VARIABILITY_PERCENT", &cfg.refill_variability_percent, NULL, NULL, CONFIG_INT},

        /* Probabilità menu */
        {"PROB_GO_TO_MENSA", &cfg.prob_vado_in_mensa, NULL, NULL, CONFIG_INT},
        {"PROB_PRIMO", &cfg.prob_primo, NULL, NULL, CONFIG_INT},
        {"PROB_SECONDO_CONTORNO", &cfg.prob_secondo_contorno, NULL, NULL, CONFIG_INT},
        {"PROB_DOLCE", &cfg.prob_dolce, NULL, NULL, CONFIG_INT},
        {"PROB_CAFFE", &cfg.prob_caffe, NULL, NULL, CONFIG_INT},

        /* Prezzi */
        {"PRICE_PRIMI", &cfg.price_primi, NULL, NULL, CONFIG_INT},
        {"PRICE_SECONDI", &cfg.price_secondi, NULL, NULL, CONFIG_INT},
        {"PRICE_DOLCE", &cfg.price_dolce, NULL, NULL, CONFIG_INT},
        {"PRICE_COFFEE", &cfg.price_coffee, NULL, NULL, CONFIG_INT},

        /* Pause */
        {"NOF_PAUSE", &cfg.nof_pause, NULL, NULL, CONFIG_INT},
        {"PAUSE_PROBABILITY", &cfg.pause_probability, NULL, NULL, CONFIG_INT},
        {"MIN_PAUSE_TIME", &cfg.min_pause_time, NULL, NULL, CONFIG_INT},
        {"MAX_PAUSE_TIME", &cfg.max_pause_time, NULL, NULL, CONFIG_INT},

        /* Simulazione */
        {"SIM_DURATION", &cfg.sim_duration, NULL, NULL, CONFIG_INT},
        {"DAY_DURATION", &cfg.day_duration, NULL, NULL, CONFIG_INT},
        {"N_NANO_SECS", NULL, &cfg.n_nano_secs, NULL, CONFIG_LONG},
        {"OVERLOAD_THRESHOLD", &cfg.overload_threshold, NULL, NULL, CONFIG_INT},

        /* Gruppi */
        {"MAX_USERS_PER_GROUP", &cfg.max_users_per_group, NULL, NULL, CONFIG_INT},
        /* Versione completa */
        {"STOP_DURATION", &cfg.stop_duration, NULL, NULL, CONFIG_INT},
        {"N_NEW_USERS", &cfg.n_new_users, NULL, NULL, CONFIG_INT},

        /* Ticket */
        {"NOF_TICKET_READERS", &cfg.nof_ticket_readers, NULL, NULL, CONFIG_INT},
        {"AVG_SRVC_TICKET", &cfg.avg_srvc_ticket, NULL, NULL, CONFIG_INT},
        {"TICKET_DISCOUNT_PCT", &cfg.ticket_discount_pct, NULL, NULL, CONFIG_INT},
        {"TICKET_PERCENTAGE", &cfg.ticket_percentage, NULL, NULL, CONFIG_INT},

        /* File */
        {"MENU_FILE_PATH", NULL, NULL, cfg.menu_file_path, CONFIG_STRING},
        {"CSV_OUTPUT_DIR", NULL, NULL, cfg.csv_output_dir, CONFIG_STRING}
    };

    const size_t n_campi = sizeof(tabella) / sizeof(tabella[0]);
    FILE *f = fopen(path, "r");

    if (!f) {
        fprintf(stderr,"config: impossibile aprire '%s': %s\n",path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char riga[MAX_RIGA];
    int trovati[128] = {0};

    while (fgets(riga, sizeof(riga), f)) {
        trim(riga);
        if (riga[0] == '\0' || riga[0] == '#')
            continue;

        char *eq = strchr(riga, '=');
        if (!eq)
            continue;
        *eq = '\0';

        char *chiave = riga;
        char *valore = eq + 1;

        char *commento = strchr(valore, '#');
        if (commento) *commento = '\0';

        trim(chiave);
        trim(valore);

        for (size_t i = 0; i < n_campi; i++) {
            if (strcmp(chiave, tabella[i].chiave) == 0) {
                switch(tabella[i].tipo) {
                    case CONFIG_INT:
                        *tabella[i].dest_int = atoi(valore);
                        break;
                    case CONFIG_LONG:
                        *tabella[i].dest_long = atol(valore);
                        break;
                    case CONFIG_STRING:
                        strncpy(tabella[i].dest_string, valore, 255);
                        tabella[i].dest_string[255] = '\0';
                        break;
                }
                trovati[i] = 1;
                break;
            }
        }
    }

    fclose(f);

    for (size_t i = 0; i < n_campi; i++) {
        if (!trovati[i]) {
            fprintf(stderr, "config: parametro '%s' non trovato in %s\n", tabella[i].chiave, path);
        }
    }

    return cfg;
}

void config_print(const Config *cfg) {
    printf("\n=== Configurazione caricata ===\n");
    printf("Utenti=%d Worker=%d Tavoli=%d\n", cfg->nof_users, cfg->nof_workers, cfg->nof_table_seats);
    printf("Postazioni: primi=%d secondi=%d coffee=%d cassa=%d\n", cfg->nof_wk_seats_primi, cfg->nof_wk_seats_secondi, cfg->nof_wk_seats_coffee, cfg->nof_wk_seats_cassa);
    printf("Servizi: primi=%d secondi=%d coffee=%d cassa=%d\n", cfg->avg_srvc_primi, cfg->avg_srvc_main_course,  cfg->avg_srvc_coffee, cfg->avg_srvc_cassa);
    printf("Refill: primi=%d secondi=%d max(%d,%d) ogni ~%ds +-%d%%\n", cfg->avg_refill_primi, cfg->avg_refill_secondi, cfg->max_porzioni_primi, cfg->max_porzioni_secondi, cfg->avg_refill_time, cfg->refill_variability_percent);
    printf("Prezzi (cent): primo=%d secondo=%d dolce=%d caffe=%d\n", cfg->price_primi, cfg->price_secondi, cfg->price_dolce, cfg->price_coffee);
    printf("Ticket: percentuale=%d%% sconto=%d%% lettori=%d\n", cfg->ticket_percentage, cfg->ticket_discount_pct, cfg->nof_ticket_readers);
    printf("Gruppi: max %d utenti; overload oltre %d utenti in coda\n", cfg->max_users_per_group, cfg->overload_threshold);
    printf("Simulazione: giorni=%d durata_giorno=%d N_NANO_SECS=%ld\n",cfg->sim_duration, cfg->day_duration, cfg->n_nano_secs);
    printf("Menu file: %s\n", cfg->menu_file_path);
    printf("CSV output: %s\n", cfg->csv_output_dir);
}