#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>

#define MAX_CUSTOMERS 100
#define NUM_CLERKS 5
#define BUSINESS_CLASS 1
#define ECONOMY_CLASS 0

struct customer_info {
    int user_id;
    int class_type;
    int service_time;
    int arrival_time;
    double waiting_time;
};

struct clerk_info {
    int clerk_id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int serving_customer;
    int busy;  // Add this line
};

struct timeval init_time;
double overall_waiting_time = 0;
double business_waiting_time = 0;
double economy_waiting_time = 0;
int business_customers = 0;
int economy_customers = 0;
int num_customers = 0;
int queue_length[2] = {0};
int queue_status[2] = {0};
int winner_selected[2] = {0};
volatile int program_running = 1;

pthread_mutex_t queue_mutex[2] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t waiting_time_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_convar[2] = {PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};
struct clerk_info clerks[NUM_CLERKS];

void *customer_entry(void *cus_info) {
    struct customer_info *p_myInfo = (struct customer_info *)cus_info;
    int class_type = p_myInfo->class_type;

    usleep(p_myInfo->arrival_time * 100000);
    fprintf(stdout, "A customer arrives: customer ID %2d. \n", p_myInfo->user_id);

    pthread_mutex_lock(&queue_mutex[class_type]);
    fprintf(stdout, "A customer enters a queue: the queue ID %1d, and length of the queue %2d. \n",
            class_type, ++queue_length[class_type]);

    struct timeval queue_enter_time;
    gettimeofday(&queue_enter_time, NULL);

    while (1) {
        pthread_cond_wait(&queue_convar[class_type], &queue_mutex[class_type]);
        if (queue_status[class_type] != 0 && !winner_selected[class_type]) {
            winner_selected[class_type] = 1;
            queue_length[class_type]--;
            break;
        }
    }

    int clerk_id = queue_status[class_type];
    queue_status[class_type] = 0;
    pthread_mutex_unlock(&queue_mutex[class_type]);

    // Ensure clerk_id is never 0
    if (clerk_id == 0) {
        fprintf(stderr, "Error: clerk_id is 0\n");
        exit(1);
    }

    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    double waiting_time = (start_time.tv_sec - queue_enter_time.tv_sec) + 
                          (start_time.tv_usec - queue_enter_time.tv_usec) / 1000000.0;
    p_myInfo->waiting_time = waiting_time;

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

    double relative_start_time = (start_time.tv_sec - init_time.tv_sec) + 
                                 (start_time.tv_usec - init_time.tv_usec) / 1000000.0;
    fprintf(stdout, "A clerk starts serving a customer: start time %.2f, the customer ID %2d, the clerk ID %1d. \n",
            relative_start_time, p_myInfo->user_id, clerk_id);

    usleep(p_myInfo->service_time * 100000);

    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    double relative_end_time = (end_time.tv_sec - init_time.tv_sec) + 
                               (end_time.tv_usec - init_time.tv_usec) / 1000000.0;
    fprintf(stdout, "A clerk finishes serving a customer: end time %.2f, the customer ID %2d, the clerk ID %1d. \n",
            relative_end_time, p_myInfo->user_id, clerk_id);

    // Set the clerk as not busy
    pthread_mutex_lock(&clerks[clerk_id-1].mutex);
    clerks[clerk_id-1].busy = 0;
    pthread_cond_signal(&clerks[clerk_id-1].cond);
    pthread_mutex_unlock(&clerks[clerk_id-1].mutex);

    pthread_mutex_lock(&queue_mutex[class_type]);
    pthread_cond_broadcast(&queue_convar[class_type]);
    pthread_mutex_unlock(&queue_mutex[class_type]);

    pthread_exit(NULL);
}

void *clerk_entry(void *clerkNum) {
    int clerk_id = (int)(intptr_t)clerkNum;
    while (program_running) {
        int selected_queue = -1;

        // Wait until the clerk is not busy
        pthread_mutex_lock(&clerks[clerk_id-1].mutex);
        while (clerks[clerk_id-1].busy && program_running) {
            pthread_cond_wait(&clerks[clerk_id-1].cond, &clerks[clerk_id-1].mutex);
        }
        if (!program_running) {
            pthread_mutex_unlock(&clerks[clerk_id-1].mutex);
            break;
        }
        pthread_mutex_unlock(&clerks[clerk_id-1].mutex);

        // Check business class queue first
        pthread_mutex_lock(&queue_mutex[BUSINESS_CLASS]);
        if (queue_length[BUSINESS_CLASS] > 0) {
            selected_queue = BUSINESS_CLASS;
        }
        pthread_mutex_unlock(&queue_mutex[BUSINESS_CLASS]);

        // If no business class customers, check economy class queue
        if (selected_queue == -1) {
            pthread_mutex_lock(&queue_mutex[ECONOMY_CLASS]);
            if (queue_length[ECONOMY_CLASS] > 0) {
                selected_queue = ECONOMY_CLASS;
            }
            pthread_mutex_unlock(&queue_mutex[ECONOMY_CLASS]);
        }

        if (selected_queue != -1) {
            // Proceed with serving the customer
            pthread_mutex_lock(&queue_mutex[selected_queue]);
            queue_status[selected_queue] = clerk_id;
            winner_selected[selected_queue] = 0;
            pthread_cond_broadcast(&queue_convar[selected_queue]);
            pthread_mutex_unlock(&queue_mutex[selected_queue]);

            // Set the clerk as busy
            pthread_mutex_lock(&clerks[clerk_id-1].mutex);
            clerks[clerk_id-1].busy = 1;
            pthread_mutex_unlock(&clerks[clerk_id-1].mutex);
        } else {
            // If no customers in queue, wait for a short while before checking again
            usleep(10000);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        exit(1);
    }

    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL) {
        printf("Could not open file %s\n", argv[1]);
        exit(1);
    }

    fscanf(fp, "%d", &num_customers);
    struct customer_info customers[MAX_CUSTOMERS];
    int i = 0;
    while (i < num_customers && !feof(fp)) {
        int arrival_time, service_time;
        fscanf(fp, "%d:%d,%d,%d", &customers[i].user_id, &customers[i].class_type,
               &arrival_time, &service_time);
        if (arrival_time <= 0 || service_time <= 0) {
            printf("Error: Invalid arrival time or service time for customer %d\n", customers[i].user_id);
            exit(1);
        }
        customers[i].arrival_time = arrival_time;
        customers[i].service_time = service_time;
        i++;
    }
    fclose(fp);

    gettimeofday(&init_time, NULL);

    for (i = 0; i < NUM_CLERKS; i++) {
        clerks[i].clerk_id = i+1;
        clerks[i].serving_customer = 0;
        clerks[i].busy = 0;  // Initialize busy flag
        pthread_mutex_init(&clerks[i].mutex, NULL);
        pthread_cond_init(&clerks[i].cond, NULL);
    }

    pthread_t clerk_threads[NUM_CLERKS];
    for (i = 0; i < NUM_CLERKS; i++) {
        int clerk_id = i + 1; // Clerk IDs start from 1
        int ret = pthread_create(&clerk_threads[i], NULL, clerk_entry, (void *)(intptr_t)clerk_id);
        if (ret != 0) {
            printf("Error creating clerk thread %d: %d\n", i, ret);
            exit(1);
        }
    }

    pthread_t customer_threads[MAX_CUSTOMERS];
    for (i = 0; i < num_customers; i++) {
        int ret = pthread_create(&customer_threads[i], NULL, customer_entry, (void *)&customers[i]);
        if (ret != 0) {
            printf("Error creating customer thread %d: %d\n", i, ret);
            exit(1);
        }
    }

    for (i = 0; i < num_customers; i++) {
        pthread_join(customer_threads[i], NULL);
    }

    // Signal program termination
    program_running = 0;

    // Wake up all clerk threads
    for (i = 0; i < NUM_CLERKS; i++) {
        pthread_mutex_lock(&clerks[i].mutex);
        pthread_cond_signal(&clerks[i].cond);
        pthread_mutex_unlock(&clerks[i].mutex);
    }

    // Wait for all clerk threads to complete
    for (i = 0; i < NUM_CLERKS; i++) {
        pthread_join(clerk_threads[i], NULL);
    }

    // Add a small delay after all customer threads have completed
    usleep(100000); // 100ms delay

    // // Add debug print statements
    // printf("Debug: overall_waiting_time = %.2f\n", overall_waiting_time);
    // printf("Debug: num_customers = %d\n", num_customers);
    // printf("Debug: business_waiting_time = %.2f\n", business_waiting_time);
    // printf("Debug: business_customers = %d\n", business_customers);
    // printf("Debug: economy_waiting_time = %.2f\n", economy_waiting_time);
    // printf("Debug: economy_customers = %d\n", economy_customers);

    printf("The average waiting time for all customers in the system is: %.2f seconds. \n", 
           num_customers > 0 ? overall_waiting_time / (double)num_customers : 0.0);
    printf("The average waiting time for all business-class customers is: %.2f seconds. \n",
           business_customers > 0 ? business_waiting_time / (double)business_customers : 0.0);
    printf("The average waiting time for all economy-class customers is: %.2f seconds. \n",
           economy_customers > 0 ? economy_waiting_time / (double)economy_customers : 0.0);

    return 0;
}
