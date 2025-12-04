#include "../lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <string.h>

// Safe wrappers for semaphore operations
#define swait(s) if (sem_wait((s)) != 0) exit_with_sys_err("sem_wait")
#define spost(s) if (sem_post((s)) != 0) exit_with_sys_err("sem_post")

#define USAGE "Uso: %s <M-verifiers (> 0)> <file-1> ... <file-N>\n", argv[0]

// Intermediate queue size:
#define INTERSIZ 10
// Final queue size:
#define FINSIZ 3
// Square matrix dimension
#define X 3

struct queue {
    long ***buf;
    size_t siz; // array size
    size_t count; // current number of insert elements
    size_t low; // index to the 'tail' of the queue
    size_t high; // index to the next free position

    size_t nprod; // number of active producers
    size_t ncons; // number of active consumers

    sem_t mut; // it will serve as a mutex
    sem_t empty; // it will count empty positions in the buffer
    sem_t full; // it will count full posititions
};

// This circular queue structure will contain a dynamic allocated array of
// double pointers to long values.
// Tracing the number of producers and the number of consumer will allow
// the program to self-terminate.
void queue_init(struct queue *q, size_t siz, size_t nprod, size_t ncons) {
    assert(q);
    q->buf = (long***)malloc(sizeof(long**) * siz);
    assert(q->buf);
    q->siz = siz;
    q->nprod = nprod;
    q->ncons = ncons;
    q->count = 0;
    q->low = q->high = 0;

    if (sem_init(&q->mut, 0, 1) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&q->empty, 0, siz) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&q->full, 0, 0) != 0) exit_with_sys_err("sem_init");
}

void queue_insert(struct queue *q, long **ar) {
    assert(q && ar);

    // If a producers is unable to complete a wait (decrease) operation
    // to the empty semaphore (if its counter is 0), 
    // then the queue is full and the producer is put to sleep
    swait(&q->empty);
    swait(&q->mut);

    // If a producer obtained queue access through the mutex purposed semaphore,
    // the new element will be inserted at the head of the queue
    q->buf[q->high] = ar;
    // The modulo will enable the circular queue mechanism
    q->high = (q->high + 1) % q->siz;
    q->count++;

    // Releasing the "mutex"
    spost(&q->mut);
    // Segnaling an eventual consumer that an element is ready,
    // by increasing semaphore's counter
    spost(&q->full);

}

long **queue_remove(struct queue *q) {
    assert(q);
    long **ret = NULL;

    // Here, a consumer is put to sleep if it can't complete a wait on 'full'
    // (if the queue is empty)
    // It will be woken up:
    // - by an element insertion
    // - by the queue_decrease procedure once the last active producer calls it
    //   and becomes unactive
    swait(&q->full);
    swait(&q->mut);
    if (q->count > 0) {
        // This code will be executed if there were elements in the queue
        // (so even if the queue was empty and and an element was inserted)
        // BUT NOT IF THE CONSUMER WAS WOKEN BY queue_decrease [if nprod == 0]
        // AND NO ELEMENTS REMAINED IN THE QUEUE
        ret = q->buf[q->low]; // low is the tail index
        q->low = (q->low+1)%q->siz;
        q->count--;
    }
    spost(&q->mut);
    // Only if an element was picked, this operation would make sense.
    // If the consumer was woken by number of producers that went to 0
    // and the queue was empty, ret pointer will still be NULL
    if (ret) spost(&q->empty);

    return ret;
}

// Called by producers when they end their work
void queue_decrease(struct queue *q) {
    assert(q);
    bool wakeall = false; // a flag is needed... 

    swait(&q->mut);
    if (q->nprod > 0) q->nprod--;
    if (q->nprod == 0) wakeall = true;
    spost(&q->mut);

    // ... because the consumers have to be waked
    // if the producers number went to 0 WHILE NOT
    // HOLDING THE 'mutex'
    if (wakeall)
        for (size_t i = 0; i < q->ncons; i++)
            spost(&q->full);
}

// Creates a 3x3 long matrix from a string cointaining 9 comma-separated integer numbers
long **get_matrix(char *str) {

    long **ret = (long **)malloc(sizeof(long*)*X);
    assert(ret);
    for (size_t i = 0; i < X; i++) {
        ret[i] = (long *)malloc(sizeof(long)*X);
        assert(ret[i]);
    }

    char *token, *saveptr, *endptr;
    char *str1 = str;

    for (size_t i = 0; i < X; i++) {
        for (size_t j = 0; j < X; j++) {
            token = strtok_r(str1, ",", &saveptr);
            if (token == NULL) exit_with_err_msg("Tokenization failed\n");

            errno = 0;
            ret[i][j] = strtol(token, &endptr, 10);

            if (errno != 0) exit_with_sys_err("strtol");

            if (endptr == token) {
               exit_with_err_msg("No digits were found in a token\n");
            }

            if (str1) str1 = NULL;
        }
    }
    // It will succeed if at least 9 values are found in the string

    return ret;

}

void print_mat(long **mat) {
    
    for (size_t i = 0; i < X; i++) {
        printf("(");
        for (size_t j = 0; j < X; j++) {
            printf("%ld", mat[i][j]);
            if (j != X-1) printf(", ");
        }
        printf("%s", (i == X-1 ? ")" : ") "));
    }
}

// Structure that will be passed at reader threads
// when creating it
struct reader_tdata {
    const char *path; // of a file
    size_t i; // the thread 'tag'
    struct queue *q; // the shared intermediate structure
};
void *reader_tfunc(void *arg) {
    struct reader_tdata *t = (struct reader_tdata *)arg;
    struct queue *q = t->q; // it's shared between readers and verifiers
    size_t i = t->i;
    printf("[READER-%zu] file '%s'\n", i, t->path);

    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("fopen");

    // The files contains plain text
    // Each line is expected to be way shorter than the default
    // stdio buffer size (typically 8192 bytes)
    char buffer[BUFSIZ];
    size_t count = 1;
    while (fgets(buffer, BUFSIZ, fp)) {
        // Each line is a matrix
        long **mat = get_matrix(buffer);
        // I'm using flockfile to not interlace between
        // different threads not-\n-terminated lines 
        flockfile(stdout);
        printf("[READER-%zu] quadrato candidato n.%zu: ", i, count++);
        print_mat(mat);
        printf("\n");
        fflush(stdout);
        funlockfile(stdout);
        
        // I will insert the matrix in the intermediate queue
        queue_insert(q, mat); 
    }
    if (ferror(fp)) exit_with_sys_err("fgets");

    queue_decrease(q); // The reader ends its work when the file is over

    printf("[READER-%zu] terminazione...\n",i);
    return NULL;
}

// The magic requirement from the assignment
bool is_magic(long **mat) {
    assert(mat);

    long magtot = 0;
    for (size_t j = 0; j < X; j++) magtot += mat[0][j];

    long curtot;

    for (size_t i = 1; i < X; i++) {
        curtot = 0;
        for (size_t j = 0; j < X; j++) {
            curtot += mat[i][j];
        }
        if (curtot != magtot) return false;
    }
    
    for (size_t i = 0; i < X; i++) {
        curtot = 0;
        for (size_t j = 0; j < X; j++) {
            curtot += mat[j][i];
        } 
        if (curtot != magtot) return false;
    }
    
    curtot = 0;
    for (size_t i = 0; i < X; i++) {
        curtot += mat[i][i];
    }
    if (curtot != magtot) return false;
    
    curtot = 0;
    size_t i = X-1;
    size_t j = 0;
    for (; j < X; i--, j++) {
        curtot += mat[j][i];
    }
    if (curtot != magtot) return false;

    return true;
}

// Structure that will be passed at verifier threads
struct verifier_tdata {
    struct queue *inter;
    struct queue *fin;
    size_t i;
};
void *verifier_tfunc(void *arg) {
    struct verifier_tdata *t = (struct verifier_tdata *)arg;
    // structure shared between readers (producers) and verifiers (consumers):
    struct queue *inter = t->inter;
    // structure shared between verifiers (producers) and main (consumer):
    struct queue *fin = t->fin;
    size_t i = t->i;

    long **cur = NULL;
    while ((cur = queue_remove(inter))) {
        // Need the flag to avoid circular dependencies
        // that would be potentially casued by the call
        // of queue_insert between flockfile and funlockfile

        bool found = false;
        flockfile(stdout);
        printf("[VERIFIER-%zu] verifico quadrato: ", i);
        print_mat(cur);
        printf("\n");
        fflush(stdout);
        if (is_magic(cur)) {
            found = true;
            printf("[VERIFIER-%zu] trovato quadrato magico!\n", i);
        } 
        funlockfile(stdout);

        if (found) {
            queue_insert(fin, cur);
        } else {
            // It's important to free the unneeded matrixes
            for (size_t i = 0; i < X; i++) free(cur[i]);
            free(cur);
        }
    }
    queue_decrease(fin);

    printf("[VERIFIER-%zu] terminazione...\n",i);
    return NULL;
}

// Second argument parser (m, number of verifiers)
size_t get_m_arg(const char **argv) {
    char *endptr;
    const char *str = argv[1];
    long val;

    errno = 0;
    val = strtol(str, &endptr, 10);

    if (errno != 0) {
        exit_with_sys_err("strtol");
    }
    if (endptr == str) {
        exit_with_err_msg(USAGE);
    }
    if (val < 0) {
        exit_with_err_msg(USAGE);
    }

    return (size_t)val;
}

int main(int argc, const char **argv) {
    if (argc < 3) exit_with_err_msg(USAGE);

    size_t m = get_m_arg(argv); // number of verifiers
    size_t n = (size_t)argc-2; // number of consumers
    struct queue inter;
    struct queue fin;
    // Containers for passing info to threads
    struct reader_tdata rs[n];
    struct verifier_tdata vs[m];
    
    // (to_initialize, queue_size, nprod, ncons)
    queue_init(&inter, INTERSIZ, n, m);
    queue_init(&fin, FINSIZ, m, 1);

    printf("[MAIN] creazione di %zu thread lettori e %zu thread verificatori\n", n, m);
    size_t i;
    // For each text file, a reader thread will be created
    for (i = 0; i < n; i++) {
        pthread_t tid;
        rs[i].path = argv[i+2];
        rs[i].q = &inter;
        rs[i].i = i;
        int err = pthread_create(&tid, NULL, reader_tfunc, (void*)(rs+i));
        if (err != 0) exit_with_err("pthread_create", err);
        err = pthread_detach(tid);
        if (err != 0) exit_with_err("pthread_detach", err);
    }
    // 'm' verifiers will be created
    for (i = 0; i < m; i++) {
        pthread_t tid;
        vs[i].inter = &inter;
        vs[i].fin = &fin;
        vs[i].i = i;
        int err = pthread_create(&tid, NULL, verifier_tfunc, (void*)(vs+i));
        if (err != 0) exit_with_err("pthread_create", err);
        err = pthread_detach(tid);
        if (err != 0) exit_with_err("pthread_detach", err);
    }

    // Main will wait on final queue for verified
    // magic square
    long **l = NULL;
    while ((l = queue_remove(&fin))) {
        flockfile(stdout);
        long tot = 0;
        size_t set = 0;
        // Different printing style
        printf("[MAIN] quadrato magico trovato:\n");
        for (size_t i = 0; i < X; i++) {
            printf("\t(");
            for (size_t j = 0; j < X; j++) {
                if (j == X-1) printf("%ld)\n", l[i][j]);
                else printf("%ld, ", l[i][j]);
                if (set++ < 3) tot+=l[i][j];
            }
            free(l[i]);
        }
        free(l);
        printf("\ttotale magico: %ld\n", tot);
        funlockfile(stdout);
    }

    free(inter.buf);
    free(fin.buf);
    pthread_exit(NULL);
}
