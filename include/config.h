#ifndef CONFIG_H
#define CONFIG_H

/* Tutti i parametri di simulazione, letti a runtime da config.conf
 * (mai hardcoded / mai passati uno a uno da riga di comando, come
 * richiesto dal testo del progetto, sezione 7). */
typedef struct {
    int nof_workers;
    int nof_users;
    int nof_table_seats;

    int nof_wk_seats_primi;
    int nof_wk_seats_secondi;
    int nof_wk_seats_coffee;
    int nof_wk_seats_cassa;

    int avg_srvc_primi;         /* secondi, +-50% */
    int avg_srvc_main_course;   /* secondi, +-50% */
    int avg_srvc_coffee;        /* secondi, +-80% */
    int avg_srvc_cassa;         /* secondi, +-20% */

    int avg_refill_primi;
    int avg_refill_secondi;
    int max_porzioni_primi;
    int max_porzioni_secondi;
    int avg_refill_time;        /* solo versione completa, +-20% */

    int price_primi;            /* centesimi */
    int price_secondi;          /* centesimi */
    int price_coffee;           /* centesimi */

    int nof_pause;
    int sim_duration;           /* giorni */
    long n_nano_secs;           /* durata di un minuto simulato */
    int overload_threshold;

    int max_users_per_group;    /* solo versione completa */
    int stop_duration;          /* solo versione completa, secondi */
    int n_new_users;            /* solo versione completa */
} Config;

Config config_load(const char *path);
void config_print(const Config *cfg);

#endif /* CONFIG_H */
