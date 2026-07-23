#include "common.h"
#include "ipc_utils.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

static int running = 1;

static void handle_sigterm(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tipo_stazione>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    TipoStazione tipo = (TipoStazione)atoi(argv[1]);

    struct sigaction sa;
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    int semid = sem_set_get(0);
    int msgid = -1;

    switch (tipo) {
        case STAZIONE_PRIMI:   msgid = msgq_primi_get(0); break;
        case STAZIONE_SECONDI: msgid = msgq_secondi_get(0); break;
        case STAZIONE_COFFEE:  msgid = msgq_coffee_get(0); break;
        case STAZIONE_CASSA:   msgid = msgq_cassa_get(0); break;
        default:
            fprintf(stderr, "Tipo stazione non valido\n");
            exit(EXIT_FAILURE);
    }

    int sem_postazioni = sem_postazioni_di(tipo);
    int sem_mutex_stato = sem_mutex_stato_di(tipo);

    while (running) {
        sem_wait_idx(semid, sem_postazioni);

        sem_wait_idx(semid, sem_mutex_stato);
        // Operazione di servizio dell'utente sulla stazione
        sem_signal_idx(semid, sem_mutex_stato);

        sem_signal_idx(semid, sem_postazioni);
        
        usleep(100000); // Simulazione tempo di servizio
    }

    return EXIT_SUCCESS;
}