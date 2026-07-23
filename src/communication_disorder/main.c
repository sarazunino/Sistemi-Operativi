#include "common.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef STOP_DURATION
#define STOP_DURATION 5
#endif

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("[COMMUNICATION_DISORDER] Avviato: tentativo di aggancio alle risorse IPC...\n");

    /* 1. Aggancio al set di semafori esistente (create = 0) */
    int semid = sem_set_get(0);
    if (semid == -1) {
        fprintf(stderr, "[COMMUNICATION_DISORDER] Errore: impossibile accedere al set di semafori.\n");
        exit(EXIT_FAILURE);
    }

    printf("[COMMUNICATION_DISORDER] Blocco della stazione cassa per %d secondi...\n", STOP_DURATION);

    /* 2. Esegue il blocco della cassa acquisendo il mutex/semaforo di stato della cassa */
    sem_wait_idx(semid, SEM_MUTEX_STATO_CASSA);

    /* 3. Mantiene il blocco per la durata stabilita */
    sleep(STOP_DURATION);

    /* 4. Rilascia il blocco ripristinando il normale funzionamento */
    sem_signal_idx(semid, SEM_MUTEX_STATO_CASSA);

    printf("[COMMUNICATION_DISORDER] Disservizio terminato. Stazione cassa sbloccata.\n");

    return EXIT_SUCCESS;
}
