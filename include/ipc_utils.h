#ifndef IPC_UTILS_H
#define IPC_UTILS_H
/*
 * Wrapper per creazione/distruzione di:
 *  - memoria condivisa (statistiche, stato stazioni/postazioni)
 *  - semafori (System V semget/semop o POSIX sem_open)
 *  - code di messaggi o pipe (comunicazione utente/operatore/cassa)
 *
 * TODO: init_shared_memory(), cleanup_shared_memory()
 * TODO: init_semaphores(), cleanup_semaphores()
 * TODO: init_message_queues(), cleanup_message_queues()
 * TODO: gestione segnali per rimozione IPC anche su terminazione anomala
 */
#endif
