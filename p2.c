#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <sched.h>  


int matrix_size = 1000;     
int num_processes = 4;      
int block_size = 32;        
float *matrixA = NULL;      
float *matrixB = NULL;      
float *verify_matrix = NULL; 

void initialize_matrices() {
    srand(time(NULL));
    
    matrixA = (float *)malloc(matrix_size * matrix_size * sizeof(float));
    matrixB = (float *)malloc(matrix_size * matrix_size * sizeof(float));
    verify_matrix = (float *)malloc(matrix_size * matrix_size * sizeof(float));
    
    if (!matrixA || !matrixB || !verify_matrix) {
        printf("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0; i < matrix_size; i++) {
        for (int j = 0; j < matrix_size; j++) {
            matrixA[i * matrix_size + j] = ((float)rand() / (float)RAND_MAX) * 4.0f - 2.0f;
            matrixB[i * matrix_size + j] = ((float)rand() / (float)RAND_MAX) * 4.0f - 2.0f;
            verify_matrix[i * matrix_size + j] = 0.0f;
        }
    }
}

void verify_result(int shmid) {
    float *shared_matrix = (float *)shmat(shmid, NULL, 0);
    if (shared_matrix == (float *)-1) {
        perror("shmat in verify_result failed");
        exit(EXIT_FAILURE);
    }
    
 
    for (int i = 0; i < matrix_size; i++) {
        for (int j = 0; j < matrix_size; j++) {
            float sum = 0.0f;
            for (int k = 0; k < matrix_size; k++) {
                sum += matrixA[i * matrix_size + k] * matrixB[k * matrix_size + j];
            }
            verify_matrix[i * matrix_size + j] = sum;
        }
    }
    
   
    int errors = 0;
    float epsilon = 1e-6f; 
    for (int i = 0; i < matrix_size; i++) {
        for (int j = 0; j < matrix_size; j++) {
            if (fabs(verify_matrix[i * matrix_size + j] - shared_matrix[i * matrix_size + j]) > epsilon) {
                errors++;
                if (errors <= 10) {
                    printf("Error at [%d,%d]: shared = %.6f, verify = %.6f\n",
                           i, j,
                           shared_matrix[i * matrix_size + j],
                           verify_matrix[i * matrix_size + j]);
                }
            }
        }
    }
    
    if (errors == 0) {
        printf("Verification SUCCESSFUL: The results match!\n");
    } else {
        printf("Verification FAILED: %d errors found.\n", errors);
    }
    shmdt(shared_matrix);
}

void multiply_matrix_block(int start_row, int start_col, int current_block_rows, int current_block_cols, float *shared_matrix) {
    for (int i = start_row; i < start_row + current_block_rows; i++) {
        for (int j = start_col; j < start_col + current_block_cols; j++) {
            float sum = 0.0f;
            for (int k = 0; k < matrix_size; k++) {
                sum += matrixA[i * matrix_size + k] * matrixB[k * matrix_size + j];
            }
            shared_matrix[i * matrix_size + j] = sum;
        }
    }
}


double getdetlatimeofday(struct timeval *begin, struct timeval *end) {
    return (end->tv_sec + end->tv_usec * 1.0 / 1000000) -
           (begin->tv_sec + begin->tv_usec * 1.0 / 1000000);
}


void child_process(int shmid, int child_id) {
    float *shared_matrix = (float *)shmat(shmid, NULL, 0);
    if (shared_matrix == (float *)-1) {
        perror("shmat failed in child");
        exit(EXIT_FAILURE);
    }
    
    int num_blocks = (matrix_size + block_size - 1) / block_size;
    
    for (int bi = 0; bi < num_blocks; bi++) {
        for (int bj = 0; bj < num_blocks; bj++) {
            int block_index = bi * num_blocks + bj;
            if (block_index % num_processes == child_id) {
                int start_row = bi * block_size;
                int start_col = bj * block_size;
                int current_block_rows = ((start_row + block_size) > matrix_size) ? (matrix_size - start_row) : block_size;
                int current_block_cols = ((start_col + block_size) > matrix_size) ? (matrix_size - start_col) : block_size;
                
                multiply_matrix_block(start_row, start_col, current_block_rows, current_block_cols, shared_matrix);
            }
        }
    }
    
    shmdt(shared_matrix);
    exit(EXIT_SUCCESS);
}

int main(int argc, char const *argv[]) {
    
    if (argc >= 2) {
        matrix_size = atoi(argv[1]);
        if (matrix_size <= 0) {
            printf("Invalid matrix size. Using default size: %d\n", 1000);
            matrix_size = 1000;
        }
    }
    if (argc >= 3) {
        int num_cores = atoi(argv[2]);
        if (num_cores <= 0) {
            printf("Invalid number of cores. Using default: 1 core\n");
            num_cores = 1;
        }
        num_processes = num_cores * 6;
        
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < num_cores; i++) {
            CPU_SET(i, &cpuset);
        }
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
            perror("sched_setaffinity failed");
        }
        printf("Input size: %d\n", matrix_size);
        printf("Total cores: %d\n", num_cores);
        
    }
    
    initialize_matrices();
    

    key_t key = 1234;
    int shmid = shmget(key, matrix_size * matrix_size * sizeof(float), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    pid_t *child_pids = malloc(num_processes * sizeof(pid_t));
    if (!child_pids) {
        perror("malloc for child_pids failed");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < num_processes; i++) {
        child_pids[i] = fork();
        if (child_pids[i] < 0) {
            perror("fork failed");
            for (int j = 0; j < i; j++) {
                kill(child_pids[j], SIGTERM);
            }
            shmctl(shmid, IPC_RMID, NULL);
            free(child_pids);
            free(matrixA);
            free(matrixB);
            free(verify_matrix);
            exit(EXIT_FAILURE);
        } else if (child_pids[i] == 0) {
            child_process(shmid, i);
        }
    }
    

    for (int i = 0; i < num_processes; i++) {
        int status;
        waitpid(child_pids[i], &status, 0);
    }
    
   
    gettimeofday(&end_time, NULL);
    double time_taken = getdetlatimeofday(&start_time, &end_time);
    printf("Total runtime: %.6f \n", time_taken);
    
    verify_result(shmid);
    shmctl(shmid, IPC_RMID, NULL);
    free(child_pids);
    free(matrixA);
    free(matrixB);
    free(verify_matrix);
    
    return 0;
}
