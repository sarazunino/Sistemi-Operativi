#include "common.h"
#include "config.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

/* ======================================================================
 * Eseguibile della versione completa (sez. 6): invocato da linea di
 * comando aggiunge alla simulazione gia' avviata N_NEW_USERS processi
 * utente oltre a quelli creati inizialmente dal responsabile mensa.
 *
 *   ./bin/add_users [n_utenti] [config_path]
 *
 * I nuovi utenti sono creati come gruppi di un solo membro e vengono
 * inseriti nel process group del responsabile: cosi' ricevono i segnali
 * di fine giornata e di fine simulazione insieme a tutti gli altri.
 * ====================================================================== */

extern char **environ;

/* Legge il pid del responsabile_mensa dal file scritto all'avvio: e' anche
 * l'identificatore del suo process group (il responsabile chiama
 * setpgid(0,0) come prima cosa). */
static pid_t leggi_responsabile_pid(void) {
    FILE *f = fopen("output/responsabile.pid", "r");
    if (!f) {
        fprintf(stderr, "add_users: impossibile leggere output/responsabile.pid "
                        "(la simulazione e' in esecuzione?)\n");
        exit(EXIT_FAILURE);
    }

    int pid;
    if (fscanf(f, "%d", &pid) != 1) {
        fprintf(stderr, "add_users: file responsabile.pid corrotto\n");
        fclose(f);
        exit(EXIT_FAILURE);
    }
    fclose(f);
    return (pid_t)pid;
}

/* Crea un nuovo utente singolo (capogruppo di se stesso) agganciato alle
 * risorse IPC gia' esistenti. Il binario e' lo stesso usato dal
 * responsabile: il segnale su SEM_BARRIERA_READY emesso al termine della
 * propria inizializzazione vale come "presenza" alla barriera di fine
 * giornata in corso; dal giorno successivo il nuovo utente si allinea al
 * normale ciclo SEM_GATE_START. */
static pid_t avvia_nuovo_utente(pid_t pgid_responsabile, const char *config_path) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("add_users: fork");
        return -1;
    }
    if (pid == 0) {
        if (setpgid(0, pgid_responsabile) == -1)
            perror("add_users: setpgid");   /* non fatale: si prosegue */

        char *args[] = {
            (char *)"./bin/utente",
            (char *)config_path,
            (char *)"1",   /* capogruppo di se stesso */
            (char *)"1",   /* gruppo di un solo membro */
            (char *)"0",   /* pid capogruppo: ricalcolato dal processo stesso */
            NULL
        };
        execve("./bin/utente", args, environ);
        perror("add_users: execve utente");
        _exit(EXIT_FAILURE);
    }
    return pid;
}

int main(int argc, char *argv[]) {
    const char *config_path = (argc > 2) ? argv[2] : CONFIG_PATH_DEFAULT;

    Config cfg = config_load(config_path);

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

    int semid     = sem_set_get(0);
    int shm_st_id = shm_stazioni_get(0);
    StatoStazioni *stato = shm_stazioni_attach(shm_st_id);

    /* Il contatore va incrementato PRIMA delle fork: se il responsabile lo
     * legge nel frattempo, al piu' attende qualche istante in piu' alla
     * barriera di fine giornata, mentre incrementarlo dopo rischierebbe di
     * fargli ignorare processi gia' esistenti. */
    sem_wait_idx(semid, SEM_MUTEX_ATTESA);
    stato->nof_children_totali += n_new;
    sem_signal_idx(semid, SEM_MUTEX_ATTESA);

    int creati = 0;
    for (int i = 0; i < n_new; i++) {
        if (avvia_nuovo_utente(pgid_responsabile, config_path) > 0) creati++;
    }

    if (creati < n_new) {
        /* alcune fork sono fallite: rimetto a posto il contatore, altrimenti
         * il responsabile attenderebbe processi che non esistono */
        sem_wait_idx(semid, SEM_MUTEX_ATTESA);
        stato->nof_children_totali -= (n_new - creati);
        sem_signal_idx(semid, SEM_MUTEX_ATTESA);
    }

    printf("[ADD_USERS] Creati %d nuovi processi utente (richiesti: %d).\n", creati, n_new);

    shm_stazioni_detach(stato);
    return (creati > 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
