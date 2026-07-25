#include "common.h"
#include "config.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

extern char **environ;

/* Legge il pid del responsabile_mensa dal file scritto all'avvio,
 * necessario per mettere i nuovi utenti nel suo stesso process group
 * (cosi' il killpg finale del responsabile li raggiunge). */
static pid_t leggi_responsabile_pid(void) {
    FILE *f = fopen("output/responsabile.pid", "r");
    if (!f) {
        fprintf(stderr, "add_users: impossibile leggere output/responsabile.pid "
                        "(la simulazione e' in esecuzione?)\n");
        exit(EXIT_FAILURE);
    }
    pid_t pid;
    if (fscanf(f, "%d", &pid) != 1) {
        fprintf(stderr, "add_users: file responsabile.pid corrotto\n");
        fclose(f);
        exit(EXIT_FAILURE);
    }
    fclose(f);
    return pid;
}

/* Crea un nuovo utente "singolo" (capogruppo di se stesso), agganciato
 * alle risorse IPC gia' esistenti. Riusa lo stesso binario ./bin/utente
 * usato dal responsabile: il suo primo signal su SEM_BARRIERA_READY
 * funge automaticamente da "pass" per l'eventuale barriera di fine
 * giornata in corso; dal giorno successivo si allinea al normale ciclo
 * SEM_GATE_START. Nessuna modifica a utente.c necessaria. */
static pid_t avvia_nuovo_utente(pid_t pgid_responsabile) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("add_users: fork");
        return -1;
    }
    if (pid == 0) {
        /* stesso process group del responsabile, cosi' il suo killpg
         * finale (e i SIGUSR1 di fine giornata) raggiungono anche noi */
        if (setpgid(0, pgid_responsabile) == -1) {
            perror("add_users: setpgid");
            /* non fatale: proseguiamo comunque */
        }
        char *args[] = { (char *)"./bin/utente", (char *)"1", (char *)"1", (char *)"0", NULL };
        execve("./bin/utente", args, environ);
        perror("add_users: execve utente");
        _exit(EXIT_FAILURE);
    }
    return pid;
}

int main(int argc, char *argv[]) {
    Config cfg = config_load(CONFIG_PATH_DEFAULT);

    int n_new = cfg.n_new_users;
    if (argc > 1) {
        int override = atoi(argv[1]);
        if (override > 0) n_new = override;
    }
    if (n_new <= 0) {
        fprintf(stderr, "add_users: nessun nuovo utente da creare (N_NEW_USERS=%d)\n", n_new);
        return EXIT_SUCCESS;
    }

    pid_t pgid_responsabile = leggi_responsabile_pid();

    int semid = sem_set_get(0);
    int shm_st_id = shm_stazioni_get(0);
    StatoStazioni *stato = shm_stazioni_attach(shm_st_id);

    if (semid == -1 || shm_st_id == -1) {
        fprintf(stderr, "add_users: risorse IPC non trovate, la simulazione e' avviata?\n");
        return EXIT_FAILURE;
    }

    int creati = 0;
    for (int i = 0; i < n_new; i++) {
        pid_t pid = avvia_nuovo_utente(pgid_responsabile);
        if (pid > 0) creati++;
    }

    if (creati > 0) {
        sem_wait_idx(semid, SEM_MUTEX_ATTESA);
        stato->nof_children_totali += creati;
        sem_signal_idx(semid, SEM_MUTEX_ATTESA);
    }

    printf("[ADD_USERS] Creati %d nuovi processi utente (richiesti: %d).\n", creati, n_new);

    shm_stazioni_detach(stato);
    return EXIT_SUCCESS;
}