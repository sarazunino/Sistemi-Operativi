#include "simtime.h"

#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

/* Nanosecondi reali corrispondenti a un secondo simulato. Non scende mai
 * sotto 1 ns, cosi' nanosleep() riceve sempre un valore valido anche con
 * configurazioni molto "accelerate". */
static long ns_per_secondo_simulato(long n_nano_secs) {
    long ns = n_nano_secs / 60;
    return (ns > 0) ? ns : 1;
}

void sim_srand(void) {
    srand((unsigned)time(NULL) ^ ((unsigned)getpid() << 16) ^ (unsigned)getpid());
}

int sim_rand_intorno(int media, int pct) {
    if (media <= 0) return 1;
    if (pct < 0) pct = 0;

    int delta = (media * pct) / 100;
    int mn = media - delta; if (mn < 1) mn = 1;
    int mx = media + delta; if (mx < mn) mx = mn;

    return mn + rand() % (mx - mn + 1);
}

int sim_rand_range(int mn, int mx) {
    if (mn < 0) mn = 0;
    if (mx < mn) mx = mn;
    return mn + rand() % (mx - mn + 1);
}

long sim_minuti_in_ns(long n_nano_secs, int minuti) {
    if (minuti < 0) minuti = 0;
    return (long)minuti * n_nano_secs;
}

void sim_sleep_sec(long n_nano_secs, int secondi_simulati) {
    if (secondi_simulati < 1) secondi_simulati = 1;

    long totale = (long)secondi_simulati * ns_per_secondo_simulato(n_nano_secs);

    struct timespec richiesto, rimanente;
    richiesto.tv_sec  = totale / 1000000000L;
    richiesto.tv_nsec = totale % 1000000000L;

    while (nanosleep(&richiesto, &rimanente) == -1 && errno == EINTR) {
        richiesto = rimanente;
    }
}

int sim_sleep_sec_interrompibile(long n_nano_secs, int secondi_simulati) {
    if (secondi_simulati < 1) secondi_simulati = 1;

    long totale = (long)secondi_simulati * ns_per_secondo_simulato(n_nano_secs);

    struct timespec richiesto;
    richiesto.tv_sec  = totale / 1000000000L;
    richiesto.tv_nsec = totale % 1000000000L;

    if (nanosleep(&richiesto, NULL) == -1) return -1;
    return 0;
}
