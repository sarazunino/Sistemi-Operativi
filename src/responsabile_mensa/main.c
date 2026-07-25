#include "common.h"
#include "config.h"
#include "ipc_utils.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>

#define MAX_FIGLI 512

static pid_t figli_pid[MAX_FIGLI];
static int n_figli = 0;

extern char **environ;

static void carica_menu(const char *path, StatoStazioni *stato) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "responsabile: impossibile aprire menu '%s'\n", path); exit(EXIT_FAILURE); }

    stato->primi.num_piatti_oggi = 0;
    stato->secondi.num_piatti_oggi = 0;

    char riga[128];
    while (fgets(riga, sizeof(riga), f)) {
        riga[strcspn(riga, "\n")] = '\0';
        if (riga[0] == '\0' || riga[0] == '#') continue;

        char tipo_str[16], nome[MAX_NOME_PIATTO];
        int prezzo;
        if (sscanf(riga, "%15[^;];%31[^;];%d", tipo_str, nome, &prezzo) != 3) continue;

        if (strcmp(tipo_str, "PRIMO") == 0 && stato->primi.num_piatti_oggi < MAX_PIATTI_PRIMI) {
            int i = stato->primi.num_piatti_oggi++;
            strncpy(stato->primi.piatti[i].nome, nome, MAX_NOME_PIATTO - 1);
            stato->primi.piatti[i].prezzo = prezzo;
        } else if (strcmp(tipo_str, "SECONDO") == 0 && stato->secondi.num_piatti_oggi < MAX_PIATTI_SECONDI) {
            int i = stato->secondi.num_piatti_oggi++;
            strncpy(stato->secondi.piatti[i].nome, nome, MAX_NOME_PIATTO - 1);
            stato->secondi.piatti[i].prezzo = prezzo;
        }
    }
    fclose(f);

    if (stato->primi.num_piatti_oggi < 2 || stato->secondi.num_piatti_oggi < 2) {
        fprintf(stderr, "responsabile: il menu deve avere almeno 2 primi e 2 secondi\n");
        exit(EXIT_FAILURE);
    }
}

/* almeno un operatore per stazione, poi assegna i restanti privilegiando
 * le stazioni col tempo medio di servizio più alto (sez. 5.2) */
static void assegna_operatori(const Config *cfg, TipoStazione *assegnazione, int nof_workers) {
    TipoStazione base[4] = { STAZIONE_PRIMI, STAZIONE_SECONDI, STAZIONE_COFFEE, STAZIONE_CASSA };
    int tempi[4] = { cfg->avg_srvc_primi, cfg->avg_srvc_main_course, cfg->avg_srvc_coffee, cfg->avg_srvc_cassa };

    int idx = 0;
    for (int i = 0; i < 4 && idx < nof_workers; i++) assegnazione[idx++] = base[i];

    while (idx < nof_workers) {
        int mx = 0;
        for (int i = 1; i < 4; i++) if (tempi[i] > tempi[mx]) mx = i;
        assegnazione[idx++] = base[mx];
        tempi[mx] /= 2;
    }
}

static void avvia_operatore(TipoStazione tipo) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork operatore"); exit(EXIT_FAILURE); }
    if (pid == 0) {
        char tipo_str[4];
        snprintf(tipo_str, sizeof(tipo_str), "%d", (int)tipo);
        char *args[] = { (char *)"./bin/operatore", tipo_str, NULL };
        execve("./bin/operatore", args, environ);
        perror("execve operatore"); _exit(EXIT_FAILURE);
    }
    figli_pid[n_figli++] = pid;
}

/* NUOVO: avvia un membro di gruppo (non capogruppo). capogruppo=0 sempre
 * qui; capogruppo_pid = pid reale del capogruppo gia' forkato. */
static void avvia_utente(int capogruppo, int num_membri, pid_t capogruppo_pid) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork utente"); exit(EXIT_FAILURE); }
    if (pid == 0) {
        char s_capo[4], s_num[4], s_pid[16];
        snprintf(s_capo, sizeof(s_capo), "%d", capogruppo);
        snprintf(s_num,  sizeof(s_num),  "%d", num_membri);
        snprintf(s_pid,  sizeof(s_pid),  "%d", (int)capogruppo_pid);
        char *args[] = { (char *)"./bin/utente", s_capo, s_num, s_pid, NULL };
        execve("./bin/utente", args, environ);
        perror("execve utente"); _exit(EXIT_FAILURE);
    }
    figli_pid[n_figli++] = pid;
}

/* NUOVO: fork del capogruppo di un nuovo gruppo. Ritorna il pid reale,
 * necessario per farlo conoscere ai membri (mailbox della coda gruppo). */
static pid_t avvia_capogruppo(int num_membri) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork capogruppo"); exit(EXIT_FAILURE); }
    if (pid == 0) {
        char s_capo[4], s_num[4], s_pid[16];
        snprintf(s_capo, sizeof(s_capo), "1");
        snprintf(s_num,  sizeof(s_num),  "%d", num_membri);
        snprintf(s_pid,  sizeof(s_pid),  "0");
        char *args[] = { (char *)"./bin/utente", s_capo, s_num, s_pid, NULL };
        execve("./bin/utente", args, environ);
        perror("execve utente capogruppo"); _exit(EXIT_FAILURE);
    }
    figli_pid[n_figli++] = pid;
    return pid;
}

/* NUOVO: forma i gruppi (sez. 6) e crea tutti gli NOF_USERS processi
 * utente. Dimensione gruppo casuale 1..MAX_USERS_PER_GROUP. */
static void crea_utenti_a_gruppi(const Config *cfg) {
    if (cfg->max_users_per_group > MAX_GRUPPO) {
        fprintf(stderr, "responsabile: MAX_USERS_PER_GROUP (%d) supera il cap statico MAX_GRUPPO (%d)\n",
                cfg->max_users_per_group, MAX_GRUPPO);
        exit(EXIT_FAILURE);
    }

    int rimanenti = cfg->nof_users;
    while (rimanenti > 0) {
        int max_size = cfg->max_users_per_group;
        if (max_size < 1) max_size = 1;
        if (max_size > rimanenti) max_size = rimanenti;
        int size = 1 + rand() % max_size; /* 1..max_size */

        pid_t capo_pid = avvia_capogruppo(size);
        for (int i = 1; i < size; i++) {
            avvia_utente(0, size, capo_pid);
        }

        rimanenti -= size;
    }
}

static int leggi_n_children(int semid, StatoStazioni *stato) {
    int n;
    sem_wait_idx(semid, SEM_MUTEX_ATTESA);
    n = stato->nof_children_totali;
    sem_signal_idx(semid, SEM_MUTEX_ATTESA);
    return n;
}

static void attendi_barriera(int semid, int n) { for (int i = 0; i < n; i++) sem_wait_idx(semid, SEM_BARRIERA_READY); }
static void rilascia_gate(int semid, int n)    { for (int i = 0; i < n; i++) sem_signal_idx(semid, SEM_GATE_START); }

int main(int argc, char *argv[]) {
    const char *config_path = (argc > 1) ? argv[1] : CONFIG_PATH_DEFAULT;
    const char *menu_path   = (argc > 2) ? argv[2] : MENU_PATH_DEFAULT;

    Config cfg = config_load(config_path);
    config_print(&cfg);

    if (cfg.nof_workers + cfg.nof_users >= MAX_FIGLI) {
        fprintf(stderr, "responsabile: troppi processi richiesti (max %d)\n", MAX_FIGLI);
        exit(EXIT_FAILURE);
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid()); /* NUOVO: serve prima della formazione gruppi */

    /* rende il responsabile leader del proprio process group, cosi'
     * add_users puo' aggregare i nuovi utenti allo stesso gruppo e
     * a fine simulazione un solo killpg raggiunge tutti i discendenti */
    setpgid(0, 0);
    FILE *pf = fopen("output/responsabile.pid", "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    ipc_register_cleanup_on_signal();

    int shm_st_id    = shm_stazioni_get(1);
    int shm_stats_id = shm_stats_get(1);
    int semid        = sem_set_get(1);
    int mq_primi     = msgq_primi_get(1);
    int mq_secondi   = msgq_secondi_get(1);
    int mq_coffee    = msgq_coffee_get(1);
    int mq_cassa     = msgq_cassa_get(1);
    int mq_gruppo    = msgq_gruppo_get(1);   /* NUOVO */

    ipc_track_for_cleanup(shm_st_id, shm_stats_id, semid, mq_primi, mq_secondi, mq_coffee, mq_cassa, mq_gruppo); /* NUOVO parametro */

    StatoStazioni *stato = shm_stazioni_attach(shm_st_id);
    shm_stazioni_init(stato);
    Statistiche *stats = shm_stats_attach(shm_stats_id);
    stats_init_global(stats);

    carica_menu(menu_path, stato);

    stato->postazioni_totali[STAZIONE_PRIMI]   = cfg.nof_wk_seats_primi;
    stato->postazioni_totali[STAZIONE_SECONDI] = cfg.nof_wk_seats_secondi;
    stato->postazioni_totali[STAZIONE_COFFEE]  = cfg.nof_wk_seats_coffee;
    stato->postazioni_totali[STAZIONE_CASSA]   = cfg.nof_wk_seats_cassa;
    stato->primi.max_porzioni   = cfg.max_porzioni_primi;
    stato->secondi.max_porzioni = cfg.max_porzioni_secondi;

    IpcSeatCounts seats = {
        .postazioni_primi = cfg.nof_wk_seats_primi, .postazioni_secondi = cfg.nof_wk_seats_secondi,
        .postazioni_coffee = cfg.nof_wk_seats_coffee, .postazioni_cassa = cfg.nof_wk_seats_cassa,
        .lettori_ticket = cfg.nof_ticket_readers,   /* NUOVO */
        .tavoli = cfg.nof_table_seats
    };
    sem_set_init(semid, &seats);

    TipoStazione assegnazione[MAX_FIGLI];
    assegna_operatori(&cfg, assegnazione, cfg.nof_workers);
    for (int i = 0; i < cfg.nof_workers; i++) {
        avvia_operatore(assegnazione[i]);
        stato->operatori_assegnati[assegnazione[i]]++;
    }

    crea_utenti_a_gruppi(&cfg); /* NUOVO: sostituisce il vecchio for su avvia_utente() */

    stato->nof_children_totali = n_figli;

    printf("[RESPONSABILE] Attendo inizializzazione di %d processi...\n", n_figli);
    attendi_barriera(semid, n_figli);
    printf("[RESPONSABILE] Simulazione avviata (%d giorni).\n", cfg.sim_duration);

    int causa_overload = 0;
    int giorno;
    for (giorno = 1; giorno <= cfg.sim_duration; giorno++) {
        printf("\n--- INIZIO GIORNO %d ---\n", giorno);
        stats_init_daily(stats);
        stato->utenti_in_attesa = 0;

        for (int i = 0; i < stato->primi.num_piatti_oggi; i++)   stato->primi.porzioni_disponibili[i]   = cfg.avg_refill_primi;
        for (int i = 0; i < stato->secondi.num_piatti_oggi; i++) stato->secondi.porzioni_disponibili[i] = cfg.avg_refill_secondi;
        stato->primi.esaurito = 0;
        stato->secondi.esaurito = 0;

        int n_oggi = leggi_n_children(semid, stato);
        rilascia_gate(semid, n_oggi);

        int minuti = 0;
        while (minuti < MINUTI_GIORNO) {
            int intervallo = 10;
            if (minuti + intervallo > MINUTI_GIORNO) intervallo = MINUTI_GIORNO - minuti;

            struct timespec ts;
            long ns = (long)intervallo * cfg.n_nano_secs;
            ts.tv_sec = ns / 1000000000L;
            ts.tv_nsec = ns % 1000000000L;
            nanosleep(&ts, NULL);
            minuti += intervallo;

            sem_wait_idx(semid, sem_mutex_stato_di(STAZIONE_PRIMI));
            for (int i = 0; i < stato->primi.num_piatti_oggi; i++)
                if (stato->primi.porzioni_disponibili[i] < stato->primi.max_porzioni) stato->primi.porzioni_disponibili[i]++;
            sem_signal_idx(semid, sem_mutex_stato_di(STAZIONE_PRIMI));

            sem_wait_idx(semid, sem_mutex_stato_di(STAZIONE_SECONDI));
            for (int i = 0; i < stato->secondi.num_piatti_oggi; i++)
                if (stato->secondi.porzioni_disponibili[i] < stato->secondi.max_porzioni) stato->secondi.porzioni_disponibili[i]++;
            sem_signal_idx(semid, sem_mutex_stato_di(STAZIONE_SECONDI));
        }

        int n_fine = leggi_n_children(semid, stato);
        killpg(0, SIGUSR1);
        attendi_barriera(semid, n_fine);

        char csv_path[128];
        snprintf(csv_path, sizeof(csv_path), "output/stats_giorno_%d.csv", giorno);
        stats_export_to_csv(stats, csv_path);
        stats_print_daily(stats);
        printf("--- FINE GIORNO %d --- (utenti ancora in coda: %d)\n", giorno, stato->utenti_in_attesa);

        if (stato->utenti_in_attesa > cfg.overload_threshold) {
            printf("[RESPONSABILE] OVERLOAD (%d > %d). Termino la simulazione.\n",
                   stato->utenti_in_attesa, cfg.overload_threshold);
            causa_overload = 1;
            break;
        }
    }

    printf("\n[RESPONSABILE] Terminazione per: %s\n", causa_overload ? "OVERLOAD" : "TIMEOUT");

    signal(SIGTERM, SIG_IGN);        /* non autoterminarsi con il prossimo killpg */
    int n_finali = leggi_n_children(semid, stato);
    killpg(0, SIGTERM);
    rilascia_gate(semid, n_finali);   /* sblocca chi era fermo su GATE_START/pause() */

    for (int i = 0; i < n_figli; i++) waitpid(figli_pid[i], NULL, 0);

    stats_print_total(stats);
    stats_export_to_csv(stats, "output/stats_totali.csv");

    shm_stazioni_detach(stato);
    shm_stats_detach(stats);
    ipc_cleanup_all(shm_st_id, shm_stats_id, semid, mq_primi, mq_secondi, mq_coffee, mq_cassa, mq_gruppo); /* NUOVO parametro */

    printf("[RESPONSABILE] Risorse IPC deallocate.\n");
    return EXIT_SUCCESS;
}