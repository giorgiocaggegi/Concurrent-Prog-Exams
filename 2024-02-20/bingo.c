#include "../lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <inttypes.h>
#include <stdbool.h>

#define swait(s) if (sem_wait(&(s))!=0) exit_with_sys_err("sem_wait")
#define spost(s) if (sem_post(&(s))!=0) exit_with_sys_err("sem_post")

#define X 3
#define Y 5

typedef unsigned long pid;

struct shared {
    uint8_t card[X][Y];

    uint8_t last;

    pid checked;

    bool cinquina;
    bool bingo;
    pid winner;
    bool eow;
    //pid nplayers;

    sem_t mutex;
    sem_t players;
    sem_t dealer;
};
void shared_init(struct shared *s)
    //, pid nplayers) 
{
    assert(s);
    s->cinquina = s->bingo = false;
    s->eow = false;

    if (sem_init(&s->mutex, 0, 1) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&s->players, 0, 0) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&s->dealer, 0, 1) != 0) exit_with_sys_err("sem_init");
}

struct player_tdata {
    pid id;
    struct shared *s;
    pid m;
};
void *player_tfunc(void *a) {
    struct player_tdata *t = (struct player_tdata *)a;
    struct shared *s = t->s;
    uint8_t ***mycard = (uint8_t ***)malloc(sizeof(uint8_t**)*t->m);
    assert(mycard);
    for (pid i = 0; i < t->m; i++) {
        mycard[i] = (uint8_t **)malloc(sizeof(uint8_t*)*X);
        assert(mycard[i]);
        for (short j = 0; j < X; j++) {
            mycard[i][j] = (uint8_t *)malloc(sizeof(uint8_t)*Y);
        }
    }

    //pid count = 0;
    for (pid i = 0; i < t->m; i++) {
        swait(s->players);
        swait(s->mutex);
        for (short j = 0; j < X; j++) {
            for (short z = 0; z < Y; z++) {
                mycard[i][j][z] = s->card[j][z];
            }
        }
        spost(s->mutex);
        spost(s->dealer);
    }

    uint8_t last = 0;
    bool eow = false;
    while (!eow) {

        swait(s->players);
        swait(s->mutex);
        if (!(eow = s->eow) && last != s->last) {
            s->winner = t->id;
            s->checked++;
        }
        spost(s->mutex);
        if (!eow) spost(s->dealer);
    }

    return NULL;
}

pid parse(char *str) {
    char *endptr;

    errno = 0;
    long ret = strtol(str, &endptr, 10);
    
    if (errno) exit_with_sys_err("strtoll");
    if (endptr == str) exit_with_err_msg("No digits found while parsing \'%s\'\n", str);
    if (ret <= 0) exit_with_err_msg("Negative argument\n");

    return (pid)ret;
}

void sdistribuisci(struct shared *s, pid n, pid m) {

    for (pid i = 0; i < n*m; i++) {
        swait(s->dealer);
        swait(s->mutex);
        printf("D: genero e distribuisco la card n.%lu: ", i+1);
        for (short i = 0; i < X; i++) {
            for (short j = 0; j < Y; j++) {
                s->card[i][j] = (uint8_t)(1+rand()%(75));
                printf("%u ", s->card[i][j]);
            }
            printf("/ ");
        }
        printf("\n");
        spost(s->mutex);
        spost(s->players);

    }
}

struct node {
    uint8_t num;
    struct node *next;
};
struct list {
    struct node *head;
    size_t count;
};
void list_init(struct list *l) {
    assert(l);

    struct node *prev = NULL;
    for (uint8_t i = 75; i >= 1; i--) {
        struct node *new = (struct node *)malloc(sizeof(struct node));
        assert(new);
        new->num = i;
        new->next = prev;
        prev = new;
    }
    l->head = prev;
    l->count = 75;
}
uint8_t a_cazz(struct list *l) {
    assert(l);

    if (l->count == 0) return 0;

    size_t x = (size_t)rand()%(l->count);
    
    struct node *prev = NULL;
    struct node *cur = l->head;
    for (size_t i = 0; i < x; i++) {
        prev = cur;
        cur = cur->next;
    }
    uint8_t ret = cur->num;
    if (!prev) {
        l->head = cur->next;
    } else {
        prev->next = cur->next;
    }
    free(cur);

    l->count--;

    return ret;
}

int main(int argc, char **argv) {
    if (argc != 3) exit_with_err_msg("USO: \'%s\' <n> <m>\n", argv[0]);

    pid n = parse(argv[1]);
    pid m = parse(argv[2]);
    struct player_tdata tds[n];
    printf("D: ci saranno %lu giocatori con %lu card ciascuno\n", n, m);
    struct shared s;
    shared_init(&s);
    struct list l;
    list_init(&l);

    for (pid i = 0; i < n; i++) {
        tds[i].id = i;
        tds[i].m = m;
        tds[i].s = &s;

        pthread_t ture;
        int err = pthread_create(&ture, NULL, player_tfunc, tds+i);
        if (err) exit_with_err("pthread_create", err);
        if ((err = pthread_detach(ture))) exit_with_err("pthread_detach", err);
    }
    
    sdistribuisci(&s, n, m);
    printf("D: fine della distribuzione delle card e inizio di estrazione dei numeri\n");

    //swait(s.dealer);

    uint8_t ture = 0;
    while ((ture = a_cazz(&l)) != 0) {
        printf("D: estrazione del prossimo numero: %u\n", ture);

        swait(s.dealer);
        swait(s.mutex);
        s.last = ture;
        s.cinquina = false;
        s.checked = 0;
        spost(s.mutex);
        spost(s.players);
        
        pid count = 0;
        while (count < n) {
            swait(s.dealer);
            swait(s.mutex);
            count = s.checked;
            if (ture == 12) {
                s.eow = true;
                spost(s.mutex);
                for (pid i = 0; i < n; i++) spost(s.players);
                pthread_exit(NULL);
            } else {
                printf("%lu ", s.winner);
                spost(s.mutex);
                spost(s.players);
            }
        }
        printf("\n");
    }
    printf("D buonanotte!\n");
    pthread_exit(NULL);
}
