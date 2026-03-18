# Parallel Matrix Multiplication (C++ | Linux | Multiprocessing)

This project explores high-performance parallel matrix multiplication using **process-level parallelism** in Linux. It implements and compares different inter-process communication (IPC) strategies and workload scheduling techniques.

---

##  Key Ideas

- Parallel computation using `fork()` (multi-process model)
- Block-based (tiled) matrix multiplication
- Inter-process communication (IPC)
- Load balancing and scheduling strategies
- Performance vs communication trade-offs

---

##  Implementations

### 1. Pipe-Based IPC (Dynamic Scheduling)
- Uses **pipes** for communication between parent and worker processes
- Parent distributes tasks dynamically (work-stealing style)
- Workers compute matrix blocks and send results back via pipes

**Highlights:**
- Dynamic load balancing
- Better CPU utilization
- Higher communication overhead

---

### 2. Shared Memory (Static Partitioning)
- Uses **System V shared memory (`shmget`, `shmat`)**
- Each process writes directly to a shared result matrix
- Work is statically divided among processes

**Highlights:**
- Low communication overhead
- Lock-free design (disjoint memory writes)
- Simpler but less adaptive than dynamic scheduling

---

##  Design Decisions

### Block-based Computation
- Matrix divided into smaller tiles (`block_size`)
- Improves cache locality and parallelism

### Lock-Free Writes
- Each process writes to a unique region of the result matrix
- Avoids synchronization overhead

### CPU Affinity
- Processes pinned to specific cores using `sched_setaffinity`
- Improves cache performance and reduces context switching

---

## Correctness

- Results verified using a sequential implementation
- Floating-point comparison with tolerance (`epsilon = 1e-6`)

---

## How to Run

```bash
g++ -o parallel_mm file_name.c -lm
./parallel_mm <matrix_size> <num_cores>
