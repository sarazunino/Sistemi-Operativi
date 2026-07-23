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
    if (semid == -1) {
        fprintf(stderr, "utente: errore nell'acquisizione delle risorse IPC\n");
        exit(EXIT_FAILURE);
    }

    printf("[UTENTE] Entrato in mensa e pronto a ordinare...\n");

    // Simulazione del percorso dell'utente (scelta stazioni, coda, cassa)
    while (running) {
        // Interazione con le stazioni e la cassa
        break; // L'utente compie il suo ciclo e termina
    }

    printf("[UTENTE] Ha terminato il pasto ed è uscito.\n");
    return EXIT_SUCCESS;
}
