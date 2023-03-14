/*
 * student.c
 * Multithreaded OS Simulation for CS 2200
 *
 * This file contains the CPU scheduler for the simulation.
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "os-sim.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/** Function prototypes **/
extern void idle(unsigned int cpu_id);
extern void preempt(unsigned int cpu_id);
extern void yield(unsigned int cpu_id);
extern void terminate(unsigned int cpu_id);
extern void wake_up(pcb_t *process);


/*
 * current[] is an array of pointers to the currently running processes.
 * There is one array element corresponding to each CPU in the simulation.
 *
 * current[] should be updated by schedule() each time a process is scheduled
 * on a CPU.  Since the current[] array is accessed by multiple threads, you
 * will need to use a mutex to protect it.  current_mutex has been provided
 * for your use.
 */
static pcb_t **current;
static pthread_mutex_t current_mutex;

static pthread_mutex_t queue_mutex;
static pthread_cond_t queue_cond;

static pcb_t *readyQueue;

static int timeslice;
static int isSRTF;
unsigned int cpu_count;


static void push(pcb_t *process) {
    pcb_t* temp;
    process -> next = NULL;
    pthread_mutex_lock(&queue_mutex);
    if(readyQueue == NULL) {
		readyQueue = process;
	}
	else {
		temp = readyQueue;
		while(temp -> next != NULL) 
			temp = temp -> next;
		temp -> next = process;
	}
    pthread_mutex_unlock(&queue_mutex);
    pthread_cond_signal(&queue_cond);
}

static pcb_t* pop() {
    pthread_mutex_lock(&queue_mutex);
    pcb_t* newProcess = readyQueue;
    if(newProcess != NULL) {
        readyQueue = newProcess -> next;
        newProcess -> next = NULL;
	}
    pthread_mutex_unlock(&queue_mutex);
    return newProcess;
}

static pcb_t* popSRTF() {
    pcb_t* newProcess = NULL;
    pthread_mutex_lock(&queue_mutex);

    if (readyQueue == NULL) {
        pthread_mutex_unlock(&queue_mutex);
        return NULL;
    } else if (readyQueue -> next == NULL) {
        newProcess = readyQueue;
        readyQueue = NULL;
    } else {
        pcb_t *minPCB = NULL;
        pcb_t *currentPCB = readyQueue;

        if (currentPCB -> time_remaining > currentPCB -> next -> time_remaining) {
            minPCB = currentPCB;
        }

        while (currentPCB -> next -> next != NULL) {
            if (currentPCB -> next -> time_remaining > currentPCB -> next -> next -> time_remaining) {
                minPCB = currentPCB -> next;
            }
            currentPCB = currentPCB -> next;
        }

        if (minPCB == NULL) {
            newProcess = readyQueue;
            readyQueue = readyQueue -> next;
        } else {
            newProcess = minPCB -> next;
            minPCB -> next = minPCB -> next -> next;
        }
        newProcess -> next = NULL;
    }
    pthread_mutex_unlock(&queue_mutex);
    return newProcess;
}

/*
 * schedule() is your CPU scheduler.  It should perform the following tasks:
 *
 *   1. Select and remove a runnable process from your ready queue which 
 *	you will have to implement with a linked list or something of the sort.
 *
 *   2. Set the process state to RUNNING
 *
 *   3. Call context_switch(), to tell the simulator which process to execute
 *      next on the CPU.  If no process is runnable, call context_switch()
 *      with a pointer to NULL to select the idle process.
 *	The current array (see above) is how you access the currently running process indexed by the cpu id. 
 *	See above for full description.
 *	context_switch() is prototyped in os-sim.h. Look there for more information 
 *	about it and its parameters.
 */
static void schedule(unsigned int cpu_id)
{
    pcb_t* newProcess;
    if (isSRTF == 1) {
        newProcess = popSRTF();
    } else {
        newProcess = pop();
    }
    if (newProcess != NULL) {
        newProcess -> state = PROCESS_RUNNING;
    }
    pthread_mutex_lock(&current_mutex);
	current[cpu_id] = newProcess;
    pthread_mutex_unlock(&current_mutex);

    context_switch(cpu_id, newProcess, timeslice);
}


/*
 * idle() is your idle process.  It is called by the simulator when the idle
 * process is scheduled.
 *
 * This function should block until a process is added to your ready queue.
 * It should then call schedule() to select the process to run on the CPU.
 */
extern void idle(unsigned int cpu_id)
{
    /* FIX ME */
    pthread_mutex_lock(&current_mutex);
    while (readyQueue == NULL) {
        pthread_cond_wait(&queue_cond, &current_mutex);
    }
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);

    /*
     * REMOVE THE LINE BELOW AFTER IMPLEMENTING IDLE()
     *
     * idle() must block when the ready queue is empty, or else the CPU threads
     * will spin in a loop.  Until a ready queue is implemented, we'll put the
     * thread to sleep to keep it from consuming 100% of the CPU time.  Once
     * you implement a proper idle() function using a condition variable,
     * remove the call to mt_safe_usleep() below.
     */
}


/*
 * preempt() is the handler called by the simulator when a process is
 * preempted due to its timeslice expiring.
 *
 * This function should place the currently running process back in the
 * ready queue, and call schedule() to select a new runnable process.
 *
 * Remember to set the status of the process to the proper value.
 */
extern void preempt(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    
    current[cpu_id] -> state = PROCESS_READY;
	push(current[cpu_id]);
    
    pthread_mutex_unlock(&current_mutex);
	schedule(cpu_id);
}


/*
 * yield() is the handler called by the simulator when a process yields the
 * CPU to perform an I/O request.
 *
 * It should mark the process as WAITING, then call schedule() to select
 * a new process for the CPU.
 */
extern void yield(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    current[cpu_id] -> state = PROCESS_WAITING;
	pthread_mutex_unlock(&current_mutex);   
	schedule(cpu_id);
}


/*
 * terminate() is the handler called by the simulator when a process completes.
 * It should mark the process as terminated, then call schedule() to select
 * a new process for the CPU.
 */
extern void terminate(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex); 
    current[cpu_id] -> state = PROCESS_TERMINATED;
    pthread_mutex_unlock(&current_mutex);   
    schedule(cpu_id);
}


/*
 * wake_up() is the handler called by the simulator when a process's I/O
 * request completes.  It should perform the following tasks:
 *
 *   1. Mark the process as READY, and insert it into the ready queue.
 *
 *   2. If the scheduling algorithm is SRTF, wake_up() may need
 *      to preempt the CPU with the highest remaining time left to allow it to
 *      execute the process which just woke up.  However, if any CPU is
 *      currently running idle, or all of the CPUs are running processes
 *      with a lower remaining time left than the one which just woke up, wake_up()
 *      should not preempt any CPUs.
 *	To preempt a process, use force_preempt(). Look in os-sim.h for 
 * 	its prototype and the parameters it takes in.
 */
extern void wake_up(pcb_t *process)
{
    process -> state = PROCESS_READY;
	push(process);

    if (isSRTF == 1) {
        pthread_mutex_lock(&current_mutex);
        unsigned int toRemove = 0;
        unsigned int idle = 0;
        for (unsigned int i = 0; i < cpu_count; i++) {
            if (current[i] == NULL) {
                idle = 1;
            } else if (current[i]->time_remaining > current[toRemove]->time_remaining) {
                toRemove = i;
            }
        }
        pthread_mutex_unlock(&current_mutex);
        if (idle == 0 && current[toRemove]->time_remaining > process->time_remaining) {
            force_preempt(toRemove);
        }
    }
}


/*
 * main() simply parses command line arguments, then calls start_simulator().
 * You will need to modify it to support the -r and -s command-line parameters.
 */
int main(int argc, char *argv[])
{
    // unsigned int cpu_count;
    isSRTF = 0;
    /* Parse command-line arguments */
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "CS 2200 Project 4 -- Multithreaded OS Simulator\n"
            "Usage: ./os-sim <# CPUs> [ -r <time slice> | -s ]\n"
            "    Default : FIFO Scheduler\n"
            "         -r : Round-Robin Scheduler\n"
            "         -s : Shortest Remaining Time First Scheduler\n\n");
        return -1;
    } 
    if (argc == 2) {
        timeslice = -1;
    } else if (!strcmp(argv[2], "-r")) {
    	// which_scheduler = 1;
        timeslice = atoi(argv[3]);
    } else if (!strcmp(argv[2], "-s")) {
        isSRTF = 1;
        timeslice = -1;
    }
    cpu_count = strtoul(argv[1], NULL, 0);

    /* FIX ME - Add support for -r and -s parameters*/

    /* Allocate the current[] array and its mutex */
    current = malloc(sizeof(pcb_t*) * cpu_count);
    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
	pthread_cond_init(&queue_cond, NULL);

    /* Start the simulator in the library */
    start_simulator(cpu_count);

    return 0;
}


#pragma GCC diagnostic pop
