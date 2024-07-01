#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Define time limits
#define MAX_CUSTOMERS 50
#define MAX_ARRIVAL_TIME 9
#define MAX_SERVICE_TIME 99

int main() {
    srand(time(NULL)); // Seed the random number generator

    int num_customers = rand() % MAX_CUSTOMERS + 1;
    printf("%d\n", num_customers);

    for (int i = 1; i <= num_customers; i++) {
        int class_type = rand() % 2;
        int arrival_time = rand() % MAX_ARRIVAL_TIME + 1;
        int service_time = rand() % MAX_SERVICE_TIME + 1;
        printf("%d:%d,%d,%d\n", i, class_type, arrival_time, service_time);
    }

    return 0;
}