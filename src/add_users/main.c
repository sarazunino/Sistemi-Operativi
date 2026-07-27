#include "common.h"
#include "config.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>

/* ======================================================================
 * Eseguibile della versione completa (sez. 6): invocato da linea di
 * comando, da un terminale qualsiasi, chiede al responsabile mensa di
 * aggiungere alla simulazione gia' avviata dei nuovi processi utente
 * oltre a quelli creati inizialmente.
 *
 *   ./bin/add_users [n_utenti] [config_path]
 *
 * NON crea i processi direttamente: si limita a inviare una richiesta
 * sulla coda dedicata. E' il responsabile a fare fork/exec dei nuovi
 * utenti (vedi common.h, MsgRichiestaAddUsers), cosi' che nascano nel suo
 * stesso process group e possano essere raggiunti dai suoi segnali di
 * fine giornata/fine simulazione e attesi con waitpid() a chiusura: se
 * fosse add_users a fare la fork, il tentativo del figlio di unirsi al
 * process group del responsabile fallirebbe con EPERM ogni volta che
 * add_users viene lanciato da un terminale diverso da quello del
 * responsabile (setpgid richiede che il process group di destinazione
 * appartenga alla propria stessa sessione), lasciando i nuovi utenti
 * sordi a SIGUSR1/SIGTERM e bloccando per sempre la barriera di fine
 * giornata del responsabile.
 * ====================================================================== */

int main(int argc, char *argv[]) {
    const char *config_path = (argc > 2) ? argv[2] : CONFIG_PATH_DEFAULT;

    Config cfg = config_load(config_path);

    int n_new = cfg.n_new_users;
    if (argc > 1) {
        int override = atoi(argv[1]);
        if (override > 0) n_new = override;
    }
    if (n_new <= 0) {
        fprintf(stderr, "add_users: nessun nuovo utente da richiedere (N_NEW_USERS=%d)\n", n_new);
        return EXIT_SUCCESS;
    }

    int mq = msgq_addusers_get(0);
    if (mq == -1) {
        fprintf(stderr, "add_users: risorse IPC non trovate, la simulazione e' avviata?\n");
        return EXIT_FAILURE;
    }

    MsgRichiestaAddUsers req;
    req.mtype    = MSG_ADDUSERS_RICHIESTA;
    req.n_utenti = n_new;

    if (msgsnd(mq, &req, sizeof(req) - sizeof(long), 0) == -1) {
        perror("add_users: msgsnd");
        return EXIT_FAILURE;
    }

    printf("[ADD_USERS] Richiesti %d nuovi utenti al responsabile mensa.\n", n_new);
    return EXIT_SUCCESS;
}
