// Name: Mindy Zheng
// Date: 3/20/2024 
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h> 
#include <math.h>
#include <signal.h> 
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <sys/wait.h>
#include <stdarg.h> 
#include <sys/msg.h>
#include <stdbool.h> 

// Global variables for signal handler
//int *seconds, *nanoseconds;
//int sh_sec, sh_nano;

void help() { 
	printf("This program is designed to simulate an operating system scheduler using multi-level feedback queuing:\n"); 
	printf("[-h] - outputs a help message and terminates the program.\n");
	printf("[-n proc] - specifies total number of child processes.\n");
	printf("[-s simul] - specifies maximum number of child processes that can simultaneously run.\n");
	printf("[-t timeLimit for children] - specifies the bound of time a child process will be launched for ... b/w 1 second and -t]\n"); 
	printf("[-i intervalInMsToLaunchChildren] - specifies how often a children should be launched based on sys clock in milliseconds\n"); 
	printf("[-f logfile] - outputs log to a specified file\n"); 
}

// the Process Table does not have to be in shared memory 
struct PCB { 
	int occupied;	// either true or false
	pid_t pid; 		// process id of current assigned child 
	int startSeconds; 	// start seconds when it was forked
	int startNano; 	// start nano when it was forked
	int blocked; 	// whether process is blocked 
	int eventBlockedUntilSec; 	// time [sec] until this process becomes unblocked
	int eventBlockedUntilNano;	// time [ns] until this process become unblocked
}; 

struct PCB processTable[20];

void printTable(int PID, int startS, int startN, struct PCB processTable[20]) { 
	printf("OSS PID: %d    SysClockS: %d   SysclockNano: %d\n", PID, startS, startN); 
	printf("Process Table: \n"); 
	printf("------------------\n"); 
	printf("Entry Occupied               PID    StartS          StartN\n"); 
	for (int i = 0; i < 20; i++) { 
		printf("%-6d %-20d %-6d %-12d %-12d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano); 
	}
}

#define PRINT_TABLE 500000000 // Condition for printing the process table; half a nano 
#define NANO_INCREMENT 100000
#define MSCONVERSION  1000000 // 1,000,000 milliseconds in a nanosecond 
#define ONE_SEC_NANO 1000000000 // 1,000,000,000 (billion) nanoseconds is equal to 1 second
#define THOUSAND_NANO 100000000 // 100,000,000 
#define SCHEDULING_TIME 10000 
#define SCHEDULING_CONST 8000
void incrementClock(int *sec, int *nano, int dispatch) {
	(*nano) += dispatch; 
	if ((*nano) >= ONE_SEC_NANO) { 
		(*nano) -= ONE_SEC_NANO; 
		(*sec)++; 
	}
}

// Key for shared memory stuff 
#define SH_KEY1 89918991
#define SH_KEY2 89928992
#define PERMS 0644

/* random number generator - if it is called with -t 7, then when calling worker processes, it should call them with a time interval randomly between 1 second and 7 seconds (with nanoseconds also random).*/ 

static void myhandler(int);
static int setupinterrupt(void);
static int setupitimer(void);

// Function to update PCB
void updatePCB(struct PCB* processTable, pid_t pid_finished) { 
	for (int i =0; i <20; i++) { 
		if (processTable[i].pid == pid_finished) { 
			processTable[i].occupied = 0; 
			processTable[i].startSeconds = 0; 
			processTable[i].startNano = 0; 
		}
	}
}

// Random number generator (source: https://stackoverflow.com/questions/62649232/use-rand-in-c-to-get-a-different-number-every-run-without-using-time-or-getp)
static int randomize_helper(FILE *in) {
    unsigned int seed;
    if (!in)
         return -1;

    if (fread(&seed, sizeof seed, 1, in) == 1) {
        fclose(in);
        srand(seed);
    return 0;
    }

    fclose(in);
    return -1;
}

static int randomize(void) {
    if (!randomize_helper(fopen("/dev/urandom", "r")))
         return 0;
    if (!randomize_helper(fopen("/dev/arandom", "r")))
         return 0;
    if (!randomize_helper(fopen("/dev/random", "r")))
         return 0;
/* Other randomness sources (binary format)? */
/* No randomness sources found. */
    return -1;
}


// Function to add to PCB
void addPCB(struct PCB* processTable, pid_t pid, int* seconds, int* nanoseconds) {
	int j = 0;
    int arrayFlag = 0;
    while (!arrayFlag) {
        if (processTable[j].occupied == 1) {
            j++;
        } else if (processTable[j].occupied == 0) {
            arrayFlag = 1;
            processTable[j].occupied = 1;
            processTable[j].pid = pid;
            processTable[j].startSeconds = *seconds;
            processTable[j].startNano = *nanoseconds;
        } else {
            printf("Error within PCB \n");
            exit(1);
        }
    }
}

// Functions for Queue (Source: https://www.geeksforgeeks.org/queue-linked-list-implementation/
// LL node to store a queue entry 
struct QNode { 
	int key; 
	struct QNode* next; 
}; 

// The queue, front stores the front node of LL and rear stores the last node of LL 
struct Queue { 
	struct QNode *front, *rear; 
}; 

// A utility function to create a new linked list node
struct QNode *newNode (int x) { 
	struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode)); 
	temp -> key = x; 
	temp -> next = NULL; 
	return temp; 
} 

// A utility function to create an empty queue 
struct Queue* createQueue() { 
	struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue)); 
	q-> front = q-> rear = NULL; 
	return q; 
} 

// The function to add a key x to queue q 
void enQueue(struct Queue* q, int x) { 
	// Create a new LL node 
	struct QNode* temp = newNode(x); 
	// If queue is empty, then new node is front and rear both 
	if (q-> rear == NULL) { 
		q-> front = q-> rear = temp; 
		return; 
	}
	// Add the new node at the end of queue and change rear 
	q-> rear -> next = temp; 
	q-> rear = temp; 
} 

// Function to remove a key from given queue q 
void deQueue(struct Queue* q) { 
	// If queue is empty, return NULL 
	if (q-> front == NULL) { 
		return; 
	} 

	// Store previous front and move front one node ahead 
	struct QNode *temp = q-> front; 
	q-> front = q-> front-> next; 

	// If front becomes NULL, then change the rear to NULL as well 
	if (q-> front == NULL) { 
		q-> rear = NULL; 
	} 
	
	free(temp); 
}  


// Function to check if a new process can be launched: 
bool launchNewProcess(int flag, int launched, int proc, int simultaneous_count, int simul, int* seconds, int* nanoseconds, int next_interval_sec, int next_interval_nano) {
    return (flag == 0 && launched < proc && simultaneous_count < simul && 
            (((*seconds) > next_interval_sec || ((*seconds) == next_interval_sec && (*nanoseconds) > next_interval_nano))));
}

// Function to check the front of the 3 queues: q0, q1, & q2. Selects next queue based on priority
#define EMPTY -1 
#define QUEUE_0 0
#define QUEUE_1 1 
#define QUEUE_2 2 
int selectNextQueue(struct Queue* q0, struct Queue* q1, struct Queue* q2) { 
	// If q0 is not empty -> 0 
	if ((q0-> front != NULL)) { 
		return QUEUE_0; 
	}  
	// If q0 is empty but q1 is not -> 1 
	else if ((q1-> front != NULL)) { 
		return QUEUE_1;
	} 
	// if q0 and q1 are empty, but q2 isn't -> 2 
	else if ((q2->front != NULL)) { 
		return QUEUE_2; 
	} 
	// if all queues are empty, then... 
	else return EMPTY; 
}
 
// Message queue struct 
typedef struct msgbuffer { 
	long mtype; 
	int intData;
	int TQ; // Time quantum  
} msgbuffer; 

void resetBuffer(struct msgbuffer *buf) { 
	buf->mtype = 0; 
	buf->intData = 0; 
	buf-> TQ = 0;
} 

/*
int random_num(int min, int max) { 
	if (min == max) { 
		return min; 
	} else { 
		return min + rand() / (RAND_MAX / (max-min + 1) + 1);
	}
} */ 

// Limit logfile from reaching more than 10k lines 
int lfprintf(FILE *stream,const char *format, ... ) {
    static int lineCount = 0;
    lineCount++;

    if (lineCount > 10000)
        return 1;

    va_list args;
    va_start(args, format);
    vfprintf(stream,format, args);
    va_end(args);

    return 0;
}

void resetPCB(struct PCB *process) { 
	process->occupied = 0;
    process->pid = 0;
    process->startSeconds = 0;
    process->startNano = 0;
    process->blocked = 0;
    process->eventBlockedUntilSec = 0;
    process->eventBlockedUntilNano = 0;
}

int msqid; 

// Function to find an unoccupied entry in the process table 
int findUnoccupiedEntry(struct PCB* processTable, int size) {
    for (int i = 0; i < size; i++) {
        if (processTable[i].occupied == 0) {
            return i;
        }
    }
    // Return -1 if no unoccupied entry is found
    return -1;
}

void updateUnoccupiedPCB(struct PCB* processTable, int index, pid_t pid, int seconds, int nanoseconds) {
    if (index != -1) {
        processTable[index].occupied = 1;
        processTable[index].pid = pid;
        processTable[index].startSeconds = seconds;
        processTable[index].startNano = nanoseconds;
    } else {
        printf("Error in the PCB\n");
        exit(1);
    }
}

// Function to check if the current time is greater than the blocked time of process
bool currentTimeGreaterThanBlocked(int seconds, int nanoseconds, struct PCB* processTable, int process_key) { 
	return ((seconds > processTable[process_key].eventBlockedUntilSec) || 
            (seconds == processTable[process_key].eventBlockedUntilSec && nanoseconds > processTable[process_key].eventBlockedUntilNano));
}


int main(int argc, char **argv) { 
	// setting up signal handlers 
	if(setupinterrupt() == -1){
        perror("Failed to set up handler for SIGPROF");
        return 1;
    }
    if(setupitimer() == -1){
        perror("Failed to set up the ITIMER_PROF interval timer");
        return 1;
    }
	printf("Initializing Shared memory process\n"); 

	if (randomize()) {
        fprintf(stderr, "Warning: No sources for randomness.\n");
    }


	// Shared memory system clock 
	// Channel for seconds 
	int sh_sec = shmget(SH_KEY1, sizeof (int), 0666 | IPC_CREAT); 
	if (sh_sec == -1) {
		perror("Shared memory get failed in seconds channel\n"); 
		exit(1); 
	}
	int *seconds = shmat(sh_sec, 0, 0); 
	
	int sh_nano = shmget(SH_KEY2, sizeof(int) *10, PERMS | IPC_CREAT);
	if (sh_nano <= 0) {
        fprintf(stderr,"Shared memory get failed in nano\n");
        exit(1);
    }
	int *nanoseconds = shmat(sh_nano, 0, 0); 

	// initializing table 
	for (int i = 0; i < 20; i++) { 
		processTable[i].occupied = 0;
    	processTable[i].pid = 0;
		processTable[i].startSeconds = 0;
		processTable[i].startNano = 0;
		processTable[i].blocked = 0; 
		processTable[i].eventBlockedUntilSec = 0; 
		processTable[i].eventBlockedUntilNano = 0; 
    }

	msgbuffer buf; 
	buf.mtype = 1; 
	buf.TQ = 0; 
	key_t msgkey; 
	system("touch msgq.txt"); 

	// Generating key for message queue
	if ((msgkey = ftok("msgq.txt", 1)) == -1) { 
		perror("ftok"); 
		exit(1); 
	} 
	
	// Creating message queue 	
	if ((msqid = msgget(msgkey, 0666 | IPC_CREAT)) == -1) { 
		perror("msgget in parent"); 
		exit(1); 
	} 
	printf("Message queue sucessfully set up!\n"); 	

	int opt; 
	const char optstr[] = "hn:s:t:i:f:"; 
	int proc = 0; // total number of processes -n 
	int simul = 0; // maximum number of child processes that can simultaneously run -s 
	int timeLimit = PRINT_TABLE; // time limit for children -t
	int interval = 1; // How often we launch a child; if 100, then system would launch a child once every 100. 
	char *filename = NULL; // initialize filename
	
	// oss [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile] 
	while ((opt = getopt(argc, argv, optstr)) != -1) { 
		switch(opt) { 
			case 'h': 
				help(); 
				break; 
			case 'n': 
				proc = atoi(optarg); 
				if (proc <= 0) { 
					printf("Error n arg: must be a positive integer.\n"); 
					help(); 
					exit(EXIT_FAILURE);
				} 
				break;
			case 's': 
				simul = atoi(optarg); 
				if (simul <= 0) {
                    printf("Error simul arg: must be a positive integer.\n");
                    help();
                    exit(EXIT_FAILURE);
                }
				break; 
			case 't': 
				timeLimit = atoi(optarg); 
				break; 
			case 'i': 
				interval = atoi(optarg);
				break; 
			case 'f': 
				filename = optarg; // get the filename 
				break; 
			default: 
				help(); 
				exit(EXIT_FAILURE);
		}
	}

	if (filename == NULL) { 
		printf("Did not read filename \n"); 
		help(); 
		exit(EXIT_FAILURE); 
	} 

	// Create file for oss.c output ONLY
	FILE *fptr = fopen(filename, "w"); 
	if (fptr == NULL) { 
		perror("Error in file creation");	
		exit(EXIT_FAILURE);
	} 


	int launched = 0, simultaneous_count = 0, pid_finished = 0, flag = 0, finished_total = 0, current_child = 0, current_queue = 0, termination_percent = 3, block_percent = 3, time_slice = 0, ready_process = 0; 
	int next_interval_sec, next_interval_nano; 
	double idle_time = 0, wait_time = 0;
	
	pid_t pid; 
	
	// Initialize queues -  q0 is the highest priority queue, q2 is the lowest priority queue. q0 should schedule processes for 10 ms, with the amount of time associated with each queue doubling
	// q0 = 10ms -> q1 = 20ms -> q2 = 40ms 
	struct Queue* q0 = createQueue(); // Highest priority queue 
	struct Queue* q1 = createQueue(); // Doubled from q1
	struct Queue* q2 = createQueue(); // Lowest priority queue 
	struct Queue* block_q = createQueue(); // Blocked priority queue 

	//printf("Entering loop in OSS\n"); 	
	while (finished_total < proc) {
		// Calculating next child if there are simultaneous processes ... 
		if (simultaneous_count > 0) { 
			// Check the blocked queue if there are any 
			if ((block_q-> front) != NULL) { 
				int process_key = (block_q -> front) -> key; // Grabs the key of 1st process in blocked queue 
				// Check if the process has been blocked until the current time or later 
				if (currentTimeGreaterThanBlocked(*seconds, *nanoseconds, processTable, process_key)) {
					deQueue(block_q); // Remove process from blocked 
					enQueue(q0, process_key); // Add to ready Queue 
					incrementClock(seconds, nanoseconds, SCHEDULING_TIME);  
					wait_time += SCHEDULING_TIME; // Increases the total wait time by the constant 
					char *unblocked_message = "OSS: PID %d was unblocked and placed into the ready queue at %d:%d\n"; 
					lfprintf(fptr, unblocked_message, processTable[process_key].pid, *seconds, *nanoseconds); 
					printf(unblocked_message, processTable[process_key].pid, *seconds, *nanoseconds);
					ready_process++; 
				}
			}

		// Check if there are any processes ready to be scheduled  
		if (ready_process > 0) {
			incrementClock(seconds, nanoseconds, SCHEDULING_CONST); 
			wait_time += SCHEDULING_CONST; // Add total wait time by constant; this represents the total time processes spend waiting in the ready queue before they get scheduled 
	
			// Select the next queue and determine which should be used to select next child process
			current_queue = selectNextQueue(q0, q1, q2); 
			// Depending on the selected queue, get key of first process in the queue : 
			switch (current_queue) {
        		case 0:
            		current_child = (q0->front)->key;
            		break;
        		case 1:
            		current_child = (q1->front)->key;
            		break;
        		case 2:
            		current_child = (q2->front)->key;
            		break;
        		default:
            		perror("Queue failed\n");
            		exit(1);
				}
			}
		} else { 
			incrementClock(seconds, nanoseconds, (7*THOUSAND_NANO)); // Increment the clock by 4 seconds (converted to nanoseconds) if no processes are to be scheduled
			idle_time += (7*THOUSAND_NANO); // Increase idle time by the same value ^. Represents the total time the system spends idle without scheduling any processes. 
		} 
		if (simultaneous_count > 0) { 
			// Set message type and data to the PID of the current child process 
			buf.mtype = processTable[current_child].pid; 
			buf.intData = processTable[current_child].pid; 
			
			// Time slices: q0 = 10ms; q1 = 20ms; q2 = 40ms 
			switch (current_queue) {
				case 0:
            		time_slice = 1*MSCONVERSION;
            		break;
        		case 1:
            		time_slice = 2*MSCONVERSION;
            		break;
        		case 2:
            		time_slice = 4*MSCONVERSION;
            		break;
    		}
		 
			buf.TQ = time_slice; 

			// print to log 
			char *dispatch_message = "OSS: Dispatching process with PID %d from queue %d at %d:%d\n"; 
			lfprintf(fptr, dispatch_message, processTable[current_child].pid, current_queue, *seconds, *nanoseconds); 
			printf(dispatch_message, processTable[current_child].pid, current_queue, *seconds, *nanoseconds); 

			// Send message to child process 
			if (msgsnd(msqid, &buf, sizeof(msgbuffer)-sizeof(long), 0) == -1) { 
				perror("msgsnd to child failed\n"); 
				exit(1); 
			} 
		
			// Dequeue from the current queue
			switch (current_queue) {
        		case 0:
            		deQueue(q0);
           			break;
        		case 1:
            		deQueue(q1);
            		break;
        		case 2:
            		deQueue(q2);
            		break;
    		} 

			// Recieve message from the child process 
			if (msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) {
				perror("Failed to recieve message in parent\n"); 
				exit(1); 
			} 

			char *recieved_message = "OSS: Receiving process with PID %d ran for %d nanoseconds\n";

			lfprintf(fptr, recieved_message, processTable[current_child].pid, abs(buf.TQ));
            printf(recieved_message, processTable[current_child].pid, abs(buf.TQ));
	
			// If there are more than 1 simultaneous process, increase wait time by the time quantum 
			if (simultaneous_count > 1) { 
				wait_time += abs(buf.TQ); 
			} 

			incrementClock(seconds, nanoseconds, abs(buf.TQ)); // Increase time as well 
		}
	
		// If the time quantum is less than 0, this indicates a terminated process or a process has terminated 	
		if (buf.TQ < 0) {
			char *terminated_process = "OSS: Received process with PID %d terminated at time %d:%d\n";
			lfprintf(fptr, terminated_process, processTable[current_child].pid, *seconds, *nanoseconds);
            printf(terminated_process, processTable[current_child].pid, *seconds, *nanoseconds);

			wait(0); 
			// Reset PCB Table 
			resetPCB(&processTable[current_child]); 
			// Reset message buffer structure 
			resetBuffer(&buf); 
	
			simultaneous_count--;
			ready_process--; 
			finished_total++; 
		// If the time quantum is greater than 0 and less than the time slice, block the process 
		} else if (buf.TQ > 0 && buf.TQ < time_slice) { 
			enQueue(block_q, current_child); // Add child to blocked queue 
			// Update the PCB to indicate blocked process and set the time 
			processTable[current_child].blocked = 1; 
			processTable[current_child].eventBlockedUntilNano = (*nanoseconds); 
			processTable[current_child].eventBlockedUntilSec++; 
			char *blocked_proc = "OSS: Recieved process with PID %d blocked until time %d:%d at time %d:%d\n";
			lfprintf(fptr, blocked_proc, processTable[current_child].pid, processTable[current_child].eventBlockedUntilSec, processTable[current_child].eventBlockedUntilNano, *seconds, *nanoseconds);
			printf(blocked_proc, processTable[current_child].pid, processTable[current_child].eventBlockedUntilSec, processTable[current_child].eventBlockedUntilNano, *seconds, *nanoseconds);

			resetBuffer(&buf); 
			ready_process--; 
		} else if (simultaneous_count > 0) { 
			// Move the current child process to the next queue based on the current queue 
			char *move_message = "OSS: Process PID %d moved from q%d to q%d at time %d:%d\n"; 
			char *requeue_message = "OSS: Process PID %d requeued to q2 at time %d:%d\n";
			switch (current_queue) {
        		case 0:
            	// Move the process from q0 to q1
            	enQueue(q1, current_child);
            	lfprintf(fptr, move_message, processTable[current_child].pid, current_queue, current_queue + 1, *seconds, *nanoseconds);
            	printf(move_message, processTable[current_child].pid, current_queue, current_queue + 1, *seconds, *nanoseconds);
            	break;
        	case 1:
            	// Move the process from q1 to q2
            	enQueue(q2, current_child);
            	lfprintf(fptr, move_message, processTable[current_child].pid, current_queue, current_queue + 1, *seconds, *nanoseconds);
            	printf(move_message, processTable[current_child].pid, current_queue, current_queue + 1, *seconds, *nanoseconds);
            	break;
        	case 2:
            	// Requeue the process to q2
            	enQueue(q2, current_child);
            	lfprintf(fptr, requeue_message, processTable[current_child].pid, *seconds, *nanoseconds);
            	printf(requeue_message, processTable[current_child].pid, *seconds, *nanoseconds);
            	break;
    		}
			
		}
					
		// Check if a new process can be launched 	
		if (launchNewProcess(flag, launched, proc, simultaneous_count, simul, seconds, nanoseconds, next_interval_sec, next_interval_nano)) {
			ready_process++; 
			simultaneous_count++;  
			flag++; // flag indicates launching a process 
			
			// Calculate the next interval for launching a process 
			next_interval_sec = next_interval_sec + (rand() % (interval + 1)); 
			next_interval_nano = rand() % (int) ONE_SEC_NANO;  
			launched++; // incremeent count of launched processes 
			pid = fork(); 
		} 
		
		// If child process..
		if (pid == 0) {
			char termination_nano[10];  
			sprintf(termination_nano, "%d", timeLimit); 
			char block_probability[10]; 
			sprintf(block_probability, "%d", block_percent); 
			char termination_probability[10]; 
			sprintf(termination_probability, "%d", termination_percent); 
			char *args[] = {"./worker", termination_probability, block_probability, termination_nano}; 

			execlp(args[0], args[0], args[1], args[2], args[3], NULL); 
			printf("Exec failed\n"); 
			exit(1);
		// If this is the parent process and a new process has been launched  
		} else if (pid > 0 && flag > 0) {
			char *generate_proc = "OSS: Generating process PID %d and putting it in queue 0 at time %d:%d\n";
			printf(generate_proc, pid, *seconds, *nanoseconds);
			lfprintf(fptr, generate_proc, pid, *seconds, *nanoseconds);
			
			flag = 0; 
			int i = findUnoccupiedEntry(processTable, sizeof(processTable)/ sizeof(processTable[0])); 
			updateUnoccupiedPCB(processTable, i, pid, *seconds, *nanoseconds); 
			// Add new process to the queue 
			enQueue(q0, i); 
			printTable(getpid(), *seconds, *nanoseconds, processTable); 
		}	
	}
	// Ratio of time CPU was idle to the total time elapsed 
	double CPU_util = idle_time / ((*seconds) * ONE_SEC_NANO + (*nanoseconds)); 
	printf("Amount of CPU utilized: %0.2f%%\n", CPU_util * 100); 
	// Calculates average wait time  
	double avg_wait = wait_time / proc; 
	printf("Average wait time was %0.2f nanoseconds\n", avg_wait); 


	if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl to get rid of queue in parent failed");
        exit(1);
    }
		

	shmdt(seconds);
    shmdt(nanoseconds);
    shmctl(sh_sec, IPC_RMID, NULL);
    shmctl(sh_nano, IPC_RMID, NULL);


	return 0; 
}
	// Functions and variables for my signal stuff 
static void myhandler(int s){
	// variables for signal handler
	int *seconds, *nanoseconds;
	int sh_sec, sh_nano;
    printf("SIGNAL RECIEVED--- TERMINATION\n");
    for(int i = 0; i < 20; i++){
        if(processTable[i].occupied == 1){
           	kill(processTable[i].pid, SIGTERM);
       	}
   	}
 	shmdt(seconds);
   	shmdt(nanoseconds);
   	shmctl(sh_sec, IPC_RMID, NULL); 
   	shmctl(sh_nano, IPC_RMID, NULL);
   	exit(1);
}

static int setupinterrupt(void){
    struct sigaction act;
   	act.sa_handler = myhandler;
   	act.sa_flags = 0;
   	return(sigemptyset(&act.sa_mask) || sigaction(SIGINT, &act, NULL) || sigaction(SIGPROF, &act, NULL));
}

static int setupitimer(void){
    struct itimerval value;
   	value.it_interval.tv_sec = 60;
   	value.it_interval.tv_usec = 0;
   	value.it_value = value.it_interval;
   	return (setitimer(ITIMER_PROF, &value, NULL));
}

