#include "common.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#ifndef N_NEW_USERS
#define N_NEW_USERS 5
#endif

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("[ADD_USERS] Avviato: verifica delle risorse IPC esistenti...\n");

    /* 1. Verifica dell'esistenza delle risorse IPC (es. set semafori) */
    int semid = sem_set_get(0);
    if (semid == -1) {
        fprintf(stderr, "[ADD_USERS] Errore: impossibile accedere alle risorse IPC. La simulazione è attiva?\n");
        exit(EXIT_FAILURE);
    }

    printf("[ADD_USERS] Creazione di %d nuovi processi utente...\n", N_NEW_USERS);

    /* 2. Ciclo di creazione dei nuovi utenti */
    for (int i = 0; i < N_NEW_USERS; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("[ADD_USERS] Errore durante la fork");
            break;
        } else if (pid == 0) {
            /* Processo figlio: esegue l'eseguibile utente */
            char *args[] = { "./bin/utente", NULL };
            char *envp[] = { NULL };
            
            execve("./bin/utente", args, envp);
            
            /* Se execve fallisce */
            perror("[ADD_USERS] Errore durante la execve di ./bin/utente");
            exit(EXIT_FAILURE);
        }

        /* Breve pausa opzionale per evitare un picco istantaneo di creazione */
        usleep(50000);
    }

    printf("[ADD_USERS] Tutti i %d nuovi utenti sono stati avviati con successo.\n", N_NEW_USERS);

    return EXIT_SUCCESS;
}