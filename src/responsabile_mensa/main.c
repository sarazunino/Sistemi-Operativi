#include "common.h"
#include "config.h"
#include "ipc_utils.h"
#include "simtime.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>

#define MAX_FIGLI 1024

static pid_t figli_pid[MAX_FIGLI];
static int n_figli = 0;

extern char **environ;

/* ======================================================================
 * Caricamento menu (config/menu.txt, sintassi "TIPO;Nome")
 * ====================================================================== */

/* Il file menu.txt non contiene i prezzi: sono fissati dalla
 * configurazione (PRICE_PRIMI, PRICE_SECONDI, PRICE_DOLCE, PRICE_COFFEE)
 * e sono uguali per tutti i piatti dello stesso tipo (sez. 5.4). Ai
 * contorni non e' associato un prezzo proprio: sono compresi nel prezzo
 * del secondo. Nessun abbinamento fisso secondo->contorno: la lista dei
 * contorni disponibili viene pubblicata in memoria condivisa e sara'
 * l'utente a scegliere quale abbinare, indicandolo nel campo
 * contorno_idx della propria richiesta (vedi common.h). */
static void carica_menu(const char *path, const Config *cfg, StatoStazioni *stato) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "responsabile: impossibile aprire menu '%s'\n", path);
        exit(EXIT_FAILURE);
    }

    StatoStazionePrimi   *primi   = &stato->primi;
    StatoStazioneSecondi *secondi = &stato->secondi;
    StatoStazioneCoffee  *coffee  = &stato->coffee;

    primi->num_piatti_oggi   = 0;
    secondi->num_piatti_oggi = 0;
    secondi->num_contorni    = 0;
    coffee->num_caffe        = 0;
    coffee->num_dolci        = 0;

    char riga[128];
    while (fgets(riga, sizeof(riga), f)) {
        riga[strcspn(riga, "\r\n")] = '\0';
        if (riga[0] == '\0' || riga[0] == '#') continue;

        char tipo_str[16], nome[MAX_NOME_PIATTO];
        if (sscanf(riga, "%15[^;];%31[^;\r\n]", tipo_str, nome) != 2) continue;

        if (strcmp(tipo_str, "PRIMO") == 0) {
            if (primi->num_piatti_oggi >= MAX_PIATTI_PRIMI) continue;
            int i = primi->num_piatti_oggi++;
            strncpy(primi->piatti[i].nome, nome, MAX_NOME_PIATTO - 1);
            primi->piatti[i].nome[MAX_NOME_PIATTO - 1] = '\0';
            primi->piatti[i].prezzo = cfg->price_primi;
        } else if (strcmp(tipo_str, "SECONDO") == 0) {
            if (secondi->num_piatti_oggi >= MAX_PIATTI_SECONDI) continue;
            int i = secondi->num_piatti_oggi++;
            strncpy(secondi->piatti[i].nome, nome, MAX_NOME_PIATTO - 1);
            secondi->piatti[i].nome[MAX_NOME_PIATTO - 1] = '\0';
            secondi->piatti[i].prezzo = cfg->price_secondi;
        } else if (strcmp(tipo_str, "CONTORNO") == 0) {
            if (secondi->num_contorni >= MAX_CONTORNI) continue;
            int i = secondi->num_contorni++;
            strncpy(secondi->contorni[i].nome, nome, MAX_NOME_PIATTO - 1);
            secondi->contorni[i].nome[MAX_NOME_PIATTO - 1] = '\0';
            secondi->contorni[i].prezzo = 0;   /* incluso nel prezzo del secondo */
        } else if (strcmp(tipo_str, "DOLCE") == 0) {
            if (coffee->num_dolci >= MAX_DOLCI) continue;
            int i = coffee->num_dolci++;
            strncpy(coffee->dolci[i].nome, nome, MAX_NOME_PIATTO - 1);
            coffee->dolci[i].nome[MAX_NOME_PIATTO - 1] = '\0';
            coffee->dolci[i].prezzo = cfg->price_dolce;
        } else if (strcmp(tipo_str, "CAFFE") == 0) {
            if (coffee->num_caffe >= NOF_TIPI_CAFFE) continue;
            int i = coffee->num_caffe++;
            strncpy(coffee->caffe[i].nome, nome, MAX_NOME_PIATTO - 1);
            coffee->caffe[i].nome[MAX_NOME_PIATTO - 1] = '\0';
            coffee->caffe[i].prezzo = cfg->price_coffee;
        }
    }
    fclose(f);

    /* sez. 5: almeno 2 primi, 2 secondi con relativi contorni e 4 tipi di caffe' */
    if (primi->num_piatti_oggi < 2 || secondi->num_piatti_oggi < 2) {
        fprintf(stderr, "responsabile: il menu deve contenere almeno 2 primi e 2 secondi\n");
        exit(EXIT_FAILURE);
    }
    if (secondi->num_contorni < 1) {
        fprintf(stderr, "responsabile: il menu deve contenere almeno un contorno\n");
        exit(EXIT_FAILURE);
    }
    if (coffee->num_caffe < NOF_TIPI_CAFFE) {
        fprintf(stderr, "responsabile: il menu deve contenere %d tipi di caffe'\n", NOF_TIPI_CAFFE);
        exit(EXIT_FAILURE);
    }
    if (coffee->num_dolci < 1) {
        fprintf(stderr, "responsabile: il menu deve contenere almeno un dolce\n");
        exit(EXIT_FAILURE);
    }

    printf("[RESPONSABILE] Menu del giorno: %d primi, %d secondi, %d contorni, %d dolci, %d caffe'.\n",
           primi->num_piatti_oggi, secondi->num_piatti_oggi, secondi->num_contorni,
           coffee->num_dolci, coffee->num_caffe);
}

/* ======================================================================
 * Assegnazione operatori alle stazioni (sez. 5.2)
 * ====================================================================== */

/* Almeno un operatore per stazione, poi i restanti vengono assegnati
 * privilegiando le stazioni con il tempo medio di servizio piu' alto.
 * Dopo ogni assegnazione il tempo della stazione scelta viene diviso per
 * il numero di operatori che vi lavorano: si ottiene cosi' il tempo medio
 * "per operatore", che e' il criterio con cui si continua a distribuire i
 * lavoratori rimanenti. */
static void assegna_operatori(const Config *cfg, TipoStazione *assegnazione, int nof_workers) {
    const TipoStazione base[NUM_STAZIONI] = {
        STAZIONE_PRIMI, STAZIONE_SECONDI, STAZIONE_COFFEE, STAZIONE_CASSA
    };
    const int tempi[NUM_STAZIONI] = {
        cfg->avg_srvc_primi, cfg->avg_srvc_main_course,
        cfg->avg_srvc_coffee, cfg->avg_srvc_cassa
    };
    int addetti[NUM_STAZIONI];
    int idx = 0;

    /* il main garantisce nof_workers >= NUM_STAZIONI, quindi ogni stazione
     * riceve sicuramente il proprio operatore (sez. 5.2) */
    for (int i = 0; i < NUM_STAZIONI; i++) {
        assegnazione[idx++] = base[i];
        addetti[i] = 1;
    }

    while (idx < nof_workers) {
        int mx = 0;
        for (int i = 1; i < NUM_STAZIONI; i++) {
            if ((long)tempi[i] * addetti[mx] > (long)tempi[mx] * addetti[i]) mx = i;
        }
        assegnazione[idx++] = base[mx];
        addetti[mx]++;
    }
}

/* Avvia il processo worker della stazione indicata: ./bin/cassiere per la
 * cassa (eseguibile dedicato, sez. 5.4), ./bin/operatore per le altre tre
 * stazioni di distribuzione. L'id univoco serve per il conteggio degli
 * "operatori attivi" nelle statistiche. */
static void avvia_worker(TipoStazione tipo, int id, const char *config_path) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork operatore/cassiere"); exit(EXIT_FAILURE); }

    if (pid == 0) {
        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%d", id);

        if (tipo == STAZIONE_CASSA) {
            char *args[] = { (char *)"./bin/cassiere", (char *)config_path, id_str, NULL };
            execve("./bin/cassiere", args, environ);
            perror("execve cassiere");
            _exit(EXIT_FAILURE);
        } else {
            char tipo_str[16];
            snprintf(tipo_str, sizeof(tipo_str), "%d", (int)tipo);
            char *args[] = { (char *)"./bin/operatore", (char *)config_path, tipo_str, id_str, NULL };
            execve("./bin/operatore", args, environ);
            perror("execve operatore");
            _exit(EXIT_FAILURE);
        }
    }
    figli_pid[n_figli++] = pid;
}

/* ======================================================================
 * Creazione utenti, organizzati in gruppi (sez. 6)
 * ====================================================================== */

static pid_t avvia_utente(const char *config_path, int capogruppo, int num_membri, pid_t capo_pid) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork utente"); exit(EXIT_FAILURE); }

    if (pid == 0) {
        char s_capo[8], s_num[8], s_pid[16];
        snprintf(s_capo, sizeof(s_capo), "%d", capogruppo);
        snprintf(s_num,  sizeof(s_num),  "%d", num_membri);
        snprintf(s_pid,  sizeof(s_pid),  "%d", (int)capo_pid);

        char *args[] = { (char *)"./bin/utente", (char *)config_path,
                         s_capo, s_num, s_pid, NULL };
        execve("./bin/utente", args, environ);
        perror("execve utente");
        _exit(EXIT_FAILURE);
    }
    figli_pid[n_figli++] = pid;
    return pid;
}

/* Forma i gruppi e crea tutti gli NOF_USERS processi utente. La
 * dimensione di ciascun gruppo e' estratta casualmente in
 * 1..MAX_USERS_PER_GROUP e l'associazione utenti<->gruppo e' quindi
 * determinata al momento della creazione dei processi (sez. 6). */
static void crea_utenti_a_gruppi(const Config *cfg, const char *config_path) {
    if (cfg->max_users_per_group > MAX_GRUPPO) {
        fprintf(stderr, "responsabile: MAX_USERS_PER_GROUP (%d) supera il limite statico MAX_GRUPPO (%d)\n",
                cfg->max_users_per_group, MAX_GRUPPO);
        exit(EXIT_FAILURE);
    }

    int rimanenti = cfg->nof_users;
    int gruppi = 0;

    while (rimanenti > 0) {
        int max_size = cfg->max_users_per_group;
        if (max_size < 1) max_size = 1;
        if (max_size > rimanenti) max_size = rimanenti;

        int size = sim_rand_range(1, max_size);

        pid_t capo_pid = avvia_utente(config_path, 1, size, 0);
        for (int i = 1; i < size; i++)
            avvia_utente(config_path, 0, size, capo_pid);

        rimanenti -= size;
        gruppi++;
    }

    printf("[RESPONSABILE] Creati %d utenti organizzati in %d gruppi.\n", cfg->nof_users, gruppi);
}

/* ======================================================================
 * Barriere di sincronizzazione (inizializzazione e fine giornata)
 * ====================================================================== */

static int leggi_n_children(int semid, StatoStazioni *stato) {
    sem_wait_idx(semid, SEM_MUTEX_ATTESA);
    int n = stato->nof_children_totali;
    sem_signal_idx(semid, SEM_MUTEX_ATTESA);
    return n;
}

static void attendi_barriera(int semid, int n) {
    for (int i = 0; i < n; i++) sem_wait_idx(semid, SEM_BARRIERA_READY);
}

static void rilascia_gate(int semid, int n) {
    for (int i = 0; i < n; i++) sem_signal_idx(semid, SEM_GATE_START);
}

/* ======================================================================
 * Rifornimento delle stazioni primi e secondi (sez. 5.2 / sez. 6)
 * ====================================================================== */

/* Le stazioni vengono rifornite ogni AVG_REFILL_TIME secondi simulati
 * +-REFILL_VARIABILITY_PERCENT%, fino al massimo di MAX_PORZIONI_*. */
static void refill_se_necessario(int semid, TipoStazione tipo, StatoStazioni *stato,
                                 int quantita_refill, int minuti_trascorsi,
                                 int *minuti_al_prossimo_refill, const Config *cfg) {
    *minuti_al_prossimo_refill -= minuti_trascorsi;
    if (*minuti_al_prossimo_refill > 0) return;

    int mutex = sem_mutex_stato_di(tipo);
    sem_wait_idx(semid, mutex);

    if (tipo == STAZIONE_PRIMI) {
        StatoStazionePrimi *s = &stato->primi;
        for (int i = 0; i < s->num_piatti_oggi; i++) {
            s->porzioni_disponibili[i] += quantita_refill;
            if (s->porzioni_disponibili[i] > s->max_porzioni)
                s->porzioni_disponibili[i] = s->max_porzioni;
        }
        if (s->num_piatti_oggi > 0) s->esaurito = 0;
    } else {
        StatoStazioneSecondi *s = &stato->secondi;
        for (int i = 0; i < s->num_piatti_oggi; i++) {
            s->porzioni_disponibili[i] += quantita_refill;
            if (s->porzioni_disponibili[i] > s->max_porzioni)
                s->porzioni_disponibili[i] = s->max_porzioni;
        }
        if (s->num_piatti_oggi > 0) s->esaurito = 0;
    }

    sem_signal_idx(semid, mutex);

    int minuti_refill = cfg->avg_refill_time / 60;
    if (minuti_refill < 1) minuti_refill = 1;
    *minuti_al_prossimo_refill = sim_rand_intorno(minuti_refill, cfg->refill_variability_percent);
}

/* Conteggio delle porzioni rimaste invendute a fine giornata (sez. 5.1:
 * "il numero di piatti avanzati totali e per tipo"). */
static void registra_piatti_avanzati(Statistiche *stats, int semid, StatoStazioni *stato) {
    int avanzati_primi = 0, avanzati_secondi = 0;

    for (int i = 0; i < stato->primi.num_piatti_oggi; i++)
        avanzati_primi += stato->primi.porzioni_disponibili[i];
    for (int i = 0; i < stato->secondi.num_piatti_oggi; i++)
        avanzati_secondi += stato->secondi.porzioni_disponibili[i];

    stats_record_piatto_avanzato(stats, semid, TIPO_PRIMO, avanzati_primi);
    stats_record_piatto_avanzato(stats, semid, TIPO_SECONDO, avanzati_secondi);
    /* dolci e caffe' hanno quantita' illimitata (sez. 5.2): non avanzano */
}

/* ======================================================================
 * MAIN
 * ====================================================================== */

int main(int argc, char *argv[]) {
    const char *config_path = (argc > 1) ? argv[1] : CONFIG_PATH_DEFAULT;

    Config cfg = config_load(config_path);
    config_print(&cfg);

    if (cfg.nof_workers < NUM_STAZIONI) {
        fprintf(stderr, "responsabile: NOF_WORKERS (%d) insufficiente: servono almeno %d operatori "
                        "per non lasciare stazioni scoperte\n", cfg.nof_workers, NUM_STAZIONI);
        exit(EXIT_FAILURE);
    }
    if (cfg.nof_workers > NOF_WORKERS_MAX) {
        fprintf(stderr, "responsabile: NOF_WORKERS (%d) supera il limite statico NOF_WORKERS_MAX (%d)\n",
                cfg.nof_workers, NOF_WORKERS_MAX);
        exit(EXIT_FAILURE);
    }
    if (cfg.nof_workers + cfg.nof_users >= MAX_FIGLI) {
        fprintf(stderr, "responsabile: troppi processi richiesti (max %d)\n", MAX_FIGLI);
        exit(EXIT_FAILURE);
    }

    sim_srand();

    /* Il responsabile diventa leader del proprio process group: add_users
     * puo' cosi' agganciare i nuovi utenti allo stesso gruppo e un solo
     * killpg raggiunge tutti i discendenti. */
    setpgid(0, 0);
    signal(SIGUSR1, SIG_IGN);   /* il segnale di fine giornata e' per i figli */

    FILE *pf = fopen("output/responsabile.pid", "w");
    if (pf) { fprintf(pf, "%d\n", (int)getpid()); fclose(pf); }
    else    { perror("responsabile: impossibile scrivere output/responsabile.pid"); }

    ipc_register_cleanup_on_signal();

    int shm_st_id    = shm_stazioni_get(1);
    int shm_stats_id = shm_stats_get(1);
    int semid        = sem_set_get(1);
    int mq_primi     = msgq_primi_get(1);
    int mq_secondi   = msgq_secondi_get(1);
    int mq_coffee    = msgq_coffee_get(1);
    int mq_cassa     = msgq_cassa_get(1);
    int mq_gruppo    = msgq_gruppo_get(1);

    ipc_track_for_cleanup(shm_st_id, shm_stats_id, semid,
                          mq_primi, mq_secondi, mq_coffee, mq_cassa, mq_gruppo);

    StatoStazioni *stato = shm_stazioni_attach(shm_st_id);
    shm_stazioni_init(stato);
    Statistiche *stats = shm_stats_attach(shm_stats_id);
    stats_init_global(stats);

    carica_menu(cfg.menu_file_path, &cfg, stato);

    stato->postazioni_totali[STAZIONE_PRIMI]   = cfg.nof_wk_seats_primi;
    stato->postazioni_totali[STAZIONE_SECONDI] = cfg.nof_wk_seats_secondi;
    stato->postazioni_totali[STAZIONE_COFFEE]  = cfg.nof_wk_seats_coffee;
    stato->postazioni_totali[STAZIONE_CASSA]   = cfg.nof_wk_seats_cassa;

    /* postazioni_libere e' il "gemello" contabile dei semafori
     * SEM_POSTAZIONI_*: gli operatori lo aggiornano a ogni acquisizione e
     * rilascio e lo usano per verificare che la stazione non resti mai
     * sguarnita quando qualcuno chiede una pausa (sez. 5.3). */
    for (int i = 0; i < NUM_STAZIONI; i++)
        stato->postazioni_libere[i] = stato->postazioni_totali[i];

    stato->primi.max_porzioni   = cfg.max_porzioni_primi;
    stato->secondi.max_porzioni = cfg.max_porzioni_secondi;

    IpcSeatCounts seats = {
        .postazioni_primi   = cfg.nof_wk_seats_primi,
        .postazioni_secondi = cfg.nof_wk_seats_secondi,
        .postazioni_coffee  = cfg.nof_wk_seats_coffee,
        .postazioni_cassa   = cfg.nof_wk_seats_cassa,
        .lettori_ticket     = cfg.nof_ticket_readers,
        .tavoli             = cfg.nof_table_seats
    };
    sem_set_init(semid, &seats);

    /* Assegnazione e avvio di operatori e cassieri (sez. 5.1 / 5.2) */
    TipoStazione assegnazione[NOF_WORKERS_MAX];
    assegna_operatori(&cfg, assegnazione, cfg.nof_workers);
    for (int i = 0; i < cfg.nof_workers; i++) {
        avvia_worker(assegnazione[i], i, config_path);
        stato->operatori_assegnati[assegnazione[i]]++;
    }
    printf("[RESPONSABILE] Operatori per stazione: primi=%d secondi=%d coffee=%d cassa=%d\n",
           stato->operatori_assegnati[STAZIONE_PRIMI], stato->operatori_assegnati[STAZIONE_SECONDI],
           stato->operatori_assegnati[STAZIONE_COFFEE], stato->operatori_assegnati[STAZIONE_CASSA]);

    crea_utenti_a_gruppi(&cfg, config_path);

    stato->nof_children_totali = n_figli;

    /* sez. 5.1: la simulazione comincia solo quando tutti i processi sono
     * stati creati e hanno terminato la fase di inizializzazione */
    printf("[RESPONSABILE] Attendo l'inizializzazione di %d processi...\n", n_figli);
    attendi_barriera(semid, n_figli);
    printf("[RESPONSABILE] Simulazione avviata (%d giorni da %d minuti simulati).\n",
           cfg.sim_duration, cfg.day_duration);

    char csv_path[512];
    snprintf(csv_path, sizeof(csv_path), "%sstatistiche_simulazione.csv", cfg.csv_output_dir);

    int causa_overload = 0;

    for (int giorno = 1; giorno <= cfg.sim_duration; giorno++) {
        printf("\n=================== INIZIO GIORNO %d ===================\n", giorno);

        stats_init_daily(stats);
        stato->utenti_in_attesa = 0;
        stato->utenti_in_coda_fine_giornata = 0;

        /* le stazioni primi e secondi vengono inizializzate all'inizio di
         * ogni giornata con AVG_REFILL_* porzioni per tipo di piatto */
        for (int i = 0; i < stato->primi.num_piatti_oggi; i++)
            stato->primi.porzioni_disponibili[i] = cfg.avg_refill_primi;
        for (int i = 0; i < stato->secondi.num_piatti_oggi; i++)
            stato->secondi.porzioni_disponibili[i] = cfg.avg_refill_secondi;
        stato->primi.esaurito   = 0;
        stato->secondi.esaurito = 0;

        int minuti_refill_base = cfg.avg_refill_time / 60;
        if (minuti_refill_base < 1) minuti_refill_base = 1;
        int minuti_refill_primi   = sim_rand_intorno(minuti_refill_base, cfg.refill_variability_percent);
        int minuti_refill_secondi = sim_rand_intorno(minuti_refill_base, cfg.refill_variability_percent);

        int n_oggi = leggi_n_children(semid, stato);
        rilascia_gate(semid, n_oggi);

        /* scorrere della giornata: il tempo avanza a blocchi di 10 minuti
         * simulati, al termine dei quali si valuta il rifornimento */
        int minuti = 0;
        while (minuti < cfg.day_duration) {
            int intervallo = 10;
            if (minuti + intervallo > cfg.day_duration) intervallo = cfg.day_duration - minuti;

            long ns = sim_minuti_in_ns(cfg.n_nano_secs, intervallo);
            struct timespec ts;
            ts.tv_sec  = ns / 1000000000L;
            ts.tv_nsec = ns % 1000000000L;
            nanosleep(&ts, NULL);
            minuti += intervallo;

            refill_se_necessario(semid, STAZIONE_PRIMI, stato, cfg.avg_refill_primi,
                                 intervallo, &minuti_refill_primi, &cfg);
            refill_se_necessario(semid, STAZIONE_SECONDI, stato, cfg.avg_refill_secondi,
                                 intervallo, &minuti_refill_secondi, &cfg);
        }

        /* chiusura della mensa: tutti i figli vengono avvisati e si attende
         * che ciascuno di essi raggiunga la barriera di fine giornata */
        int n_fine = leggi_n_children(semid, stato);
        killpg(0, SIGUSR1);
        attendi_barriera(semid, n_fine);

        /* da qui nessun figlio sta lavorando: si possono leggere lo stato
         * delle stazioni e ripulire le code senza interferenze */
        registra_piatti_avanzati(stats, semid, stato);
        msgq_svuota(mq_primi);
        msgq_svuota(mq_secondi);
        msgq_svuota(mq_coffee);
        msgq_svuota(mq_cassa);
        msgq_svuota(mq_gruppo);

        stats_print_daily(stats);
        stats_print_total(stats);
        stats_export_to_csv(stats, csv_path, giorno);

        printf("=================== FINE GIORNO %d === (utenti ancora in coda alla chiusura: %d)\n",
               giorno, stato->utenti_in_coda_fine_giornata);

        if (stato->utenti_in_coda_fine_giornata > cfg.overload_threshold) {
            printf("[RESPONSABILE] OVERLOAD: %d utenti in coda oltre la soglia di %d.\n",
                   stato->utenti_in_coda_fine_giornata, cfg.overload_threshold);
            causa_overload = 1;
            break;
        }
    }

    stats->motivo_terminazione = causa_overload ? TERMINAZIONE_OVERLOAD : TERMINAZIONE_TIMEOUT;
    printf("\n[RESPONSABILE] Terminazione della simulazione per: %s\n",
           causa_overload ? "OVERLOAD" : "TIMEOUT");

    /* Fine simulazione: si avvisano i figli e si sbloccano quelli fermi al
     * cancello di inizio giornata. */
    signal(SIGTERM, SIG_IGN);   /* non autoterminarsi con il prossimo killpg */
    int n_finali = leggi_n_children(semid, stato);
    killpg(0, SIGTERM);
    rilascia_gate(semid, n_finali);

    for (int i = 0; i < n_figli; i++) {
        while (waitpid(figli_pid[i], NULL, 0) == -1 && errno == EINTR)
            ;
    }

    printf("\n########## STATISTICHE FINALI DELLA SIMULAZIONE ##########\n");
    stats_print_total(stats);
    printf("CSV con le statistiche giornaliere: %s\n", csv_path);

    shm_stazioni_detach(stato);
    shm_stats_detach(stats);
    ipc_cleanup_all(shm_st_id, shm_stats_id, semid, mq_primi, mq_secondi, mq_coffee, mq_cassa, mq_gruppo);

    printf("[RESPONSABILE] Risorse IPC deallocate.\n");
    return EXIT_SUCCESS;
}
