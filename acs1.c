#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

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

struct timeval init_time;
double overall_waiting_time = 0;
double business_waiting_time = 0;
double economy_waiting_time = 0;
int business_customers = 0;
int economy_customers = 0;
int queue_length[2] = {0};
int queue_status[2] = {0}; 
int winner_selected[2] = {0};

pthread_mutex_t queue_mutex[2];
pthread_mutex_t waiting_time_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_convar[2]; 
pthread_cond_t clerk_convar[NUM_CLERKS];

// Add these global variables
int total_customers = 0;
int business_class_customers = 0;
int economy_class_customers = 0;
pthread_mutex_t customer_count_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int simulation_finished = 0;

void *customer_entry(void *cus_info) {
    struct customer_info *p_myInfo = (struct customer_info *)cus_info;
    int class_type = p_myInfo->class_type;
    
    usleep(p_myInfo->arrival_time * 100000);
    
    fprintf(stdout, "A customer arrives: customer ID %2d. \n", p_myInfo->user_id);

    // --------------
    pthread_mutex_lock(&customer_count_mutex);
    total_customers++;
    if (class_type == BUSINESS_CLASS) {
        business_class_customers++;
    } else {
        economy_class_customers++;
    }
    pthread_mutex_unlock(&customer_count_mutex);
    // -------------
    
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
    
    pthread_mutex_unlock(&queue_mutex[class_type]);
    
    usleep(10);
    int clerk_id = queue_status[class_type];
    queue_status[class_type] = 0;
    
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    double waiting_time = (start_time.tv_sec - queue_enter_time.tv_sec) + (start_time.tv_usec - queue_enter_time.tv_usec)/1000000.0;
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
    
    double relative_start_time = (start_time.tv_sec - init_time.tv_sec) + (start_time.tv_usec - init_time.tv_usec)/1000000.0;
    
    fprintf(stdout, "A clerk starts serving a customer: start time %.2f, the customer ID %2d, the clerk ID %1d. \n", 
            relative_start_time, p_myInfo->user_id, clerk_id);
            
    usleep(p_myInfo->service_time * 100000);
    
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    double relative_end_time = (end_time.tv_sec - init_time.tv_sec) + (end_time.tv_usec - init_time.tv_usec)/1000000.0;
    
    fprintf(stdout, "A clerk finishes serving a customer: end time %.2f, the customer ID %2d, the clerk ID %1d. \n",
            relative_end_time, p_myInfo->user_id, clerk_id);
            
    pthread_cond_signal(&clerk_convar[clerk_id-1]);
    
    pthread_exit(NULL);
}

void *clerk_entry(void *clerkNum) {
    int clerk_id = *(int *)clerkNum;

    fprintf(stdout, "Clerk %d started working!\n", clerk_id);
    
    while (!simulation_finished) {
        int selected_queue = -1;
        
        // Determine which queue to select based on availability
        pthread_mutex_lock(&queue_mutex[BUSINESS_CLASS]);
        pthread_mutex_lock(&queue_mutex[ECONOMY_CLASS]);
        
        if (queue_length[BUSINESS_CLASS] > 0) {
            selected_queue = BUSINESS_CLASS;
        } else if (queue_length[ECONOMY_CLASS] > 0) {
            selected_queue = ECONOMY_CLASS;
        }
        
        if (selected_queue != -1) {
            // Select the queue and update status
            pthread_mutex_unlock(&queue_mutex[BUSINESS_CLASS]);
            pthread_mutex_unlock(&queue_mutex[ECONOMY_CLASS]);
            
            pthread_mutex_lock(&queue_mutex[selected_queue]);
            queue_status[selected_queue] = clerk_id;
            winner_selected[selected_queue] = 0;
            pthread_cond_broadcast(&queue_convar[selected_queue]);
            pthread_mutex_unlock(&queue_mutex[selected_queue]);
            
            // Wait for customer to signal readiness
            pthread_mutex_lock(&queue_mutex[selected_queue]);
            pthread_cond_wait(&clerk_convar[clerk_id - 1], &queue_mutex[selected_queue]);
            pthread_mutex_unlock(&queue_mutex[selected_queue]);
        } else {
            pthread_mutex_unlock(&queue_mutex[BUSINESS_CLASS]);
            pthread_mutex_unlock(&queue_mutex[ECONOMY_CLASS]);
        }
    }

    fprintf(stdout, "Clerk %d finished working!\n", clerk_id);

    pthread_exit(NULL);
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

    int num_customers;
    fscanf(fp, "%d", &num_customers);

    struct customer_info customers[MAX_CUSTOMERS];

    int i = 0;
    while (i < num_customers && !feof(fp)) {
        int arrival_time, service_time;
        fscanf(fp, "%d: %d,%d,%d", &customers[i].user_id, &customers[i].class_type,
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

    for (i = 0; i < 2; i++) {
        pthread_mutex_init(&queue_mutex[i], NULL);
        pthread_cond_init(&queue_convar[i], NULL);
    }

    for (i = 0; i < NUM_CLERKS; i++) {
        pthread_cond_init(&clerk_convar[i], NULL);
    }

    gettimeofday(&init_time, NULL);

    pthread_t clerk_threads[NUM_CLERKS];
    int clerk_ids[NUM_CLERKS];
    for (i = 0; i < NUM_CLERKS; i++) {
        clerk_ids[i] = i + 1;
        pthread_create(&clerk_threads[i], NULL, clerk_entry, (void *)&clerk_ids[i]);
    }

    pthread_t customer_threads[MAX_CUSTOMERS];
    for (i = 0; i < num_customers; i++) {
        pthread_create(&customer_threads[i], NULL, customer_entry, (void *)&customers[i]);
    }

    // Wait for all customer threads to finish
    for (i = 0; i < num_customers; i++) {
        pthread_join(customer_threads[i], NULL);
    }

    simulation_finished = 1; // Set cleanup flag to signal threads to exit

    for (i = 0; i < 2; i++) {
        pthread_cond_broadcast(&queue_convar[i]); // Signal all queues to wake up and check the cleanup flag
    }

    // Wait for all clerk threads to finish
    for (i = 0; i < NUM_CLERKS; i++) {
        pthread_join(clerk_threads[i], NULL);
    }

    printf("The average waiting time for all customers in the system is: %.2f seconds. \n", overall_waiting_time / num_customers);
    printf("The average waiting time for all business-class customers is: %.2f seconds. \n",
           business_customers > 0 ? business_waiting_time / business_customers : 0);
    printf("The average waiting time for all economy-class customers is: %.2f seconds. \n",
           economy_customers > 0 ? economy_waiting_time / economy_customers : 0);

    // Destroy mutexes and condition variables
    for (i = 0; i < 2; i++) {
        pthread_mutex_destroy(&queue_mutex[i]);
        pthread_cond_destroy(&queue_convar[i]);
    }

    for (i = 0; i < NUM_CLERKS; i++) {
        pthread_cond_destroy(&clerk_convar[i]);
    }

    pthread_mutex_destroy(&waiting_time_mutex);

    return 0;
}
