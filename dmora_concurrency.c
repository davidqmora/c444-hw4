#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>


//======================================================================================
//
//  Model is used to represent the problem that the executable will tackle
//  when run. This is specified by the user.
//
//======================================================================================

// Represents the concurrency problem to tackle:
//   - Producers/Consumers
//   - Dining Philosophers
//   - Potion Brewers
enum Model {
    ProdCon,
    Diners,
    Brewers,
    None
};

enum Model ProblemType = None;

char* ModelNames[] = {
        "Producers/Consumers",
        "Dining Philosophers",
        "Potion Brewers",
        "Invalid"
};

//======================================================================================
//
//  Thead management
//
//======================================================================================

// This flag is used to signal the currently running task (and all its spawned
// threads) that it is time top close up shop.
// This flag is set when SIGTERM is received.
bool TerminationRequested = false;


pthread_mutex_t Mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  QueueEmpty = PTHREAD_COND_INITIALIZER;
pthread_cond_t  QueueFull = PTHREAD_COND_INITIALIZER;


//======================================================================================
//
//  Ancillary functions
//
//======================================================================================
const int MAX_SLEEP_TIME_MS = 120;

// This function based on https://stackoverflow.com/a/1157217
long random_sleep() {
    struct timespec timespec;

    long period = random() % MAX_SLEEP_TIME_MS;
    timespec.tv_sec = period / 1000;
    timespec.tv_nsec = (period % 1000) * 1E6;

    nanosleep(&timespec, &timespec);
}


//======================================================================================
//
//  Producer/Consumer related items.
//
//======================================================================================
const int PRO_CON_QUEUE_SIZE = 100;

int ProducerCount = 0;
int ConsumerCount = 0;

int Queue[PRO_CON_QUEUE_SIZE];
int Q_Index = 0;

// To simulate a more non-deterministic behavior for producers and consumers, I will
// add a random sleep period, then each actor will try to either add a datum to the
// "queue" or remove one from it.

bool queue_full() {
    return Q_Index >= (PRO_CON_QUEUE_SIZE - 1);
}

bool queue_empty() {
    return Q_Index == 0;
}

void queue_push(int value) {
    Queue[Q_Index++]  = value;

    if (Q_Index >= PRO_CON_QUEUE_SIZE) {
        Q_Index = PRO_CON_QUEUE_SIZE - 1;
    }
}

int queue_pop() {
    int value = Queue[Q_Index--];

    if (Q_Index < 0) {
        Q_Index = 0;
    }

    return value;
}

void* produce(void* id) {
    printf("Starting consumer %d", *(int*)id);

    while (!TerminationRequested) {
        random_sleep();

        int value = 0;
        pthread_mutex_lock(&Mutex);
        if (queue_full()) {
            pthread_cond_wait(&QueueFull, &Mutex);
        }
        queue_push(value);
        pthread_mutex_unlock(&Mutex);
        pthread_cond_signal(&QueueEmpty);
    }
}

void* consume(void* id) {
    printf("Starting consumer %d", *(int*)id);

    while (!TerminationRequested) {
        random_sleep();

        pthread_mutex_lock(&Mutex);
        if (queue_empty()) {
            pthread_cond_wait(&QueueEmpty, &Mutex);
        }
        queue_pop();
        pthread_mutex_unlock(&Mutex);
        pthread_cond_signal(&QueueFull);
    }
}


void run_prodcon() {
    printf("Running Producer/Consumer with %d producers and %d consumers.\n", ProducerCount, ConsumerCount);

    pthread_t producers[ProducerCount];
    pthread_t consumers[ConsumerCount];

    // Start consumers first, to avoid choking the queue.
    for (int i = 0; i < ConsumerCount; i++) {
        pthread_create(&consumers[i], NULL, consume, (void *) &i);
    }
    for (int i = 0; i < ConsumerCount; i++) {
        pthread_create(&producers[i], NULL, produce, (void *) &i);
    }

    while (!TerminationRequested);

    for (int i = 0; i < ConsumerCount; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < ConsumerCount; i++) {
        pthread_join(consumers[i], NULL);
    }
}

void run_diners() {
    printf("Running Dining Philosophers.\n");
}

void run_brewers() {
    printf("Running Potion Brewers.\n");
}

void parse_command_line(int argc, char **argv) {
    int option;
    while ((option = getopt(argc, argv, "dbpn:c:")) != -1) {
        switch (option) {
            case 'd':
                ProblemType = Diners;
                break;

            case 'b':
                ProblemType = Brewers;
                break;

            case 'p':
                ProblemType = ProdCon;
                break;

            case 'n':
                ProducerCount = atoi(optarg);
                break;

            case 'c':
                ConsumerCount = atoi(optarg);
                break;

            case '?':
                switch (optopt) {
                    case 'n':
                    case 'c':
                        printf("Option %c requires a value.\n", optopt);
                        break;

                    default:
                        printf("Unknown option character %c.\n", optopt);
                        ProblemType = None;
                        return;
                }
                break;

            default:
                ProblemType = None;
        }
    }

    if  (ProblemType == ProdCon && (ProducerCount == 0 || ConsumerCount == 0)) {
        printf("For the Producer/Consumer, both the -n and -c commands must be "
               "present and each followed by an integer value greater than zero.\n");
        ProblemType = None;
    }

    if ((ProblemType == Diners || ProblemType == Brewers && argc > 2) || (ProblemType == ProdCon && argc > 6)) {
        printf("Solution set to %s, extra parameters passed will be ignored.\n", ModelNames[ProblemType]);
    }
}

void print_help(char *exe_name) {
    printf("Usage:\n");
    printf("%s <Mode>\n", exe_name);
    printf("Mode is one of: \n");
    printf("  -d: Dining Philosopher's solution \n");
    printf("  -b: Potion Brewer's solution\n");
    printf("  -p: Producer/Consumer solution\n");
    printf("      Required arguments for Producer/Consumer solution:\n");
    printf("      -n: Number of producers to instantiate\n");
    printf("      -c: Number of consumes to instantiate\n\n");
    printf("If multiple modes are specified, the last one in the command line overrides the others.\n");
}

void print_heading() {
    printf("Concurrency problem runner.\n");
    printf("David Mora - 933-324-249\n\n");
}

int main(int argc, char **argv) {
    ProblemType = None;

    if (argc <= 1) {
        print_heading();
        print_help(argv[0]);
        return -1;
    }

    parse_command_line(argc, argv);

    srandom(time(0));

    switch (ProblemType) {
        case ProdCon:
            run_prodcon();
            break;

        case Diners:
            run_diners();
            break;

        case Brewers:
            run_brewers();
            break;

        default:
            printf("No valid mode chosen or the parameters are incorrect.\n\n");
            print_help(argv[0]);
    }

    return ProblemType == None ? -1 : 0;
}
