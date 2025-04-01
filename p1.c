#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>


typedef struct {
    int start_row;
    int start_col;
    int block_rows;
    int block_cols;
} TaskData;

int matrix_size = 1000; 
int num_processes = 0;   
int block_size = 32;     
float *matrixA = NULL;  
float *matrixB = NULL;  
float *matrixC = NULL;  
float *verify_matrix = NULL; 


void initialize_matrices() {
    srand(time(NULL));
    
    matrixA = (float *)malloc(matrix_size * matrix_size * sizeof(float));
    matrixB = (float *)malloc(matrix_size * matrix_size * sizeof(float));
    matrixC = (float *)malloc(matrix_size * matrix_size * sizeof(float));
    verify_matrix = (float *)malloc(matrix_size * matrix_size * sizeof(float));
    
    if (!matrixA || !matrixB || !matrixC || !verify_matrix) {
        printf("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0; i < matrix_size; i++) {
        for (int j = 0; j < matrix_size; j++) {
            matrixA[i * matrix_size + j] = ((float)rand() / (float)RAND_MAX) * 4.0f - 2.0f;
            matrixB[i * matrix_size + j] = ((float)rand() / (float)RAND_MAX) * 4.0f - 2.0f;
            matrixC[i * matrix_size + j] = 0.0f;
            verify_matrix[i * matrix_size + j] = 0.0f;
        }
    }
}


void multiply_matrix_block(int start_row, int start_col, int block_rows, int block_cols) {
    for (int i = start_row; i < start_row + block_rows; i++) {
        for (int j = start_col; j < start_col + block_cols; j++) {
            float sum = 0.0f;
            for (int k = 0; k < matrix_size; k++) {
                sum += matrixA[i * matrix_size + k] * matrixB[k * matrix_size + j];
            }
            matrixC[i * matrix_size + j] = sum;
        }
    }
}

double getdetlatimeofday(struct timeval *begin, struct timeval *end) {
    return (end->tv_sec + end->tv_usec * 1.0 / 1000000) -
           (begin->tv_sec + begin->tv_usec * 1.0 / 1000000);
}



void verify_result() {
    // Compute result sequentially into verify_matrix
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
            if (fabs(matrixC[i * matrix_size + j] - verify_matrix[i * matrix_size + j]) > epsilon) {
                errors++;
                if (errors <= 10) {
                    printf("Error at [%d,%d]: computed = %.6f, verify = %.6f\n",
                           i, j,
                           matrixC[i * matrix_size + j],
                           verify_matrix[i * matrix_size + j]);
                }
            }
        }
    }
    
    if (errors == 0)
        printf("Verification SUCCESSFUL: The results match!\n");
    else
        printf("Verification FAILED: %d errors found.\n", errors);
        
}

void child_process(int child_id, int task_pipe_fd[2], int result_pipe_fd[2]) {
    close(task_pipe_fd[1]);    
    close(result_pipe_fd[0]);  
    
    TaskData task;
    while (1) {
        ssize_t bytes = read(task_pipe_fd[0], &task, sizeof(TaskData));
        if (bytes != sizeof(TaskData)) {
            perror("Child read error");
            exit(EXIT_FAILURE);
        }
        if (task.start_row == -1)
            break;
        
        multiply_matrix_block(task.start_row, task.start_col, task.block_rows, task.block_cols);
        int header[4] = { task.block_rows, task.block_cols, task.start_row, task.start_col };
        if (write(result_pipe_fd[1], header, sizeof(header)) != sizeof(header)) {
            perror("Child write header error");
            exit(EXIT_FAILURE);
        }
       
        for (int i = 0; i < task.block_rows; i++) {
            if (write(result_pipe_fd[1],
                      &matrixC[(task.start_row + i) * matrix_size + task.start_col],
                      task.block_cols * sizeof(float)) != task.block_cols * sizeof(float)) {
                perror("Child write block error");
                exit(EXIT_FAILURE);
            }
        }
    }
    close(task_pipe_fd[0]);
    close(result_pipe_fd[1]);
    exit(EXIT_SUCCESS);
}

int main(int argc, char const *argv[]) {
    if (argc >= 2) {
        matrix_size = atoi(argv[1]);
        if (matrix_size <= 0) {
            printf("Invalid matrix size. Using default: %d\n", 1000);
            matrix_size = 1000;
        }
    }
    
    int num_cores = 1;
    if (argc >= 3) {
        num_cores = atoi(argv[2]);
        if (num_cores <= 0) {
            printf("Invalid core count. Using default: 1\n");
            num_cores = 1;
        }
    }
    num_processes = num_cores * 6;
    printf("Input size: %d\n", matrix_size);
    printf("Total Cores: %d\n", num_cores);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < num_cores; i++) {
        CPU_SET(i, &cpuset);
    }
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        perror("sched_setaffinity failed");
    }
    initialize_matrices();
    
    int **task_pipes = malloc(num_processes * sizeof(int *));
    int **result_pipes = malloc(num_processes * sizeof(int *));
    for (int i = 0; i < num_processes; i++) {
        task_pipes[i] = malloc(2 * sizeof(int));
        result_pipes[i] = malloc(2 * sizeof(int));
        if (pipe(task_pipes[i]) == -1 || pipe(result_pipes[i]) == -1) {
            perror("Pipe creation failed");
            exit(EXIT_FAILURE);
        }
    }
    
    pid_t *child_pids = malloc(num_processes * sizeof(pid_t));
    for (int i = 0; i < num_processes; i++) {
        child_pids[i] = fork();
        if (child_pids[i] < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        } else if (child_pids[i] == 0) {
            
            for (int j = 0; j < num_processes; j++) {
                if (j != i) {
                    close(task_pipes[j][0]); close(task_pipes[j][1]);
                    close(result_pipes[j][0]); close(result_pipes[j][1]);
                }
            }
            child_process(i, task_pipes[i], result_pipes[i]);
        }
    }
    
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
   
    int blocks_per_row = (matrix_size + block_size - 1) / block_size;
    int total_tasks = blocks_per_row * blocks_per_row;
    int tasks_completed = 0, next_task = 0;
    

    for (int i = 0; i < num_processes && next_task < total_tasks; i++) {
        int block_row = (next_task / blocks_per_row) * block_size;
        int block_col = (next_task % blocks_per_row) * block_size;
        int current_block_rows = (block_row + block_size > matrix_size) ? (matrix_size - block_row) : block_size;
        int current_block_cols = (block_col + block_size > matrix_size) ? (matrix_size - block_col) : block_size;
        TaskData task = { block_row, block_col, current_block_rows, current_block_cols };
        if (write(task_pipes[i][1], &task, sizeof(TaskData)) != sizeof(TaskData)) {
            perror("Parent write error");
            exit(EXIT_FAILURE);
        }
        next_task++;
    }
    
    while (tasks_completed < total_tasks) {
        for (int i = 0; i < num_processes && tasks_completed < total_tasks; i++) {
            int header[4];
            ssize_t n = read(result_pipes[i][0], header, sizeof(header));
            if (n == sizeof(header)) {
                int block_rows = header[0], block_cols = header[1];
                int start_row = header[2], start_col = header[3];
                for (int r = 0; r < block_rows; r++) {
                    ssize_t m = read(result_pipes[i][0],
                        &matrixC[(start_row + r) * matrix_size + start_col],
                        block_cols * sizeof(float));
                    if (m != block_cols * sizeof(float)) {
                        perror("Parent block read error");
                        exit(EXIT_FAILURE);
                    }
                }
                tasks_completed++;
                
                if (next_task < total_tasks) {
                    int block_row = (next_task / blocks_per_row) * block_size;
                    int block_col = (next_task % blocks_per_row) * block_size;
                    int current_block_rows = (block_row + block_size > matrix_size) ? (matrix_size - block_row) : block_size;
                    int current_block_cols = (block_col + block_size > matrix_size) ? (matrix_size - block_col) : block_size;
                    TaskData task = { block_row, block_col, current_block_rows, current_block_cols };
                    if (write(task_pipes[i][1], &task, sizeof(TaskData)) != sizeof(TaskData)) {
                        perror("Parent write error");
                        exit(EXIT_FAILURE);
                    }
                    next_task++;
                }
            }
        }
    }
    
    TaskData termination = { -1, -1, -1, -1 };
    for (int i = 0; i < num_processes; i++) {
        if (write(task_pipes[i][1], &termination, sizeof(TaskData)) != sizeof(TaskData)) {
            perror("Parent write termination error");
            exit(EXIT_FAILURE);
        }
    }
    
    for (int i = 0; i < num_processes; i++) {
        int status;
        waitpid(child_pids[i], &status, 0);
    }
    
    gettimeofday(&end_time, NULL);
    double time_taken = getdetlatimeofday(&start_time, &end_time);
    printf("Total runtime: %.6f\n", time_taken);
    verify_result();
    
    for (int i = 0; i < num_processes; i++) {
        close(task_pipes[i][0]); close(task_pipes[i][1]);
        close(result_pipes[i][0]); close(result_pipes[i][1]);
        free(task_pipes[i]); free(result_pipes[i]);
    }
    free(task_pipes); free(result_pipes);
    free(child_pids);
    free(matrixA); free(matrixB); free(matrixC); free(verify_matrix);
    
    return 0;
}
