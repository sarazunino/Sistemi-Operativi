#ifndef CONFIG_H
#define CONFIG_H

typedef struct {

    int nof_workers;
    int nof_users;
    int nof_table_seats;
    int avg_consumption_time;  

    int nof_wk_seats_primi;
    int nof_wk_seats_secondi;
    int nof_wk_seats_coffee;
    int nof_wk_seats_cassa;
    int avg_srvc_primi;
    int avg_srvc_main_course;
    int avg_srvc_coffee;
    int avg_srvc_cassa;

    int avg_refill_primi;
    int avg_refill_secondi;
    int max_porzioni_primi;
    int max_porzioni_secondi;
    int avg_refill_time;
    int refill_variability_percent;

    int prob_vado_in_mensa;
    int prob_primo;
    int prob_secondo_contorno;
    int prob_dolce;
    int prob_caffe;

    int price_primi;
    int price_secondi;
    int price_dolce;
    int price_coffee;

    int nof_pause;
    int pause_probability;
    int min_pause_time;
    int max_pause_time;

    int sim_duration;
    int day_duration;          
    long n_nano_secs;
    int overload_threshold;

    int max_users_per_group;

    int stop_duration;
    int n_new_users;

    int nof_ticket_readers;
    int avg_srvc_ticket;
    int ticket_discount_pct;
    int ticket_percentage;

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
