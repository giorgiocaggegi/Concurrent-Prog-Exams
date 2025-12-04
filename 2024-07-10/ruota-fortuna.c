#include "../lib-misc.h"
#include <pthread.h>
#include <stdbool.h>
#include <ctype.h>

#define USAGE "Uso: %s <n-numero-giocatori> <m-numero-partite> <file-con-frasi>\n"
#define ALPHASIZ ('z'-'a'+1)

#define lockm(m) \
    do { \
        int errrrr = pthread_mutex_lock((m)); \
        if (errrrr != 0) exit_with_err("pthread_mutex_lock", errrrr); \
    } while (0)
#define unlockm(m) \
    do { \
        int errrrr = pthread_mutex_unlock((m)); \
        if (errrrr != 0) exit_with_err("pthread_mutex_unlock", errrrr); \
    } while (0)
#define waitc(c, m) \
    do { \
        int errrrr = pthread_cond_wait((c),(m)); \
        if (errrrr != 0) exit_with_err("pthread_cond_wait", errrrr); \
    } while (0)
#define signc(c) \
    do { \
        int errrrr = pthread_cond_signal((c)); \
        if (errrrr != 0) exit_with_err("pthread_cond_signal", errrrr); \
    } while (0)
#define broadc(c) \
    do { \
        int errrrr = pthread_cond_broadcast((c)); \
        if (errrrr != 0) exit_with_err("pthread_cond_broadcast", errrrr); \
    } while (0)


size_t mcwrap(const char *str) {

    errno = 0;
    char *endptr;    
    long val = strtol(str, &endptr, 10);

    if (errno != 0) {
        fprintf(stderr, USAGE, str);
        exit_with_sys_err("strtol");
    }
    if (endptr == str || val <= 0) {
        exit_with_err_msg(USAGE, str);
    }

    return (size_t)val;

}

struct node {
    char *frase;
    struct node *next;
};
struct list {
    struct node *head;
    size_t count;
};

void insert(struct list *l, char *c) {
    assert(l && c);
    size_t c_len = strlen(c);
    struct node *new = (struct node *)calloc(1, sizeof(struct node));
    assert(new);
    
    new->frase = (char *)malloc(sizeof(char)*(c_len+1));
    memcpy(new->frase, c, c_len+1);
    

    new->next = l->head;
    l->head = new;
    l->count++;

}

char *extract(struct list *l) {
    assert(l);
    if (l->count == 0) return NULL;
    
    ssize_t what = rand() % l->count;

    struct node *prev = NULL;
    struct node *cur = l->head;
    while (--what > 0) {
        prev = cur;
        cur = cur->next;
    } 
    char *ret = cur->frase;
    if (prev) prev->next = cur->next;
    else l->head = cur->next;
    free(cur);
    l->count--;

    return ret;
}

void offusca(char *str, size_t len) {
    assert(str);
    for (size_t i = 0; i < len; i++) {
        if (isalpha(str[i])) str[i] = '#';
    }
}

struct alpha {
    char c;
    bool valid;
};
struct alpha *get_alpha() {
    struct alpha *ret = (struct alpha *)malloc(ALPHASIZ*sizeof(struct alpha));
    assert(ret);
    for (size_t i = 0; i < ALPHASIZ; i++) {
        ret[i].c = 'a'+i;
        ret[i].valid = true;
    }
    return ret;
}
void reset_alpha(struct alpha *a) {
    assert(a);
    for (size_t i = 0; i < ALPHASIZ; i++) {
        a[i].valid = true;
    }
}
long discover(char c, char *str, char *osc, size_t len) {
    /*bool set = false;
    for (size_t i = 0; i < ALPHASIZ || !(set); i++) {
        if (c == a[i].c) set = true;
    }*/
    if (c < 'a' || c > 'z') exit_with_err_msg("Dafuck?\n");

    long ret = 0;
    for (size_t i = 0; i < len; i++) {
        if (c == tolower(str[i])) {
            osc[i] = str[i];
            ret++;
        }
    }
    return ret;
}
char set_random(struct alpha *a) {
    bool set = false;
    char c;

    while (!set) {
        
        size_t ture = rand() % ALPHASIZ;
        if (a[ture].valid) {
            c = a[ture].c;
            a[ture].valid = false;
            set = true;
        }
    }

    return c;
}

long *get_ps(size_t n) {
    long *ps = (long *)calloc(n, sizeof(long));
    assert(ps);
    return ps;
}

struct shared {
    struct alpha *a;
    char c;
    long *ps;
    
    ssize_t i;

    pthread_mutex_t mut;
    pthread_cond_t player;
    pthread_cond_t mike;

    bool game;
    bool set_by_g;
};
struct shared *get_shared(size_t n) {
    struct shared *s = (struct shared *)malloc(sizeof(struct shared));
    assert(s);
    s->a = get_alpha();
    s->ps = get_ps(n);
    s->game = true;
    s->set_by_g = false;
    s->i = -1;
    
    int err = pthread_mutex_init(&s->mut, NULL);
    if (err != 0) exit_with_err("pthread_mutex_init", err);
    err = pthread_cond_init(&s->player, NULL);
    if (err != 0) exit_with_err("pthread_cond_init", err);
    err = pthread_cond_init(&s->mike, NULL);
    if (err != 0) exit_with_err("pthread_cond_init", err);

    return s;
}

struct g_tdata {
    size_t i;
    struct shared *s;
};
void *g_tfunc(void *arg) {
    struct g_tdata *t = (struct g_tdata *)arg;
    struct shared *s = t->s;
    size_t i = t->i;

    printf("[G%zu] avviato e pronto\n", i);

    while (true) {
        lockm(&s->mut);
        while (s->i == -1 || (s->game && (s->set_by_g || (size_t)s->i != i))) {
            waitc(&s->player, &s->mut);

            if (s->i != -1 && s->game && !s->set_by_g && (size_t)s->i != i) {
                unlockm(&s->mut);
                signc(&s->player);
            }
        }

        if (!s->game) {
            unlockm(&s->mut);
            break;
        }

        char c = set_random(s->a);
        printf("[G%zu] scelgo la lettera '%c'\n", i, c);
        s->c = c;
        s->set_by_g = true;
        signc(&s->mike);
        unlockm(&s->mut);

    }
    

    free(t);
    printf("[G%zu] exiting...\n", i);
    return NULL;
}

int main(int argc, const char **argv) {

    srand(time(NULL));

    if (argc != 4) exit_with_err_msg(USAGE, argv[0]);

    size_t n = mcwrap(argv[1]);
    size_t m = mcwrap(argv[2]);
    struct list l = {0};

    FILE *fp = fopen(argv[3], "r");
    if (!fp) exit_with_sys_err("fopen");

    char buffer[BUFSIZ];
    while (fgets(buffer, BUFSIZ, fp)) {
        size_t b_len = strlen(buffer);
        if (buffer[b_len-1] == '\n') {
            buffer[b_len-1] = '\0';
            b_len--;
        }
        insert(&l, buffer);
        
    }
    if (ferror(fp)) exit_with_sys_err("fgets");
    if (m > l.count) exit_with_err_msg("Non ci sono frasi sufficienti. Max valore di m: %zu\n", l.count);

    printf("[M] lette %zu possibili frasi da indovinare per %zu partite\n", l.count, m);

    struct shared *s = get_shared(n);

    // Startin threads
    for (size_t i = 0; i < n; i++) {
        pthread_t tid;
        struct g_tdata *g = (struct g_tdata *)malloc(sizeof(struct g_tdata));
        g->i = i;
        g->s = s;
        int err = pthread_create(&tid, NULL, g_tfunc, g);
        if (err != 0) exit_with_err("pthread_create", err);
        err = pthread_detach(tid);
        if (err != 0) exit_with_err("pthread_detach", err);
    }


    size_t count = 1;
    char *str = NULL;
    while ((str = extract(&l)) && count <= m) {
        char *osc = strdup(str);
        size_t len = strlen(str);
        printf("[M] scelta la frase \"%s\" per la partita n.%zu\n", str, count);

        offusca(osc, len);

        bool work = true;
        size_t turno = 0;
        printf("[M] tabellone: %s\n", osc);
        do {
            lockm(&s->mut);
            printf("[M] adesso è il turno di G%zu\n", turno);
            s->i = turno;
            unlockm(&s->mut);

            signc(&s->player);

            lockm(&s->mut);
            while (!s->set_by_g) waitc(&s->mike, &s->mut);

            long howmany = discover(s->c, str, osc, len);
            long r = (1 + rand() % (4))*100;
            long points = howmany * r;
            printf("[M] ci sono %ld occorrenze di '%c'; assegnati %ldx%ld=%ld punti a G%zu\n",
                howmany, s->c, howmany, r, points, turno
            );
            s->ps[turno] += points;

            unlockm(&s->mut);
            
            printf("[M] tabellone: %s\n", osc);
            turno = (turno+1) % n;
            s->set_by_g = false;
            if (strcmp(str, osc) == 0) {
                flockfile(stdout);
                printf("[M] frase completata; punteggi attuali: ");
                for (size_t i = 0; i < n; i++) {
                    printf("G%zu:%ld ", i, s->ps[i]);
                }
                printf("\n");
                fflush(stdout);
                funlockfile(stdout);
                reset_alpha(s->a);
                work = false;
            }
        } while (work);

        if (str) free(str);
        if (osc) free(osc);
        count++;
    }

    lockm(&s->mut);
    printf("[M] questa era l'ultima partita: il vincitore è G2\n");
    s->game = false;
    unlockm(&s->mut);
    broadc(&s->player);

}

