#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* Formato atteso, una riga per parametro:
 *   CHIAVE=valore
 * righe vuote o che iniziano per '#' sono ignorate (vedi config.conf). */

#define MAX_RIGA 256

typedef struct {
    const char *chiave;
    int        *dest_int;   /* usato se is_long == 0 */
    long       *dest_long;  /* usato se is_long == 1 */
    int         is_long;
} CampoConfig;

static void trim(char *s) {
    char *start = s;
    while (isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);

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
        {"NOF_WORKERS",           &cfg.nof_workers,           NULL, 0},
        {"NOF_USERS",             &cfg.nof_users,             NULL, 0},
        {"NOF_TABLE_SEATS",       &cfg.nof_table_seats,       NULL, 0},
        {"NOF_WK_SEATS_PRIMI",    &cfg.nof_wk_seats_primi,    NULL, 0},
        {"NOF_WK_SEATS_SECONDI",  &cfg.nof_wk_seats_secondi,  NULL, 0},
        {"NOF_WK_SEATS_COFFEE",   &cfg.nof_wk_seats_coffee,   NULL, 0},
        {"NOF_WK_SEATS_CASSA",    &cfg.nof_wk_seats_cassa,    NULL, 0},
        {"AVG_SRVC_PRIMI",        &cfg.avg_srvc_primi,        NULL, 0},
        {"AVG_SRVC_MAIN_COURSE",  &cfg.avg_srvc_main_course,  NULL, 0},
        {"AVG_SRVC_COFFEE",       &cfg.avg_srvc_coffee,       NULL, 0},
        {"AVG_SRVC_CASSA",        &cfg.avg_srvc_cassa,        NULL, 0},
        {"AVG_REFILL_PRIMI",      &cfg.avg_refill_primi,      NULL, 0},
        {"AVG_REFILL_SECONDI",    &cfg.avg_refill_secondi,    NULL, 0},
        {"MAX_PORZIONI_PRIMI",    &cfg.max_porzioni_primi,    NULL, 0},
        {"MAX_PORZIONI_SECONDI",  &cfg.max_porzioni_secondi,  NULL, 0},
        {"AVG_REFILL_TIME",       &cfg.avg_refill_time,       NULL, 0},
        {"PRICE_PRIMI",           &cfg.price_primi,           NULL, 0},
        {"PRICE_SECONDI",         &cfg.price_secondi,         NULL, 0},
        {"PRICE_COFFEE",          &cfg.price_coffee,          NULL, 0},
        {"NOF_PAUSE",             &cfg.nof_pause,              NULL, 0},
        {"SIM_DURATION",          &cfg.sim_duration,           NULL, 0},
        {"N_NANO_SECS",           NULL, &cfg.n_nano_secs,       1},
        {"OVERLOAD_THRESHOLD",    &cfg.overload_threshold,    NULL, 0},
        {"MAX_USERS_PER_GROUP",   &cfg.max_users_per_group,   NULL, 0},
        {"STOP_DURATION",         &cfg.stop_duration,         NULL, 0},
        {"N_NEW_USERS",           &cfg.n_new_users,           NULL, 0},
    };
    const size_t n_campi = sizeof(tabella) / sizeof(tabella[0]);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "config: impossibile aprire '%s': %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char riga[MAX_RIGA];
    int  trovati[64] = {0};

    while (fgets(riga, sizeof(riga), f)) {
        trim(riga);
        if (riga[0] == '\0' || riga[0] == '#') continue;

        char *eq = strchr(riga, '=');
        if (!eq) continue;
        *eq = '\0';
        char *chiave = riga;
        char *valore = eq + 1;

        char *cancelletto = strchr(valore, '#');
        if (cancelletto) *cancelletto = '\0';
        trim(chiave);
        trim(valore);

        for (size_t i = 0; i < n_campi; i++) {
            if (strcmp(chiave, tabella[i].chiave) == 0) {
                if (tabella[i].is_long) {
                    *tabella[i].dest_long = atol(valore);
                } else {
                    *tabella[i].dest_int = atoi(valore);
                }
                if (i < sizeof(trovati) / sizeof(trovati[0])) trovati[i] = 1;
                break;
            }
        }
    }
    fclose(f);

    for (size_t i = 0; i < n_campi && i < sizeof(trovati) / sizeof(trovati[0]); i++) {
        if (!trovati[i]) {
            fprintf(stderr, "config: attenzione, parametro '%s' non trovato in %s (uso 0)\n",
                    tabella[i].chiave, path);
        }
    }

    return cfg;
}

void config_print(const Config *cfg) {
    printf("=== Configurazione caricata ===\n");
    printf("NOF_WORKERS=%d NOF_USERS=%d NOF_TABLE_SEATS=%d\n", cfg->nof_workers, cfg->nof_users, cfg->nof_table_seats);
    printf("Postazioni: primi=%d secondi=%d coffee=%d cassa=%d\n", cfg->nof_wk_seats_primi, cfg->nof_wk_seats_secondi, 
        cfg->nof_wk_seats_coffee, cfg->nof_wk_seats_cassa);
    printf("Tempi servizio (s): primi=%d secondi=%d coffee=%d cassa=%d\n", cfg->avg_srvc_primi, cfg->avg_srvc_main_course, 
        cfg->avg_srvc_coffee, cfg->avg_srvc_cassa);
    printf("Prezzi (centesimi): primi=%d secondi=%d coffee=%d\n", cfg->price_primi, cfg->price_secondi, cfg->price_coffee);
    printf("SIM_DURATION=%d giorni, N_NANO_SECS=%ld, OVERLOAD_THRESHOLD=%d\n", cfg->sim_duration, cfg->n_nano_secs, cfg->overload_threshold);
}
