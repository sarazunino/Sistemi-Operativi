/*
 * Processo CASSIERE (operatore cassa)
 * -------------------------------------
 * - Compete per una postazione cassa (NOF_WK_SEATS_CASSA)
 * - Riceve dagli utenti (coda messaggi/pipe) l'elenco piatti scelti
 *   + eventuale caffè, calcola il totale (PRICE_PRIMI/SECONDI/COFFEE)
 * - Aggiorna il ricavo in memoria condivisa (protetto da semaforo)
 * - Gestisce le pause come un operatore normale
 *
 * TODO: implementare
 */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return 0;
}
