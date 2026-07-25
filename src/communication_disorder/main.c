#include "common.h"
#include "config.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Interrompe per STOP_DURATION secondi il funzionamento della cassa,
 * azzerando SEM_CASSA_ATTIVA: il/i cassiere/i sono gia' predisposti
 * (vedi cassiere.c) a bloccarsi automaticamente su questo semaforo
 * senza attesa attiva, e a riprendere non appena viene ripristinato. */
int main(int argc, char *argv[]) {
    Config cfg = config_load(CONFIG_PATH_DEFAULT);

    int durata = cfg.stop_duration;
    if (argc > 1) {
        int override = atoi(argv[1]);
        if (override > 0) durata = override;
    }
    if (durata <= 0) {
        fprintf(stderr, "communication_disorder: STOP_DURATION non valido (%d)\n", durata);
        return EXIT_FAILURE;
    }

    int semid = sem_set_get(0);
    if (semid == -1) {
        fprintf(stderr, "communication_disorder: risorse IPC non trovate, la simulazione e' avviata?\n");
        return EXIT_FAILURE;
    }

    printf("[COMMUNICATION_DISORDER] Guasto ai pagamenti automatici: cassa bloccata per %d secondi.\n", durata);

    sem_wait_idx(semid, SEM_CASSA_ATTIVA);
    sleep((unsigned int)durata);
    sem_signal_idx(semid, SEM_CASSA_ATTIVA);

    printf("[COMMUNICATION_DISORDER] Guasto risolto: cassa nuovamente operativa.\n");
    return EXIT_SUCCESS;
}