#include "common.h"
#include "config.h"
#include "ipc_utils.h"
#include "simtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ======================================================================
 * Eseguibile della versione completa (sez. 6): invocato da linea di
 * comando interrompe per STOP_DURATION secondi il funzionamento dei
 * pagamenti automatici, bloccando la stazione cassa.
 *
 *   ./bin/communication_disorder [durata_secondi] [config_path]
 *
 * Il blocco e' realizzato azzerando il semaforo SEM_CASSA_ATTIVA: i
 * cassieri eseguono una wait/signal su questo semaforo prima di ogni
 * incasso, quindi restano sospesi in attesa passiva (nessuna attesa
 * attiva) fino al ripristino. Gli utenti nel frattempo si accodano
 * normalmente alla cassa.
 *
 * La durata e' espressa in secondi di tempo simulato, come tutti gli
 * altri tempi del progetto (vedi simtime.h).
 * ====================================================================== */

int main(int argc, char *argv[]) {
    const char *config_path = (argc > 2) ? argv[2] : CONFIG_PATH_DEFAULT;

    Config cfg = config_load(config_path);

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

    printf("[COMMUNICATION_DISORDER] Guasto ai pagamenti automatici: "
           "cassa bloccata per %d secondi simulati.\n", durata);
    fflush(stdout);

    sem_wait_idx(semid, SEM_CASSA_ATTIVA);
    sim_sleep_sec(cfg.n_nano_secs, durata);
    sem_signal_idx(semid, SEM_CASSA_ATTIVA);

    printf("[COMMUNICATION_DISORDER] Guasto risolto: cassa nuovamente operativa.\n");
    return EXIT_SUCCESS;
}
