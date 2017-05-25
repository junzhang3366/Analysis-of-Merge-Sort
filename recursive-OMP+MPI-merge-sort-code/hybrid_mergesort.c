/* 
 Hybrid MPI+OpenMP parallel recursive mergesort
 Copyright (C) 2011  Atanas Radenski
 This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*//*
 Copyright (C) 2011  Atanas Radenski
 This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h> 
#include <mpi.h>
#include <omp.h>

#define SMALL    32  // Arrays size <= SMALL switches to insertion sort

void merge(int a[], int size, int temp[]);
void insertion_sort(int a[], int size);
void mergesort_serial(int a[], int size, int temp[]);
void mergesort_parallel_mpi(int a[], int size, int temp[], 
                        int level, int my_rank, int max_rank,  
                        int tag, MPI_Comm comm, int threads);
int topmost_level_mpi(int my_rank);
void run_root_mpi (int a[], int size, int temp[], int max_rank, int tag, MPI_Comm comm, int threads);
void run_node_mpi(int my_rank, int max_rank, int tag, MPI_Comm comm, int threads);
void mergesort_parallel_omp(int a[], int size, int temp[], int threads);
int main(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    // All processes
	MPI_Init(&argc, &argv);
    // Enable nested parallelism, if available
	omp_set_nested(1);
	// Check processes and their ranks
	// number of processes == communicator size
	int comm_size; MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    int my_rank; MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    int max_rank = comm_size - 1;
    int tag = 123, root_rank = 0;
	// Check arguments
	if ( argc != 3 ) /* argc must be 3 for proper execution! */
	{
	    if (my_rank == 0) {
			printf( "Usage: %s array-size OMP-threads-per-MPI-process>0\n", argv[0]);
		}
		MPI_Abort(MPI_COMM_WORLD, 1);		
	}
	// Get arguments
	int size = atoi(argv[1]); // Array size 
	int threads = atoi(argv[2]); // Requested number of threads per node
	if (threads < 1) {
	    if (my_rank == 0) {
			printf( "Error: requested %d threads per MPI process, must be at least 1\n", threads);
		}
		MPI_Abort(MPI_COMM_WORLD, 1);			
	}
    // Set test data
    if (my_rank == 0) { // Only root process sets test data 
		puts("-Multilevel parallel Recursive Mergesort with MPI and OpenMP-\t");
		printf("Array size = %d\nProcesses = %d\nThreads per process = %d\n", size, comm_size, threads);
		// Check nested parallelism availability
		if (omp_get_nested() !=1 )
		{
			puts("Warning: Nested parallelism desired but unavailable");
		} 	
		// Array allocation
		int* a    = malloc(sizeof(int)*size);
		int* temp = malloc(sizeof(int)*size);
		if (a == NULL || temp == NULL) 
		{
			printf( "Error: Could not allocate array of size %d\n", size);
			MPI_Abort(MPI_COMM_WORLD, 1);
		}
	    // Random array initialization
        srand(time(NULL));
        int i;
        for(i=0; i<size; i++) {
            a[i] = rand() % size;
        }       
		// Sort with root process
		double start = MPI_Wtime();
		run_root_mpi(a, size, temp, max_rank, tag, MPI_COMM_WORLD, threads);
        double end = MPI_Wtime();
		printf("Start = %.2f\nEnd = %.2f\nElapsed = %.2f\n",
			start, end, end - start);
		double wtick = MPI_Wtick();
		printf("Wtick = %.8f\n1/Wtick = %.8f\n", wtick, 1.0 / wtick);
       	// Result check
		for(i=1; i<size; i++) {
            if (!(a[i-1] <= a[i])) {
				printf("Implementtion error: a[%d]=%d > a[%d]=%d\n", i-1, a[i-1], i, a[i]);
				MPI_Abort(MPI_COMM_WORLD, 1);
            }
        }
	} // Root process end
    else { // Node processes  
        run_node_mpi(my_rank, max_rank, tag, MPI_COMM_WORLD, threads);
    }
    fflush(stdout);
    MPI_Finalize();
    return 0;
}

// Root process code
void run_root_mpi (int a[], int size, int temp[], int max_rank, int tag, MPI_Comm comm, int threads) {
    int my_rank;
    MPI_Comm_rank(comm, &my_rank);
    if (my_rank != 0) {
        printf("Error: run_root_mpi called from process %d; must be called from process 0 only\n", my_rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    mergesort_parallel_mpi(a, size, temp, 0, my_rank, max_rank, tag, comm, threads); // level=0; my_rank=root_rank=0 
    return;
}

// Node process code
void run_node_mpi(int my_rank, int max_rank, int tag, MPI_Comm comm, int threads) {
    int level=topmost_level_mpi(my_rank);
    // Probe for a message and determine its size and sender
    MPI_Status status; int size;
    MPI_Probe(MPI_ANY_SOURCE, tag, comm, &status);
    MPI_Get_count(&status, MPI_INT, &size);
    int parent_rank = status.MPI_SOURCE;
    // Allocate int a[size], temp[size] 
    int *a = malloc(sizeof(int)*size); 
    int *temp = malloc(sizeof(int)*size);
    MPI_Recv(a, size, MPI_INT, parent_rank, tag, comm, &status);
	Was: mergesort_parallel_mpi(a, size, temp, level, my_rank, max_rank, tag, comm, threads);  
	// Send sorted array to parent process
    MPI_Send(a, size, MPI_INT, parent_rank, tag, comm);
    return;
}

// Given a process rank, calculate the top level of the process tree in which the process participates
// Root assumed to always have rank 0 and to participate at level 0 of the process tree
int topmost_level_mpi(int my_rank) {
    int level = 0;
    while (pow(2, level) <= my_rank) level++;
    return level;    
}

// MPI merge sort
void mergesort_parallel_mpi(int a[], int size, int temp[], 
                        int level, int my_rank, int max_rank,  
                        int tag, MPI_Comm comm, int threads) {
    int helper_rank = my_rank + pow(2, level);
    if (helper_rank > max_rank) { // no more MPI processes available, then use OpenMP
		mergesort_parallel_omp(a, size, temp, threads);
    	// Was: mergesort_serial(a, size, temp);
    } else {
        MPI_Request request; MPI_Status status;
        // Send second half, asynchronous
        MPI_Isend(a + size/2, size - size/2, MPI_INT, helper_rank, tag, comm, &request);
        // Sort first half with OpenMP
		// mergesort_parallel_omp(a, size/2, temp, threads);
		mergesort_parallel_mpi(a, size/2, temp, level+1, my_rank, max_rank, tag, comm, threads);
        // Receive second half sorted
        MPI_Recv(a + size/2, size - size/2, MPI_INT, helper_rank, tag, comm, &status);
        // Merge the two sorted sub-arrays through temp
        merge(a, size, temp);
    } 
    return;
}

// OpenMP merge sort with given number of threads
void mergesort_parallel_omp(int a[], int size, int temp[], int threads) {
    int i;
    if ( threads == 1) {
        //printf("Thread %d begins serial mergesort\n", omp_get_thread_num());
    	mergesort_serial(a, size, temp);
    } else if (threads > 1) {
       #pragma omp parallel sections num_threads(2)
       {
			#pragma omp section
			mergesort_parallel_omp(a, size/2, temp, threads/2);
			#pragma omp section
			mergesort_parallel_omp(a + size/2, size - size/2, temp + size/2, threads - threads/2);
			// The above use of temp + size/2 is an essential change from the serial version	
       }
	   // Thread allocation is implementation dependent
       // Some threads can execute multiple sections while others are idle 
       // Merge the two sorted sub-arrays through temp
       merge(a, size, temp); 
    } else {
       printf("Error: %d threads\n", threads); 
       return;
    }
}

void mergesort_serial(int a[], int size, int temp[]) {
    // Switch to insertion sort for small arrays
     if (size < SMALL) {
       insertion_sort(a, size);
       return;
    }    
    mergesort_serial(a, size/2, temp);
    mergesort_serial(a + size/2, size - size/2, temp);
    // The above call will not work properly in an OpenMP program
    // Merge the two sorted subarrays into a temp array
    merge(a, size, temp);
}

void merge(int a[], int size, int temp[]) {
    int i1 = 0;
    int i2 = size/2;
    int tempi = 0;
    while (i1 < size/2 && i2 < size) {
        if (a[i1] < a[i2]) {
            temp[tempi] = a[i1];
            i1++;
        } else {
            temp[tempi] = a[i2];
            i2++;
        }
        tempi++;
    }
    while (i1 < size/2) {
        temp[tempi] = a[i1];
        i1++;
        tempi++;
    }
    while (i2 < size) {
        temp[tempi] = a[i2];
        i2++;
        tempi++;
    }
    // Copy sorted temp array into main array, a
    memcpy(a, temp, size*sizeof(int));
}

void insertion_sort(int a[], int size) {
    int i;
    for (i=0; i < size; i++) {
    int j, v = a[i];
        for (j = i - 1; j >= 0; j--) {
            if (a[j] <= v) break; 
            a[j + 1] = a[j];
        }
        a[j + 1] = v;
    }
}
