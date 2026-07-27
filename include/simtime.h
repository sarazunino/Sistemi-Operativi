#ifndef SIMTIME_H
#define SIMTIME_H

/* ======================================================================
 * Tempo simulato e generazione di valori casuali.
 *
 * Convenzione unica per tutto il progetto (sez. 5.1): un minuto di
 * simulazione corrisponde a N_NANO_SECS nanosecondi reali, quindi un
 * secondo simulato corrisponde a N_NANO_SECS/60 nanosecondi reali.
 * Tutti i processi (responsabile, operatori, cassieri, utenti,
 * communication_disorder) usano queste funzioni: cosi' i tempi restano
 * confrontabili fra loro e con la durata della giornata.
 * ====================================================================== */

/**
 * @brief Inizializza il generatore pseudo-casuale del processo chiamante
 *        con un seme dipendente da pid e ora corrente.
 *
 * Va chiamata una sola volta subito dopo la exec, altrimenti processi
 * creati nello stesso istante genererebbero le stesse sequenze.
 */
void sim_srand(void);

/**
 * @brief Genera un intero casuale nell'intorno +-pct% del valore medio.
 *
 * @param media Valore medio (in secondi simulati o porzioni).
 * @param pct   Ampiezza percentuale dell'intorno (es. 50 per +-50%).
 * @return Valore casuale in [media-pct%, media+pct%], mai inferiore a 1.
 */
int sim_rand_intorno(int media, int pct);

/**
 * @brief Genera un intero casuale uniforme in [mn, mx].
 *
 * @param mn Estremo inferiore.
 * @param mx Estremo superiore (se minore di mn viene portato a mn).
 * @return Valore casuale nell'intervallo indicato.
 */
int sim_rand_range(int mn, int mx);

/**
 * @brief Converte minuti simulati in nanosecondi reali.
 *
 * @param n_nano_secs Nanosecondi reali per minuto simulato.
 * @param minuti      Minuti simulati.
 * @return Durata reale corrispondente, in nanosecondi.
 */
long sim_minuti_in_ns(long n_nano_secs, int minuti);

/**
 * @brief Sospende il processo per il numero indicato di secondi simulati,
 *        riprendendo il sonno residuo se interrotto da un segnale.
 *
 * Da usare quando l'operazione in corso non puo' essere abbandonata a
 * meta' (sez. 5.3: l'operatore "termina di servire il cliente che stava
 * servendo" anche se nel frattempo la giornata e' finita).
 *
 * @param n_nano_secs      Nanosecondi reali per minuto simulato.
 * @param secondi_simulati Durata in secondi simulati.
 */
void sim_sleep_sec(long n_nano_secs, int secondi_simulati);

/**
 * @brief Come sim_sleep_sec(), ma interrompibile da un segnale.
 *
 * @param n_nano_secs      Nanosecondi reali per minuto simulato.
 * @param secondi_simulati Durata in secondi simulati.
 * @return 0 se il tempo e' trascorso interamente, -1 se il sonno e' stato
 *         interrotto da un segnale (errno == EINTR).
 */
int sim_sleep_sec_interrompibile(long n_nano_secs, int secondi_simulati);

#endif /* SIMTIME_H */
