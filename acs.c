#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>

// Define constants for maximum number of customers and clerks
#define MAX_CUSTOMERS 100
#define NUM_CLERKS 5
// Define constants for customer class types
#define BUSINESS_CLASS 1
#define ECONOMY_CLASS 0

// Structure to hold customer information
struct customer_info {
    int user_id; // Unique identifier for each customer
    int class_type; // Customer class (business or economy)
    int service_time; // Time required to serve the customer
    int arrival_time; // Time at which the customer arrives
    double waiting_time; // Time the customer spends waiting in queue
};

// Structure to hold clerk information
struct clerk_info {
    int clerk_id; // Unique identifier for each clerk
    pthread_mutex_t mutex; // Mutex for thread-safe operations on clerk data
    pthread_cond_t cond; // Condition variable for signaling clerk availability
    int busy; // Flag to indicate if the clerk is currently serving a customer
};

// Global variables
struct timeval init_time; // Initial time when the program starts
double overall_waiting_time = 0; // Total waiting time for all customers
double business_waiting_time = 0; // Total waiting time for business class customers
double economy_waiting_time = 0; // Total waiting time for economy class customers
int business_customers = 0; // Count of business class customers
int economy_customers = 0; // Count of economy class customers
int num_customers = 0; // Total number of customers
int queue_length[2] = {0}; // Length of queues for each class
int queue_status[2] = {0}; // Status of queues (0 if no clerk available, clerk_id if available)
int winner_selected[2] = {0}; // Flag to indicate if a customer has been selected from the queue
volatile int program_running = 1; // Flag to control program execution

// Mutexes and condition variables for thread synchronization
pthread_mutex_t queue_mutex[2] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t waiting_time_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_convar[2] = {PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};

struct clerk_info clerks[NUM_CLERKS]; // Array to store information for all clerks
pthread_mutex_t clerk_assignment_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to select the next available clerk
int select_next_available_clerk() {
    for (int i = 0; i < NUM_CLERKS; i++) {
        if (!clerks[i].busy) {
            return i + 1; // Return clerk_id + 1 because clerk IDs start from 1
        }
    }
    return -1; // No available clerk
}

// Thread function for customer behavior
void *customer_entry(void *cus_info) {
    struct customer_info *p_myInfo = (struct customer_info *)cus_info;
    int class_type = p_myInfo->class_type;

    // Simulate customer arrival time
    usleep(p_myInfo->arrival_time * 100000);
    printf("A customer arrives: customer ID %2d.\n", p_myInfo->user_id);

    struct timeval queue_enter_time;
    gettimeofday(&queue_enter_time, NULL);
    
    int clerk_id = -1;
    double waiting_time = 0.0;

    // Try to get an available clerk
    pthread_mutex_lock(&clerk_assignment_mutex);
    clerk_id = select_next_available_clerk();

    if (clerk_id != -1) {
        clerks[clerk_id-1].busy = 1;
        pthread_mutex_unlock(&clerk_assignment_mutex);
    } else {
        pthread_mutex_unlock(&clerk_assignment_mutex);

        // No clerk available, enter the queue
        pthread_mutex_lock(&queue_mutex[class_type]);
        queue_length[class_type]++;
        printf("A customer enters a queue: the queue ID %1d, and length of the queue %2d.\n",
               class_type, queue_length[class_type]);

        // Wait until a clerk becomes available
        while (1) {
            pthread_cond_wait(&queue_convar[class_type], &queue_mutex[class_type]);
            if (queue_status[class_type] != 0 && !winner_selected[class_type]) {
                clerk_id = queue_status[class_type];
                queue_status[class_type] = 0;
                winner_selected[class_type] = 1;
                queue_length[class_type]--;
                break;
            }
        }
        pthread_mutex_unlock(&queue_mutex[class_type]);
    }
    
    // Calculate waiting time
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    waiting_time = (start_time.tv_sec - queue_enter_time.tv_sec) +
                   (start_time.tv_usec - queue_enter_time.tv_usec) / 1000000.0;
    
    // Update waiting time statistics
    pthread_mutex_lock(&waiting_time_mutex);
    overall_waiting_time += waiting_time;
    if (class_type == BUSINESS_CLASS) {
        business_waiting_time += waiting_time;
        business_customers++;
    } else {
        economy_waiting_time += waiting_time;
        economy_customers++;
    }
    pthread_mutex_unlock(&waiting_time_mutex);
    
    // Calculate and print service start time
    double relative_start_time = (start_time.tv_sec - init_time.tv_sec) +
                                 (start_time.tv_usec - init_time.tv_usec) / 1000000.0;
    printf("A clerk starts serving a customer: start time %.2f, the customer ID %2d, the clerk ID %1d.\n",
           relative_start_time, p_myInfo->user_id, clerk_id);
    
    // Simulate service time
    usleep(p_myInfo->service_time * 100000);
    
    // Calculate and print service end time
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    double relative_end_time = (end_time.tv_sec - init_time.tv_sec) +
                               (end_time.tv_usec - init_time.tv_usec) / 1000000.0;
    printf("A clerk finishes serving a customer: end time %.2f, the customer ID %2d, the clerk ID %1d.\n",
           relative_end_time, p_myInfo->user_id, clerk_id);
    
    // Mark clerk as available
    pthread_mutex_lock(&clerk_assignment_mutex);
    clerks[clerk_id-1].busy = 0;
    pthread_mutex_unlock(&clerk_assignment_mutex);
    
    pthread_exit(NULL);
}




// Thread function for clerk behavior
void *clerk_entry(void *clerkNum) {
    int clerk_id = (int)(intptr_t)clerkNum;
    while (program_running) {
        int selected_queue = -1;
        
        // Check business class queue first
        pthread_mutex_lock(&clerk_assignment_mutex);
        if (!clerks[clerk_id-1].busy) {
            // Check business class queue first
            pthread_mutex_lock(&queue_mutex[BUSINESS_CLASS]);
            if (queue_length[BUSINESS_CLASS] > 0) {
                selected_queue = BUSINESS_CLASS;
            }
            pthread_mutex_unlock(&queue_mutex[BUSINESS_CLASS]);
            
            // If no business class customers, check economy class
            if (selected_queue == -1) {
                pthread_mutex_lock(&queue_mutex[ECONOMY_CLASS]);
                if (queue_length[ECONOMY_CLASS] > 0) {
                    selected_queue = ECONOMY_CLASS;
                }
                pthread_mutex_unlock(&queue_mutex[ECONOMY_CLASS]);
            }
            
            // If a queue is selected, signal a customer
            if (selected_queue != -1) {
                pthread_mutex_lock(&queue_mutex[selected_queue]);
                queue_status[selected_queue] = clerk_id;
                winner_selected[selected_queue] = 0;
                pthread_cond_signal(&queue_convar[selected_queue]);
                pthread_mutex_unlock(&queue_mutex[selected_queue]);
                clerks[clerk_id-1].busy = 1;
            }
        }
        pthread_mutex_unlock(&clerk_assignment_mutex);
        
        usleep(10000); // 10ms wait before checking again
    }
    return NULL;
}


int main(int argc, char *argv[]) {
    // Check for correct command line arguments
    if (argc != 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        exit(1);
    }

    // Open and read input file
    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL) {
        printf("Could not open file %s\n", argv[1]);
        exit(1);
    }

    // Read number of customers
    fscanf(fp, "%d", &num_customers);
    struct customer_info customers[MAX_CUSTOMERS];

    // Read customer information from file
    int i = 0;
    while (i < num_customers && !feof(fp)) {
        int arrival_time, service_time;
        fscanf(fp, "%d:%d,%d,%d", &customers[i].user_id, &customers[i].class_type,
               &arrival_time, &service_time);

        // Validate input data
        if (arrival_time <= 0 || service_time <= 0) {
            printf("Error: Invalid arrival time or service time for customer %d\n", customers[i].user_id);
            exit(1);
        }
        customers[i].arrival_time = arrival_time;
        customers[i].service_time = service_time;
        i++;
    }
    fclose(fp);

    // Record initial time
    gettimeofday(&init_time, NULL);

    // Initialize clerk data structures
    int ret;
    for (i = 0; i < NUM_CLERKS; i++) {
        clerks[i].clerk_id = i + 1;
        clerks[i].busy = 0;
        
        ret = pthread_mutex_init(&clerks[i].mutex, NULL);
        if (ret != 0) {
            fprintf(stderr, "Error initializing mutex for clerk %d: %s\n", i, strerror(ret));
            exit(1);
        }
        
        ret = pthread_cond_init(&clerks[i].cond, NULL);
        if (ret != 0) {
            fprintf(stderr, "Error initializing condition variable for clerk %d: %s\n", i, strerror(ret));
            pthread_mutex_destroy(&clerks[i].mutex);
            exit(1);
        }
    }

    // Create clerk threads
    pthread_t clerk_threads[NUM_CLERKS];
    for (i = 0; i < NUM_CLERKS; i++) {
        int clerk_id = i + 1;
        ret = pthread_create(&clerk_threads[i], NULL, clerk_entry, (void *)(intptr_t)clerk_id);
        if (ret != 0) {
            fprintf(stderr, "Error creating clerk thread %d: %s\n", i, strerror(ret));
            exit(1);
        }
    }

    // Create customer threads
    pthread_t customer_threads[MAX_CUSTOMERS];
    for (i = 0; i < num_customers; i++) {
        ret = pthread_create(&customer_threads[i], NULL, customer_entry, (void *)&customers[i]);
        if (ret != 0) {
            fprintf(stderr, "Error creating customer thread %d: %s\n", i, strerror(ret));
            exit(1);
        }
    }

    // Wait for all customer threads to finish
    for (i = 0; i < num_customers; i++) {
        ret = pthread_join(customer_threads[i], NULL);
        if (ret != 0) {
            fprintf(stderr, "Error joining customer thread %d: %s\n", i, strerror(ret));
        }
    }
    
    // Signal program termination and wait for clerk threads to finish
    program_running = 0;
    for (i = 0; i < NUM_CLERKS; i++) {
        pthread_mutex_lock(&clerks[i].mutex);
        pthread_cond_signal(&clerks[i].cond);
        pthread_mutex_unlock(&clerks[i].mutex);
    }
    
    for (i = 0; i < NUM_CLERKS; i++) {
        ret = pthread_join(clerk_threads[i], NULL);
        if (ret != 0) {
            fprintf(stderr, "Error joining clerk thread %d: %s\n", i, strerror(ret));
        }
    }
    
    // Cleanup: Destroy mutexes and condition variables
    for (i = 0; i < 2; i++) {
        ret = pthread_mutex_destroy(&queue_mutex[i]);
        if (ret != 0) {
            fprintf(stderr, "Error destroying queue mutex %d: %s\n", i, strerror(ret));
        }
        
        ret = pthread_cond_destroy(&queue_convar[i]);
        if (ret != 0) {
            fprintf(stderr, "Error destroying queue condition variable %d: %s\n", i, strerror(ret));
        }
    }

    for (i = 0; i < NUM_CLERKS; i++) {
        ret = pthread_mutex_destroy(&clerks[i].mutex);
        if (ret != 0) {
            fprintf(stderr, "Error destroying clerk mutex %d: %s\n", i, strerror(ret));
        }
        
        ret = pthread_cond_destroy(&clerks[i].cond);
        if (ret != 0) {
            fprintf(stderr, "Error destroying clerk condition variable %d: %s\n", i, strerror(ret));
        }
    }

    pthread_mutex_destroy(&clerk_assignment_mutex);
    pthread_mutex_destroy(&waiting_time_mutex);
    
    // Print final statistics
    printf("The average waiting time for all customers in the system is: %.2f seconds.\n", 
           num_customers > 0 ? overall_waiting_time / (double)num_customers : 0.0);
    printf("The average waiting time for all business-class customers is: %.2f seconds.\n", 
           business_customers > 0 ? business_waiting_time / (double)business_customers : 0.0);
    printf("The average waiting time for all economy-class customers is: %.2f seconds.\n", 
           economy_customers > 0 ? economy_waiting_time / (double)economy_customers : 0.0);
    
    return 0;
}