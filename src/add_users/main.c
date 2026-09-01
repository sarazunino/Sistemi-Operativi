#include "common.h"
#include "config.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>


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

    MsgRichiestaAddUsers req;
    req.mtype = MSG_ADDUSERS_RICHIESTA;
    req.n_utenti = n_new;

    if (msgsnd(mq, &req, sizeof(req) - sizeof(long), 0) == -1) {
        perror("add_users: msgsnd");
        return EXIT_FAILURE;
    }

    printf("[ADD_USERS] Richiesti %d nuovi utenti al responsabile mensa.\n", n_new);
    return EXIT_SUCCESS;
}
