/*
 * Processo RESPONSABILE MENSA
 * ----------------------------
 * - Legge la configurazione (config.h)
 * - Crea le risorse IPC condivise (memoria condivisa, semafori, code)
 * - Crea (fork+execve) NOF_WORKERS operatori, NOF_USERS utenti, il cassiere
 * - Crea le 4 stazioni di distribuzione + i posti tavolo
 * - Attende che tutti i processi abbiano finito l'inizializzazione
 *   (es. tramite semaforo/barriera) prima di far partire la simulazione
 * - Cicla per SIM_DURATION giorni, ognuno "lungo" N_NANO_SECS al minuto
 * - Ogni fine giornata: legge le statistiche (mai le scrive!) e le stampa
 * - Gestisce la terminazione (timeout / overload), stampa causa finale
 * - Dealloca tutte le risorse IPC prima di uscire
 *
 * TODO: implementare tutto quanto sopra
 */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return 0;
}
