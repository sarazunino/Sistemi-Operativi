#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int nof_workers;
    int nof_users;
    int nof_table_seats;

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

    int price_primi;
    int price_secondi;
    int price_coffee;

    int nof_pause;
    int sim_duration;
    long n_nano_secs;
    int overload_threshold;

    int max_users_per_group;
    int stop_duration;
    int n_new_users;

    int nof_ticket_readers;
    int avg_srvc_ticket;
    int ticket_discount_pct;
} Config;

Config config_load(const char *path);
void config_print(const Config *cfg);

#endif /* CONFIG_H */