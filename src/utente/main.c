/*
 * Processo UTENTE
 * -----------------
 * - Decide il menu (lettura config/menu.txt)
 * - Si accoda alla stazione primi/secondi/coffee in base alle code
 *   (stato code leggibile da memoria condivisa)
 * - Se il piatto scelto è terminato: sceglie alternativa dello stesso
 *   tipo, o rinuncia a quel tipo, o desiste del tutto se non c'è nulla
 * - Passa in cassa, paga
 * - Cerca un posto libero tra i NOF_TABLE_SEATS, mangia, poi esce
 * - Se a fine giornata è ancora in coda: abbandona (statistiche "non serviti")
 *
 * Versione completa: gestione utente_w_ticket / utente_w_o_ticket
 * e gruppi che pagano insieme.
 *
 * TODO: implementare
 */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return 0;
}
