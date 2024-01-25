# Operating System -  User-Level Threads Library (uthreads)
**Overview**

This repository contains the source code for a simple User-Level Threads Library named uthreads. The library allows for the creation and management of multiple threads within a single process. Each thread has its own stack, and the library provides functions for thread creation, termination, blocking, resuming, sleeping, and obtaining information about threads.

**Files**

uthreads.h: Header file containing the API for the uthreads library.
uthreads.cpp: Implementation file containing the source code for the uthreads library.

**Compilation**

To compile the uthreads library, use the following command:

*g++ -o uthreads uthreads.cpp -std=c++11*

**Usage**

- Include the uthreads.h header file in your program.
- Link your program with the compiled uthreads library.

**Initialization**

Initialize the thread library by calling *\*uthread_init(int quantum_usecs)* . This function sets up the main thread and defines the length of a quantum in microseconds.

**Thread Creation**

Create a new thread using *uthread_spawn(thread_entry_point entry_point)* with the entry point function for the new thread.

**Thread Termination**

Terminate a thread using *uthread_terminate(int tid)* .

**Thread Blocking and Resuming**

Block and resume threads using *uthread_block(int tid)* and *uthread_resume(int tid)*.

**Thread Sleeping**

Make a thread sleep for a specified number of quantums using *uthread_sleep(int num_quantums)* .

**Thread Information**

Obtain information about threads using *uthread_get_tid()* , *uthread_get_total_quantums()*, and *uthread_get_quantums(int tid)* .

Enjoy using the uthreads library in your projects!

