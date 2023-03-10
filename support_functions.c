#include <stdio.h>
#include <stdlib.h>
#include <getopt.h> //solo perché vscode da errore su optarg
#include <string.h>
#include <errno.h>
#include <gd.h>
#include <omp.h>

#define NUM_THREADS 4
#define HORIZON 1000
#define FACTOR 1.5

unsigned int gen_rand = 586761;
int seed_rand = 586761;

/*
 * Struttura per la posizione di una particella.
 */
typedef struct
{
    int x; // posizione sull'asse x
    int y; // posizione sull'asse y
} position;

/*
 * Struttura per la particella.
 * Contiene la posizione attuale, la direzione, lo stato di stuck e lo stato di uscita.
 * Lo stato di stuck indica se la particella è bloccata o meno.
 * Lo stato di uscita indica se la particella è uscita dalla matrice o meno.
 */
typedef struct
{
    position *current_position; // posizione attuale della particella

    int dire; // direzione

    int stuck; // 0 = not stuck, 1 = stuck
    int isOut; // 0 = inside, 1 = outside
} particle;

/*
 * Struttura per la lista di particelle bloccate.
 * Contiene un array di particelle, la dimensione dell'array e la capacità massima dell'array.
 */
typedef struct
{
    particle *data; // array di particelle
    int size;       // numero di particelle
    int capacity;   // capacità massima dell'array
} stuckedParticles;

void get_args_parallel(int argc, char *argv[], int *num_particles, int *n, int *m, int *seed, int *num_threads, int *horizon);
void get_args(int argc, char *argv[], int *num_particles, int *n, int *m, int *seed, int *horizon);
void write_matrix(int n, int m, int **matrix);
void print_matrix(int n, int m, int **matrix);
int check_position_openMP(int n, int m, int **matrix, particle *p, stuckedParticles *sp);
int check_position(int n, int m, int **matrix, particle *p, stuckedParticles *sp);
void move(particle *p, int n, int m);
void move_parallel(particle *p, int n, int m);
double get_time(void);
int init_StuckedParticles(stuckedParticles *sp, int capacity);
int sp_append(stuckedParticles *sp, particle p);
int sp_append_openMP(stuckedParticles *sp, particle p);
particle sp_pop(stuckedParticles *sp);
int sp_destroy(stuckedParticles *sp);
void createImage(gdImagePtr img, int width, int height, int **matrix, int *colors, char *filename);

// nodo della lista
typedef struct node
{
    particle p;
    struct node *next;
    struct node *prev;
} node;

typedef struct
{
    struct node *head;
    struct node *tail;
    int size;
} list;

void init_list(list *l);
void append(list *l, particle p);
particle pop(list *l);

void init_list(list *l)
{
    l->head = NULL;
    l->tail = NULL;
    l->size = 0;
}

void append(list *l, particle p)
{
    node *new_node = (node *)malloc(sizeof(node));
    new_node->p = p;
    new_node->next = NULL;
    new_node->prev = NULL;

    if (l->head == NULL)
    {
        l->head = new_node;
        l->tail = new_node;
    }
    else
    {
        l->tail->next = new_node;
        new_node->prev = l->tail;
        l->tail = new_node;
    }
    l->size++;
}

particle pop(list *l)
{
    if (l->size == 0)
    {
        return (particle){NULL, 0, 0, 0};
    }
    particle p = l->tail->p;
    node *tmp = l->tail;
    l->tail = l->tail->prev;
    if (l->tail != NULL)
    {
        l->tail->next = NULL;
    }
    free(tmp);
    l->size--;
    return p;
}

/*
 * Inizializza la struttura stuckedParticles.
 * @param stuckedParticles struttura da inizializzare
 * @param capacity capacità massima dell'array
 * @return 0 se l'inizializzazione è andata a buon fine, -1 altrimenti
 */
int init_StuckedParticles(stuckedParticles *sp, int capacity)
{
    sp->data = (particle *)calloc(capacity + 1, sizeof(particle));
    if (sp->data == NULL)
    {
        return -1;
    }
    sp->size = 0;
    sp->capacity = (int)capacity + 1;
    return 0;
}

/*
 * Aggiunge una particella alla lista, se la lista è piena viene riallocata la memoria.
 * @param stuckedParticles struttura a cui aggiungere la particella
 * @param particle particella da aggiungere
 * @return 0 se l'aggiunta è andata a buon fine, -1 altrimenti
 */
int sp_append(stuckedParticles *sp, particle p)
{
    if (sp->size == sp->capacity - 1)
    {
        perror(" Errore nell'append \n");
        exit(10);
    }
    sp->data[sp->size + 1] = p;
    sp->size++;

    return 0;
}

/*
 * Aggiunge una particella alla lista, se la lista è piena viene riallocata la memoria.
 * @param stuckedParticles struttura a cui aggiungere la particella
 * @param particle particella da aggiungere
 * @return 0 se l'aggiunta è andata a buon fine, -1 altrimenti
 */
int sp_append_openMP(stuckedParticles *sp, particle p)
{
    if (sp->size == sp->capacity - 1)
    {
        perror(" Errore nell'append \n");
        exit(10);
    }
    #pragma omp critical
    {
    sp->data[sp->size + 1] = p;
    sp->size++;
    }
    return 0;
}


/*
 * Rimuove l'ultima particella inserita nella struttura e la restituisce.
 * @param stuckedParticles struttura da cui rimuovere la particella
 * @return particella rimossa, in caso di errore restituisce una particella con tutti i campi a 0
 */
particle sp_pop(stuckedParticles *sp)
{
    if (sp->size == 0)
    {
        return (particle){NULL, 0, 0, 0};
    }
    particle p = sp->data[sp->size];
    sp->size--;
    return p;
}

/*
 * Distrugge e libera la memoria occupata dalla struttura.
 * @param stuckedParticles struttura da distruggere
 * @return 0 se la distruzione è andata a buon fine, -1 altrimenti
 */
int sp_destroy(stuckedParticles *sp)
{
    if (sp->data != NULL)
    {
        free(sp->data);
        return 0;
    }
    else
    {
        return -1;
    }
}

/*
 * Recupera tutti gli argomenti passati in input al programma e li setta alle opportune variabili.
 * Prende come parametri le variabili da settare:
 * @param argc numero di argomenti passati in input
 * @param argv array di argomenti passati in input
 * @param num_particles numero di particelle
 * @param n numero di righe della matrice
 * @param m numero di colonne della matrice
 * @param seed posizione iniziale della particella
 * @param num_threads numero di threads
 * @param horizon numero di iterazioni massime
 */
void get_args_parallel(int argc, char *argv[], int *num_particles, int *n, int *m, int *seed, int *num_threads, int *horizon)
{
    // recupero le dimensioni della matrice
    char *token = strtok(argv[1], ",");
    *n = (int)atoi(token);
    token = strtok(NULL, ",");
    *m = (int)atoi(token);

    // recupero il numero di particelle
    *num_particles = (int)atoi(argv[2]);

    // genero una seed casuale - DEFAULT
    seed[0] = (int)rand_r(&gen_rand) % *n;
    seed[1] = (int)rand_r(&gen_rand) % *m;

    // setto il numero massimo di iterazioni - DEFAULT
    *horizon = HORIZON;

    // setto il numero di threads - DEFAULT
    *num_threads = NUM_THREADS;

    int opt;
    while ((opt = getopt(argc, argv, "-n:-t:-s:")) != -1)
    {
        switch (opt)
        {
        case 'n':
            // recupero il numero di threads - OPTION
            *num_threads = atoi(optarg);
            break;
        case 't':
            // recupero il numero massimo di iterazioni - OPTION
            *horizon = atoi(optarg);
            break;
        case 's':
            // recupero la posizione iniziale del seed - OPTION
            token = strtok(optarg, ",");
            seed[0] = (int)atoi(token);
            token = strtok(NULL, ",");
            seed[1] = (int)atoi(token);
            break;
        case '?':
            printf("Usage: %s [-n num_threads] [-t horizon] [-s seed] n,m num_particles \n", argv[0]);
            exit(2);
        }
    }
}

/*
 * Controlla tutti i possibili movimenti che potrebbe fare la particella in una superficie 2D.
 * @param n: numero di righe della matrice
 * @param m: numero di colonne della matrice
 * @param matrix: matrice di interi
 * @param p: particella
 * @param sp: lista di particelle bloccate
 * @return 0 se la particella non è rimasta bloccata, -1 se la particella è rimasta bloccata
 */
int check_position(int n, int m, int **matrix, particle *p, stuckedParticles *sp)
{
    if (p->isOut == 1)
    {
        return 0;
    }

    int directions[] = {0, 1, 0, -1, 1, 0, -1, 0, 1, 1, 1, -1, -1, 1, -1, -1};

    for (int i = 0; i < 8; i += 2)
    {
        int near_y = p->current_position->y + directions[i];
        int near_x = p->current_position->x + directions[i + 1];
        if (near_x >= 0 && near_x < m && near_y >= 0 && near_y < n)
        {
            if (matrix[near_y][near_x] == 1)
            {
                sp_append(sp, *p);
                p->stuck = 1;
                return -1;
            }
        }
    }
    return 0;
}


/*
 * Controlla tutti i possibili movimenti che potrebbe fare la particella in una superficie 2D.
 * @param n: numero di righe della matrice
 * @param m: numero di colonne della matrice
 * @param matrix: matrice di interi
 * @param p: particella
 * @param sp: lista di particelle bloccate
 * @return 0 se la particella non è rimasta bloccata, -1 se la particella è rimasta bloccata
 */
int check_position_openMP(int n, int m, int **matrix, particle *p, stuckedParticles *sp)
{
    if (p->isOut == 1)
    {
        return 0;
    }

    int directions[] = {0, 1, 0, -1, 1, 0, -1, 0, 1, 1, 1, -1, -1, 1, -1, -1};

    for (int i = 0; i < 8; i += 2)
    {
        int near_y = p->current_position->y + directions[i];
        int near_x = p->current_position->x + directions[i + 1];
        if (near_x >= 0 && near_x < m && near_y >= 0 && near_y < n)
        {
            if (matrix[near_y][near_x] == 1)
            {
                sp_append_openMP(sp, *p);
                p->stuck = 1;
                return -1;
            }
        }
    }
    return 0;
}

/*
 * Muove la particella in una direzione pseudocasuale, utilizza una reentrant random.
 * Se la particelle esce fuori dalla matrice viene settata la variabile isOut a 1.
 * Se la particella non esce fuori dalla matrice viene settata la variabile isOut a 0.
 * @param p puntatore alla particella interessata
 * @param n numero di righe della matrice
 * @param m numero di colonne della matrice
 */
void move_parallel(particle *p, int n, int m)
{

    // calcolo la nuova posoizione della particella
    p->dire = rand_r(&gen_rand) % 2 == 0 ? 1 : -1;
    p->current_position->x += rand_r(&gen_rand) % 2 * p->dire;

    p->dire = rand_r(&gen_rand) % 2 == 0 ? 1 : -1;
    p->current_position->y += rand_r(&gen_rand) % 2 * p->dire;

    // controllo se la particella è uscita dalla matrice o no
    if (!(p->current_position->x >= 0 && p->current_position->x < m && p->current_position->y >= 0 && p->current_position->y < n))
    {
        p->isOut = 1;
        return;
    }
    else
    {
        p->isOut = 0;
    }
}

// --------------- RENDER FUNCTIONS --------------- //
/*
 * Crea l'immagine e la salva su file.
 * @param img puntatore all'immagine
 * @param width larghezza dell'immagine
 * @param height altezza dell'immagine
 * @param matrix matrice contenente i valori da stampare
 * @param colors array contenente i colori da utilizzare
 * @param filename nome del file da salvare
 */
void createImage(gdImagePtr img, int width, int height, int **matrix, int *colors, char *filename)
{
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            if (matrix[y][x] == 1)
            {
                gdImageSetPixel(img, x, y, colors[0]); // set black
            }
            else if (matrix[y][x] > 1)
            {
                gdImageSetPixel(img, x, y, colors[1]); // set red
            }
        }
    }

    // Salva l'immagine
    FILE *out = fopen(filename, "wb");
    gdImageJpeg(img, out, 100);
    fclose(out);
}

// --------- DEBUG FUNCTIONS --------- //

/*
 * Scrive la matrice su un file di testo.
 * default filename `output/matrix.txt`. DEBUG
 * @param n numero di righe della matrice
 * @param m numero di colonne della matrice
 * @param matrix matrice da scrivere
 */
void write_matrix(int n, int m, int **matrix)
{
    FILE *fptr;

    fptr = fopen("output/matrix.txt", "w+");
    if (fptr == NULL)
        perror("Error opening file");

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            fprintf(fptr, "%d ", matrix[i][j]);
        }
        fprintf(fptr, "\n");
    }

    if (ferror(fptr))
        perror("Error writing file");

    // close file
    if (fclose(fptr))
        perror("Error closing file");
}

/*
 * Stampa la matrice su standard output. DEBUG
 * @param n numero di righe della matrice
 * @param m numero di colonne della matrice
 * @param matrix matrice da stampare
 */
void print_matrix(int n, int m, int **matrix)
{
    int a, b;
    for (a = 0; a < n; a++)
    {
        for (b = 0; b < m; b++)
        {
            printf("%d ", matrix[a][b]);
        }
        printf("\n");
    }
    printf("-----------------------------------------------------\n");
}