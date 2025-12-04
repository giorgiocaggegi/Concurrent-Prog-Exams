#include "../lib-misc.h"
#include <pthread.h>
#include <stdbool.h>

#define lockm(m) \
    do { \
        int err_macro = pthread_mutex_lock((m)); \
        if (err_macro) exit_with_err("pthread_mutex_lock", err_macro); \
    } while (0)
#define unlockm(m) \
    do { \
        int err_macro = pthread_mutex_unlock((m)); \
        if (err_macro) exit_with_err("pthread_mutex_unlock", err_macro); \
    } while (0) 
#define signalc(c) \
    do { \
        int err_macro = pthread_cond_signal((c)); \
        if (err_macro) exit_with_err("pthread_cond_signal", err_macro); \
    } while (0)
#define broadc(c) \
    do { \
        int err_macro = pthread_cond_broadcast((c)); \
        if (err_macro) exit_with_err("pthread_cond_broadcast", err_macro); \
    } while (0)
#define waitc(c, m) \
    do { \
        int err_macro = pthread_cond_wait((c),(m)); \
        if (err_macro) exit_with_err("pthread_cond_wait", err_macro); \
    } while (0)

/*
Tutti i thread comunicheranno usando una
unica struttura dati condivisa e si coordineranno impiegando 2 (due) 
variabili condizione
e un mutex. 
La struttura dati condivisa dovrà contenere almeno le seguenti informazioni:
<object-description>, <minimum-offer>, <maximum-offer> e una generica offerta del
momento <offer>.
Il thread J, per ogni d
*/

struct shared {
    char description[BUFSIZ];
    unsigned long minimum;
    unsigned long maximum;
    unsigned long offer;
    unsigned long best;
    long winner;

    unsigned long sell_id;
    unsigned long turn;
    bool *offered;
    bool eow;

    pthread_mutex_t mut;
    pthread_cond_t bidders;
    pthread_cond_t judge;
};

void shared_init(struct shared *s, unsigned long n) {
    assert(s);

    s->sell_id = 1;
    s->turn = 0;
    s->best = 0;
    s->offered = (bool *)calloc(n, sizeof(bool));
    s->eow = false;
    s->winner = -1; 

    int err = pthread_mutex_init(&s->mut, NULL);
    if (err) exit_with_err("pthread_mutex_init", err);
    err = pthread_cond_init(&s->bidders, NULL);
    if (err) exit_with_err("pthread_cond_init", err); 
    err = pthread_cond_init(&s->judge, NULL);
    if (err) exit_with_err("pthread_cond_init", err); 
}

bool offer(struct shared *s, unsigned long i) {
    assert(s);
    bool offered = false;

    lockm(&s->mut);
    while ((s->turn != i || s->offered[i]) && !s->eow && s->sell_id != 0) {
        if (s->sell_id != 0) signalc(&s->bidders);
        waitc(&s->bidders, &s->mut);
    }

    if (!s->eow) {
        s->offer = 1 + rand() % s->maximum;
        s->offered[i] = true;
        printf("[B%zu] invio offerta di %lu EUR per asta n.%lu\n", i, s->offer, s->sell_id);

        signalc(&s->judge);
        offered = true;
    }
    unlockm(&s->mut);

    return offered;
}

struct tdata {
    struct shared *s;
    unsigned long i;
};
void *tfunc(void *a) {
    struct tdata *t = (struct tdata *)a;
    struct shared *s = t->s;
    unsigned long i = t->i;

    while (offer(s, i));

    return NULL;
}

unsigned long parse(char *str) {
    char *endptr;

    errno = 0;
    long val = strtol(str, &endptr, 10);

    if (errno != 0) exit_with_sys_err("fopen");
    if (endptr == str) exit_with_err_msg("No number found while parsing %s\n", str);
    if (val < 0) exit_with_err_msg("Parsed a negagtive number\n");

    return (unsigned long)val;
}

void zicc(char *buffer, struct shared *s) {
    assert(buffer && s);

    char *str = strtok(buffer, ",");
    if (!str) exit_with_err_msg("Tokenization failed\n");
    memcpy(s->description, str, strlen(str)+1);

    str = strtok(NULL, ",");
    if (!str) exit_with_err_msg("Tokenization failed\n");
    s->minimum = parse(str);

    str = strtok(NULL, ",");
    if (!str) exit_with_err_msg("Tokenization failed\n");
    s->maximum = parse(str);

}

bool some_active(struct shared *s, unsigned long n) {
    assert(s);
    for (unsigned long i = 0; i < n; i++) if (s->offered[i]) return true;
    return false;
}

int main(int argc, char **argv) {
    if (argc != 3) exit_with_err_msg("USO: \'%s\' <auction-file> <num-bidders>\n", argv[0]);
    
    struct shared s;
    int err;
    FILE *fp;
    unsigned long n = parse(argv[2]);
    struct tdata tds[n];

    if (!(fp = fopen(argv[1], "r"))) exit_with_sys_err("fopen");

    shared_init(&s, n);
    for (unsigned long i = 0; i < n; i++) {
        tds[i].s = &s;
        tds[i].i = i;
        pthread_t tid;
        if ((err = pthread_create(&tid, NULL, tfunc, tds+i))) exit_with_err("pthread_create", err);
        if ((err = pthread_detach(tid))) exit_with_err("pthread_detach", err);
    }

    unsigned long totsoldi = 0;
    unsigned long totbabbe = 0;
    unsigned long totbuone = 0;
    char buffer[BUFSIZ];
    while (fgets(buffer, BUFSIZ, fp)) {
        lockm(&s.mut);
        zicc(buffer, &s);
        s.turn = 0;
        s.winner = -1;
        s.best = 0;
        for (unsigned long i = 0; i < n; i++) s.offered[i] = false;
        printf("[J] lancio asta n.%lu per %s con offerta minima di %lu EUR e massima di %lu EUR\n",
            s.sell_id, s.description, s.minimum, s.maximum
        );
        unlockm(&s.mut);
        signalc(&s.bidders);

        unsigned long howmany = 0;
        for (unsigned long i = 0; i < n; i++) {
            lockm(&s.mut);
            while (!s.offered[s.turn])
                waitc(&s.judge, &s.mut);
            
            printf("[J] ricevuta offerta da B%lu\n",i);
            if (s.offer > s.minimum && s.offer > s.best) {
                s.best = s.offer;
                s.winner = (long)i;
                howmany++;
            }

            s.turn++;
            if (i != n-1) {
                unlockm(&s.mut);
                signalc(&s.bidders);
            }
        }

        if (s.winner == -1) {
            printf("[J] l'asta n.%lu per %s si è conclusa senza alcuna offerta valida pertanto l'oggetto non risulta assegnato\n",
                s.sell_id, s.description
            );
            totbabbe++;
        } else {
            printf("[J] l'asta n.%lu per %s si è conclusa con %lu offerte valide su %lu; il vincitore è B%lu che si aggiudica l'oggetto per %lu EUR\n",
                s.sell_id, s.description, howmany, n, (unsigned long)s.winner, s.best
            );
            totsoldi += s.best;
            totbuone++;
        }
        s.sell_id++;
        printf("\n");
        unlockm(&s.mut);

    }

    lockm(&s.mut);
    s.eow = true;
    unlockm(&s.mut);
    broadc(&s.bidders);

    printf("[J] sono state svolte %lu aste di cui %lu andate assegnate e %lu andate a vuoto; il totale raccolto è di %lu EUR\n",
        s.sell_id-1, totbuone, totbabbe, totsoldi
    );
    
    pthread_exit(NULL);

}
