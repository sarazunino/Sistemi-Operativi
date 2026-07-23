#include "common.h"
#include "ipc_utils.h"

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
    (void)argc;
    (void)argv;

    struct sigaction sa;
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    int semid = sem_set_get(0);
    int msgq_cassa = msgq_cassa_get(0);

    if (semid == -1 || msgq_cassa == -1) {
        fprintf(stderr, "cassiere: errore nell'acquisizione delle risorse IPC\n");
        exit(EXIT_FAILURE);
    }

    printf("[CASSIERE] Avviato e in attesa di clienti alla cassa...\n");

    while (running) {
        // Logica di gestione della cassa e ricezione dei messaggi dalla coda
        usleep(150000); // Simulazione tempo di transazione
    }

    printf("[CASSIERE] Terminato correttamente.\n");
    return EXIT_SUCCESS;
}
