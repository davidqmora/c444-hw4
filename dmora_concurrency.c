#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <semaphore.h>
#include <string.h>


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
const int PROD_CON_MAX_SLEEP_TIME_MS = 50;

// This function based on https://stackoverflow.com/a/1157217
void random_sleep(long max_sleep_time_ms) {
    struct timespec timespec;

    long period = random() % max_sleep_time_ms;
    timespec.tv_sec = period / 1000;
    timespec.tv_nsec = (period % 1000) * (long)1E6;

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
// buffer configuration. This data must live in the heap so that threads
// can share it.
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
        random_sleep(PROD_CON_MAX_SLEEP_TIME_MS);

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

    printf("Producer %d exiting.\n", my_id);

    return NULL;
}

void* consume(void* id) {
    int my_id = *(int*)id;
    printf("Starting consumer %d\n", my_id);

    while (!TerminationRequested) {
        random_sleep(PROD_CON_MAX_SLEEP_TIME_MS);

        pthread_mutex_lock(&Mutex);
        if (queue_empty()) {
            printf("Consumer %d, queue empty, waiting...\n", my_id);
            pthread_cond_wait(&QueueEmpty, &Mutex);
        }
        int value = queue_pop();
        pthread_mutex_unlock(&Mutex);
        pthread_cond_signal(&QueueFull);

        printf("Consumer %d, value: %d\n", my_id, value);
    }

    printf("Consumer %d exiting.\n", my_id);

    return NULL;
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

    // Then the producers.
    for (int i = 0; i < ProducerCount; i++) {
        pthread_create(&producers[i], NULL, produce, (void *) &i);
    }

    // At this point, this function has nothing else to do. So the logical
    // next step is to make it wait for the threads to finish their work.
    // The join call below will block the main thread, which is exactly what
    // I want.

    // Normally, this would be the right order:
    //    Stop the producers, so the consumers have a chance to dry up the
    //    queue. Then stop the consumers.
    // In this case, I am keeping it simple and just stopping everything
    // at the same time. Nonetheless, I preserve the order for the sake of example.
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

// This solution implements the four righties and one leftie strategy discussed in
// The Little Book of Semaphores.

#define FORK_COUNT 5
#define PHILOSOPHER_COUNT 5

// The forks will be the resources that the diners share/compete for. Since each
// philosopher is a different thread, the forks need to be in the heap.
// By the problem definition, we have five forks.
// I use semaphores since that seems to be the convention, though in this case,
// its maximum value is 1, so I could, just the same, have used mutexes.
sem_t Forks[FORK_COUNT];

const long MINIMUM_THINKING_SECS = 1;
const long MAXIMUM_THINKING_SECS = 20 - MINIMUM_THINKING_SECS;
const long MINIMUM_EATING_SECS = 2;
const long MAXIMUM_EATING_SECS = 9 - MINIMUM_EATING_SECS;

void think(int id) {
    printf("Philosopher %d, thinking...\n", id);
    sleep(MINIMUM_THINKING_SECS);
    random_sleep(MAXIMUM_THINKING_SECS * 1000);
}

void eat(int id) {
    printf("Philosopher %d, eating\n", id);
    sleep(MINIMUM_EATING_SECS);
    random_sleep(MAXIMUM_EATING_SECS);
}

void get_forks(int id, int left_fork, int right_fork) {
    printf("Philosopher %d, getting fork %d\n", id, right_fork);
    sem_wait(&Forks[right_fork]);
    printf("Philosopher %d, getting fork %d\n", id, left_fork);
    sem_wait(&Forks[left_fork]);
}

void put_down_forks(int id, int left_fork, int right_fork) {
    printf("Philosopher %d, yielding fork %d\n", id, right_fork);
    sem_post(&Forks[right_fork]);
    printf("Philosopher %d, yielding fork %d\n", id, left_fork);
    sem_post(&Forks[left_fork]);
}

void* think_then_eat(void* id) {
    int my_id = *(int*)id;
    printf("Philosopher %d sitting at table.\n", my_id);

    // Figure out which forks we can get.
    int left_fork = my_id;
    int right_fork = (my_id + 1) % FORK_COUNT;

    // Philosopher 0 is a leftie, so his/her/their forks
    // need to be swapped.
    if (my_id == 0) {
        left_fork = right_fork;
        right_fork = my_id;
    }

    while (!TerminationRequested) {
        think(my_id);
        get_forks(my_id, left_fork, right_fork);
        eat(my_id);
        put_down_forks(my_id, left_fork, right_fork);
    }

     printf("Philosopher %d leaving the table.\n", my_id);
     return NULL;
}

void run_diners() {
    printf("Running Dining Philosophers.\n");

    // Prepare the forks, i.e. the semaphores.
    for (int i = 0; i < FORK_COUNT; i++) {
        sem_init(&Forks[i], 0, 1);
    }

    int id[] = {0, 1, 2, 3, 4, 5};
    pthread_t philosophers[PHILOSOPHER_COUNT];
    for (int i = 0; i < PHILOSOPHER_COUNT; i++) {
        pthread_create(&philosophers[i], NULL, think_then_eat, (void *) &id[i]);
    }

    // Wait for philosophers to be done.
    for (int i = 0; i < PHILOSOPHER_COUNT; i++) {
        pthread_join(philosophers[i], NULL);
    }
}


//======================================================================================
//
//  Potion Brewers.
//
//======================================================================================

// This solution implements the pushers strategy for the Cigarette Smoker's Problem
// as presented in the Little Book of Semaphores. I prefer to call them brokers. Other
// elements of the problem have been named according to the nomenclature used in the
// class materials.

#define BREWER_COUNT 3

char* INGREDIENT_NAMES[] = { "Bezoars", "Unicorn Horns", "Mistletoe Berries"};

struct Ingredient {
    char* name;
    sem_t* flag;
    bool* is_available;
};

struct BrewerInfo {
    int id;

    struct Ingredient* ingredient;
    sem_t* agent;
};

struct AgentInfo {
    int id;
    struct Ingredient* ingredient1;
    struct Ingredient* ingredient2;
    sem_t* agent;
};

struct BrokerInfo {
    int id;

    struct Ingredient* broker_ingredient;
    struct Ingredient* ingredient1;
    struct Ingredient* ingredient2;

    pthread_mutex_t* mutex;
};

//sem_t Agent;
//sem_t Bezoars;
//sem_t Horns;
//sem_t Berries;
//
//sem_t BezoarsBrewer;
//sem_t HornsBrewer;
//sem_t BerriesBrewer;
//
//bool HornsAvailable = false;
//bool BezoarsAvailable = false;
//bool BerriesAvailable = false;


// This code is based directly on the pusher solution
// shown in the Little Book of Semaphores.

//void* bezoars_broker(void *) {
//    sem_wait(&Bezoars);
//
//    pthread_mutex_lock(&Mutex);
//
//    if (HornsAvailable) {
//        HornsAvailable = false;
//        sem_post(&Horns);
//    } else if (BerriesAvailable) {
//        BerriesAvailable = false;
//        sem_post(&Berries);
//    } else {
//        BezoarsAvailable = true;
//    }
//
//    pthread_mutex_unlock(&Mutex);
//}
//
//void* horns_broker(void *) {
//    sem_wait(&Horns);
//
//    pthread_mutex_lock(&Mutex);
//
//    if (BezoarsAvailable) {
//        BezoarsAvailable = false;
//        sem_post(&Bezoars);
//    } else if (BerriesAvailable) {
//        BerriesAvailable = false;
//        sem_post(&Berries);
//    } else {
//        HornsAvailable = true;
//    }
//
//    pthread_mutex_unlock(&Mutex);
//}
//
//void* berries_broker(void *) {
//    sem_wait(&Berries);
//
//    pthread_mutex_lock(&Mutex);
//
//    if (HornsAvailable) {
//        HornsAvailable = false;
//        sem_post(&Horns);
//    } else if (BezoarsAvailable) {
//        BezoarsAvailable = false;
//        sem_post(&Berries);
//    } else {
//        BerriesAvailable = true;
//    }
//
//    pthread_mutex_unlock(&Mutex);
//}

void* broker_ingredients(void* broker_info) {
    struct BrokerInfo* broker = (struct BrokerInfo*)broker_info;

    printf("Broker %d starting.", broker->id);

    sem_wait(broker->broker_ingredient->flag);

    pthread_mutex_lock(broker->mutex);

    if (broker->ingredient1->is_available) {
        broker->ingredient1->is_available = false;
        sem_post(broker->ingredient1->flag);
    } else if (broker->ingredient2->is_available) {
        broker->ingredient2->is_available = false;
        sem_post(broker->ingredient2->flag);
    } else {
        broker->broker_ingredient->is_available = (bool *) true;
    }

    pthread_mutex_unlock(broker->mutex);
}

void* release_ingredients(void* agent_info) {
    struct AgentInfo* agent = (struct AgentInfo*)agent_info;

    printf("Agent %d opening shop.", agent->id);

    while (!TerminationRequested) {
        sem_wait(agent->agent);

        printf("Agent %d releasing ingredients.", agent->id);
        sem_post(agent->ingredient1->flag);
        sem_post(agent->ingredient2->flag);
    }

    printf("Agent %d opening shop.", agent->id);
}

void* brew(void* brewer_info) {
    struct BrewerInfo* brewer = (struct BrewerInfo*)brewer_info;

    printf("BrewerInfo %d opening shop.", brewer->id);

    while (!TerminationRequested) {
        sem_wait(brewer->ingredient->flag);
        printf("BrewerInfo %d brewing potion.", brewer->id);
        sem_post(brewer->agent);
        printf("BrewerInfo %d using potion.", brewer->id);
    }

    printf("BrewerInfo %d closing shop.", brewer->id);
}

void initialize_ingredients(struct Ingredient* ingredients) {
    for (int i = 0; i < 3; i++) {
        ingredients[i].flag = (sem_t *) malloc(sizeof(sem_t));
        sem_init(ingredients[i].flag, 0, 0);

        ingredients[i].is_available = (bool *) malloc(sizeof(bool));
        *(ingredients[i].is_available) = false;

        ingredients[i].name = (char*)malloc(sizeof(char) * strlen(INGREDIENT_NAMES[i]));
        strcpy(ingredients[i].name, INGREDIENT_NAMES[i]);
    }
}

void run_brewers() {
    printf("Running Potion Brewers.\n");

    sem_t agent;
    sem_init(&agent, 0, 0);

    struct Ingredient* ingredients = (struct Ingredient*)malloc(sizeof(struct Ingredient) * 3);
    initialize_ingredients(ingredients);

    struct AgentInfo agent_info[3];
    struct BrewerInfo brewer_info[3];
    struct BrokerInfo broker_info[3];

    pthread_t brewers[3];
    pthread_t agents[3];
    pthread_t brokers[3];

    for (int i = 0; i < 3; i++) {
        brewer_info[i].id = i;
        brewer_info[i].ingredient = &ingredients[i];
        brewer_info[i].agent = &agent;

        agent_info[i].id = i;
        agent_info[i].agent = &agent;

        int broker_ingredient = i;
        int ingredient1 = (i + 1) % 3;
        int ingredient2 = (i + 2) % 3;

        broker_info[i].mutex = &Mutex;
        broker_info[i].id = i;
        broker_info[i].broker_ingredient = &ingredients[broker_ingredient];
//        broker_info[i].ingredient_1_available = &ingredients_available[ingredient1];
//        broker_info[i].ingredient_2_available = &ingredients_available[ingredient2];
//        broker_info[i].flag_ingredient = &ingredient_flags[broker_ingredient];
        broker_info[i].ingredient1 = &ingredients[ingredient1];
        broker_info[i].ingredient1 = &ingredients[ingredient2];

        pthread_create(&agents[i], NULL, release_ingredients, (void *) &agent_info[i]);
        pthread_create(&brokers[i], NULL, broker_ingredients, (void *) &broker_info[i]);
        pthread_create(&brewers[i], NULL, brew, (void *) &brewer_info[i]);
    }

    // Wait for everyone to be done.
    for (int i = 0; i < 3; i++) {
        pthread_join(brewers[i], NULL);
        sem_destroy(ingredients[i].flag);
    }

    sem_destroy(&agent);
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
    printf("  -b: Potion BrewerInfo's solution\n");
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
