#ifndef CONFIG_H
#define CONFIG_H

/* Tutti i tempi (AVG_SRVC_*, pause, consumo, refill, STOP_DURATION) sono
 * espressi in SECONDI DI TEMPO SIMULATO: la conversione in tempo reale
 * avviene tramite N_NANO_SECS (1 minuto simulato = N_NANO_SECS ns reali,
 * quindi 1 secondo simulato = N_NANO_SECS/60 ns reali). Vedi simtime.h. */

typedef struct {

    /* Parametri simulazione */
    int nof_workers;
    int nof_users;
    int nof_table_seats;
    int avg_consumption_time;   /* per piatto acquistato */

    /* Postazioni */
    int nof_wk_seats_primi;
    int nof_wk_seats_secondi;
    int nof_wk_seats_coffee;
    int nof_wk_seats_cassa;

    /* Servizio */
    int avg_srvc_primi;
    int avg_srvc_main_course;
    int avg_srvc_coffee;
    int avg_srvc_cassa;

    /* Porzioni */
    int avg_refill_primi;
    int avg_refill_secondi;
    int max_porzioni_primi;
    int max_porzioni_secondi;
    int avg_refill_time;
    int refill_variability_percent;

    /* Menu */
    int prob_vado_in_mensa;
    int prob_primo;
    int prob_secondo_contorno;
    int prob_dolce;
    int prob_caffe;

    /* Prezzi (in centesimi) */
    int price_primi;
    int price_secondi;
    int price_dolce;
    int price_coffee;

    /* Pause */
    int nof_pause;
    int pause_probability;
    int min_pause_time;
    int max_pause_time;

    /* Giorni */
    int sim_duration;
    int day_duration;           /* minuti simulati per giornata */
    long n_nano_secs;
    int overload_threshold;

    /* Gruppi */
    int max_users_per_group;

    /* Complete */
    int stop_duration;
    int n_new_users;

    /* Ticket */
    int nof_ticket_readers;
    int avg_srvc_ticket;
    int ticket_discount_pct;
    int ticket_percentage;

    /* File */
    char menu_file_path[256];
    char csv_output_dir[256];

} Config;

/**
 * @brief Carica i parametri di configurazione dal file specificato.
 *
 * @param path Percorso del file di configurazione.
 * @return Struttura Config contenente tutti i parametri caricati.
 */
Config config_load(const char *path);

/**
 * @brief Stampa a video il contenuto della configurazione caricata.
 *
 * @param cfg Puntatore alla struttura Config da stampare.
 */
void config_print(const Config *cfg);

#endif /* CONFIG_H */
