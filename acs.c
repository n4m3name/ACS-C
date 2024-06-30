#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#define MAX_CUSTOMERS 100
#define NUM_CLERKS 5

// Global variable to store the start time of the simulation
struct timeval start_time;

// Structure to represent a customer
struct Customer {
    int id;                 // Customer ID
    int class;              // 0 for economy, 1 for business
    int arrival_time;       // Arrival time in tenths of a second
    int service_time;       // Service time in tenths of a second
    pthread_mutex_t mutex;  // Mutex for customer synchronization
    pthread_cond_t cond;    // Condition variable for customer synchronization
    int being_served;       // Flag to indicate if the customer is being served
};

// Structure to represent a queue
struct Queue {
    struct Customer* customers[MAX_CUSTOMERS]; // Array of pointers to customers
    int front;                                 // Index of the front of the queue
    int rear;                                  // Index of the rear of the queue
    int count;                                 // Number of customers in the queue
    pthread_mutex_t mutex;                     // Mutex for queue synchronization
    pthread_cond_t cond;                       // Condition variable for queue synchronization
};

// Structure to represent a clerk
struct Clerk {
    int id;                // Clerk ID
    pthread_cond_t cond;   // Condition variable for clerk synchronization
};

// Global variables for queues and clerks
struct Queue business_queue, economy_queue;
struct Clerk clerks[NUM_CLERKS];
pthread_mutex_t clerk_mutex; // Mutex for synchronizing access to clerks

// Flags and counters for simulation
int simulation_done = 0;
int customers_served = 0;
double total_wait_time = 0;
double business_wait_time = 0;
double economy_wait_time = 0;

// Function to initialize the simulation start time
void init_simulation() {
    gettimeofday(&start_time, NULL);
}

// Function to get the elapsed time since the simulation started
double get_elapsed_time() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - start_time.tv_sec) + (now.tv_usec - start_time.tv_usec) / 1000000.0;
}

// Function to initialize a queue
void init_queue(struct Queue* q) {
    q->front = 0;
    q->rear = -1;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

// Function to add a customer to a queue
void enqueue(struct Queue* q, struct Customer* customer) {
    pthread_mutex_lock(&q->mutex);
    q->rear = (q->rear + 1) % MAX_CUSTOMERS;
    q->customers[q->rear] = customer;
    q->count++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

// Function to remove a customer from a queue
struct Customer* dequeue(struct Queue* q) {
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    struct Customer* customer = q->customers[q->front];
    q->front = (q->front + 1) % MAX_CUSTOMERS;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
    return customer;
}

// Function for customer threads
void* customer_thread(void* arg) {
    struct Customer* customer = (struct Customer*)arg;
    // Sleep until the customer's arrival time
    usleep(customer->arrival_time * 100000); // Convert to microseconds
    double arrival_time = get_elapsed_time();
    printf("Customer %d (Class %d) arrived at time %.2f\n", customer->id, customer->class, arrival_time);

    // Determine the appropriate queue based on customer class
    struct Queue* queue = customer->class ? &business_queue : &economy_queue;
    pthread_mutex_lock(&queue->mutex);
    enqueue(queue, customer);
    printf("Customer %d enters queue %d (length %d) at time %.2f\n", customer->id, customer->class, queue->count, get_elapsed_time());
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    // Wait to be served by a clerk
    pthread_mutex_lock(&customer->mutex);
    while (!customer->being_served) {
        pthread_cond_wait(&customer->cond, &customer->mutex);
    }
    pthread_mutex_unlock(&customer->mutex);

    // Customer has been served and can now exit
    printf("Customer %d exits at time %.2f\n", customer->id, get_elapsed_time());
    return NULL;
}

// Function for clerk threads
void* clerk_thread(void* arg) {
    int clerk_id = *(int*)arg;
    while (!simulation_done) {
        struct Customer* customer = NULL;
        pthread_mutex_lock(&clerk_mutex);
        // Serve business class customers first
        if (business_queue.count > 0) {
            customer = dequeue(&business_queue);
        } else if (economy_queue.count > 0) {
            customer = dequeue(&economy_queue);
        } else {
            pthread_cond_wait(&clerks[clerk_id].cond, &clerk_mutex);
        }
        pthread_mutex_unlock(&clerk_mutex);

        if (customer) {
            double start_service_time = get_elapsed_time();
            printf("Clerk %d starts serving Customer %d (Class %d) at time %.2f\n", clerk_id, customer->id, customer->class, start_service_time);

            // Signal the customer that they are being served
            pthread_mutex_lock(&customer->mutex);
            customer->being_served = 1;
            pthread_cond_signal(&customer->cond);
            pthread_mutex_unlock(&customer->mutex);

            // Simulate the service time
            usleep(customer->service_time * 100000); // Convert to microseconds
            double end_service_time = get_elapsed_time();
            printf("Clerk %d finishes serving Customer %d at time %.2f\n", clerk_id, customer->id, end_service_time);

            // Update waiting time statistics
            pthread_mutex_lock(&clerk_mutex);
            customers_served++;
            double wait_time = start_service_time - customer->arrival_time / 10.0; // Convert to seconds
            total_wait_time += wait_time;
            if (customer->class) {
                business_wait_time += wait_time;
            } else {
                economy_wait_time += wait_time;
            }
            pthread_mutex_unlock(&clerk_mutex);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    // Open the input file
    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        printf("Error opening file\n");
        return 1;
    }

    int num_customers;
    fscanf(file, "%d", &num_customers);

    // Arrays to store customer and clerk threads
    pthread_t customer_threads[MAX_CUSTOMERS], clerk_threads[NUM_CLERKS];
    int clerk_ids[NUM_CLERKS];
    struct Customer customers[MAX_CUSTOMERS];

    // Initialize queues and mutexes
    init_queue(&business_queue);
    init_queue(&economy_queue);
    pthread_mutex_init(&clerk_mutex, NULL);

    // Create clerk threads
    for (int i = 0; i < NUM_CLERKS; i++) {
        clerk_ids[i] = i;
        pthread_cond_init(&clerks[i].cond, NULL);
        pthread_create(&clerk_threads[i], NULL, clerk_thread, &clerk_ids[i]);
    }

    // Initialize the simulation start time
    init_simulation();

    // Read customer information from the input file and create customer threads
    for (int i = 0; i < num_customers; i++) {
        fscanf(file, "%d:%d,%d,%d", &customers[i].id, &customers[i].class, &customers[i].arrival_time, &customers[i].service_time);
        pthread_mutex_init(&customers[i].mutex, NULL);
        pthread_cond_init(&customers[i].cond, NULL);
        customers[i].being_served = 0;
        pthread_create(&customer_threads[i], NULL, customer_thread, &customers[i]);
    }

    // Close the input file
    fclose(file);

    // Wait for all customer threads to finish
    for (int i = 0; i < num_customers; i++) {
        pthread_join(customer_threads[i], NULL);
    }

    // Signal clerks to finish and wait for all clerk threads to finish
    simulation_done = 1;
    for (int i = 0; i < NUM_CLERKS; i++) {
        pthread_cond_signal(&clerks[i].cond);
        pthread_join(clerk_threads[i], NULL);
    }

    // Print simulation results
    printf("Simulation complete.\n");
    printf("Average wait time for all customers: %.2f seconds\n", total_wait_time / customers_served);
    printf("Average wait time for business class: %.2f seconds\n", business_wait_time / customers_served);
    printf("Average wait time for economy class: %.2f seconds\n", economy_wait_time / customers_served);

    return 0;
}
