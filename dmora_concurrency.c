#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>


//======================================================================================
//
//  Thead and signal management
//
//======================================================================================

pthread_mutex_t Mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  QueueEmpty = PTHREAD_COND_INITIALIZER;
pthread_cond_t  QueueFull = PTHREAD_COND_INITIALIZER;

// This flag is used to signal the currently running task (and all its spawned
// threads) that it is time top close up shop.
// This flag is set when SIGTERM is received.
static volatile sig_atomic_t TerminationRequested = 0;

static void sig_handler(int _)
{
    (void)_;
    TerminationRequested = 1;
}


//======================================================================================
//
//  Ancillary functions
//
//======================================================================================
const int MAX_SLEEP_TIME_MS = 50;

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
//  Producer/Consumer.
//
//======================================================================================

#define QUEUE_SIZE 100

int ProducerCount = 0;
int ConsumerCount = 0;


//========================================================
// Queue
//========================================================

// The Producers/Consumers  "queue" implemented as a simple array in a ring
// buffer configuration.
//
int PCQueue[QUEUE_SIZE];

int Q_Head = 0;
int Q_Tail = 0;

const int QUEUE_END = QUEUE_SIZE - 1;

// To simulate a more non-deterministic behavior for producers and consumers, I will
// add a random sleep period, then each actor will try to either add a datum to the
// "queue" or remove one from it.

// The queue is full when one of two conditions is true:
//  - The head is at position 0 and the tail is at QUEUE_SIZE - 1
//  - The tail is within one element of the head.
bool queue_full() {
    return (Q_Head == 0 && Q_Tail == QUEUE_END) || (Q_Tail == (Q_Head - 1));
}

// The queue is empty if the head is outside the queue. That is, is negative.
bool queue_empty() {
    return Q_Head < 0;
}

void queue_push(int value) {
    if (Q_Head < 0) {
        Q_Head = 0;
        Q_Tail = 0;
    } else if (Q_Head > 0 && Q_Tail == QUEUE_END) {
        Q_Tail = 0;
    } else {
        Q_Tail++;
    }

    PCQueue[Q_Tail]  = value;
}

int queue_pop() {
    int value = PCQueue[Q_Head];

    if (Q_Head == Q_Tail) {
        Q_Head = -1;
        Q_Tail = -1;
    } else if (Q_Head == QUEUE_END) {
        Q_Head = 0;
    } else {
        Q_Head++;
    }

    return value;
}


//========================================================
// Producer/Consumer tasks
//========================================================

// This is the "contents" produced and consumed. Just a simple counter.
int Count = 0;

void* produce(void* id) {
    int my_id = *(int*)id;
    printf("Starting consumer %d\n", my_id);

    while (!TerminationRequested) {
        random_sleep();

        pthread_mutex_lock(&Mutex);
        if (queue_full()) {
            printf("Producer %d, queue full, waiting...\n", my_id);
            pthread_cond_wait(&QueueFull, &Mutex);
        }

        int value = Count++;
        queue_push(value);
        pthread_mutex_unlock(&Mutex);
        pthread_cond_signal(&QueueEmpty);

        printf("Producer %d, value: %d\n", my_id, value);
    }
}

void* consume(void* id) {
    int my_id = *(int*)id;
    printf("Starting consumer %d\n", my_id);

    while (!TerminationRequested) {
        random_sleep();

        int value = -1;
        pthread_mutex_lock(&Mutex);
        if (queue_empty()) {
            printf("Consumer %d, queue empty, waiting...\n", my_id);
            pthread_cond_wait(&QueueEmpty, &Mutex);
        }
        value = queue_pop();
        pthread_mutex_unlock(&Mutex);
        pthread_cond_signal(&QueueFull);

        printf("Consumer %d, value: %d\n", my_id, value);
    }
}


//========================================================
// Producer/Consumer runner
//========================================================

void run_prodcon() {
    printf("Running Producer/Consumer with %d producers and %d consumers.\n", ProducerCount, ConsumerCount);

    pthread_t producers[ProducerCount];
    pthread_t consumers[ConsumerCount];

    // Start consumers first, to avoid choking the queue.
    for (int i = 0; i < ConsumerCount; i++) {
        pthread_create(&consumers[i], NULL, consume, (void *) &i);
    }
    for (int i = 0; i < ProducerCount; i++) {
        pthread_create(&producers[i], NULL, produce, (void *) &i);
    }

    while (!TerminationRequested);

    for (int i = 0; i < ProducerCount; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < ConsumerCount; i++) {
        pthread_join(consumers[i], NULL);
    }
}


//======================================================================================
//
//  Dining Philosophers.
//
//======================================================================================

void run_diners() {
    printf("Running Dining Philosophers.\n");
}

void run_brewers() {
    printf("Running Potion Brewers.\n");
}


//======================================================================================
//
//  MAIN: command line parser, usage help, top level routine.
//
//======================================================================================

//========================================================
// Concurrency Problem Management
//========================================================

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

//========================================================
// Command line parsing
//========================================================

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

//========================================================
// Help messages and such.
//========================================================

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

//========================================================
// Top routine.
//========================================================

int main(int argc, char **argv) {
    signal(SIGINT, sig_handler);

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
