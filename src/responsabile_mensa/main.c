#include "common.h"
#include "config.h"
#include "ipc_utils.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    const char *config_path = (argc > 1) ? argv[1] : "config/config.conf";

    /* 1. Caricamento della configurazione */
    Config cfg = config_load(config_path);

    printf("[RESPONSABILE] Configurazione caricata con successo da '%s'.\n", config_path);

    /* 2. Registrazione signal handler per la pulizia sicura */
    ipc_register_cleanup_on_signal();

    /* 3. Creazione delle risorse IPC */
    int shm_stazioni_id = shm_stazioni_get(1);
    int shm_stats_id    = shm_stats_get(1);
    int semid           = sem_set_get(1);

    int msgq_primi      = msgq_primi_get(1);
    int msgq_secondi    = msgq_secondi_get(1);
    int msgq_coffee     = msgq_coffee_get(1);
    int msgq_cassa      = msgq_cassa_get(1);

    /* Tracciamo le risorse per il cleanup automatico in caso di crash/interruzione */
    ipc_track_for_cleanup(shm_stazioni_id, shm_stats_id, semid,
                          msgq_primi, msgq_secondi, msgq_coffee, msgq_cassa);

    /* 4. Attach e inizializzazione delle memorie condivise */
    StatoStazioni *stato = shm_stazioni_attach(shm_stazioni_id);
    shm_stazioni_init(stato);

    Statistiche *stats = shm_stats_attach(shm_stats_id);
    stats_init_global(stats);

    /* Inizializzazione dei semafori con i conteggi presi da config */
    IpcSeatCounts seats = {
        .postazioni_primi   = cfg.nof_wk_seats_primi,
        .postazioni_secondi = cfg.nof_wk_seats_secondi,
        .postazioni_coffee  = cfg.nof_wk_seats_coffee,
        .postazioni_cassa   = cfg.nof_wk_seats_cassa,
        .tavoli             = cfg.nof_table_seats
    };
    sem_set_init(semid, &seats);

    printf("[RESPONSABILE] Risorse IPC create e inizializzate correttamente.\n");

    /* 5. Avvio dei processi figli (Stazioni, Cassa, Utenti) */
    /* Nota: Qui verranno inserite le fork/exec per i vari moduli della mensa. */
    printf("[RESPONSABILE] Avvio della simulazione per %d giorni...\n", cfg.sim_duration);

    /* Simulazione dei giorni */
    for (int giorno = 1; giorno <= cfg.sim_duration; giorno++) {
        printf("\n--- INIZIO GIORNO %d ---\n", giorno);
        
        stats_init_daily(stats);

        /* Simulazione temporanea della giornata (verrà sostituita dalla sincronizzazione reale dei processi) */
        sleep(2); 

        /* Esportazione report giornaliero in CSV */
        char csv_path[128];
        snprintf(csv_path, sizeof(csv_path), "output/stats_giorno_%d.csv", giorno);
        stats_export_to_csv(stats, csv_path);
        stats_print_daily(stats);

        printf("--- FINE GIORNO %d ---\n", giorno);
    }

    /* 6. Statistiche finali e export totale */
    stats_print_total(stats);
    stats_export_to_csv(stats, "output/stats_totali.csv");
    printf("[RESPONSABILE] Simulazione completata. Report salvati nella cartella 'output/'.\n");

    /* 7. Detach e Deallocazione pulita delle risorse IPC */
    shm_stazioni_detach(stato);
    shm_stats_detach(stats);

    ipc_cleanup_all(shm_stazioni_id, shm_stats_id, semid,
                    msgq_primi, msgq_secondi, msgq_coffee, msgq_cassa);

    return EXIT_SUCCESS;
}
