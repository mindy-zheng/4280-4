// Name: Mindy Zheng
// Date: 2/17/2024 
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

void help() { 
	printf("This program is designed to simulate an operating system scheduler using multi-level feedback queuing:\n"); 
	printf("[-h] - outputs a help message and terminates the program.\n");
	printf("[-n proc] - specifies total number of child processes.\n");
	printf("[-s simul] - specifies maximum number of child processes that can simultaneously run.\n");
	printf("[-t timeLimit for children] - specifies the bound of time a child process will be launched for ... b/w 1 second and -t]\n"); 
	printf("[-i intervalInMs] - specifies how often a children should be launched based on sys clock in milliseconds\n"); 
}

// the Process Table does not have to be in shared memory 
struct PCB { 
	int occupied;	// either true or false
	pid_t pid; 		// process id of current assigned child 
	int startSeconds; 	// start seconds when it was forked
	int startNano; 	// start nano when it was forked 
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
#define MSCONVERSION  1000000
#define ONE_SEC_NANO 1000000000 // 1,000,000,000 (billion) nanoseconds is equal to 1 second

void incrementClock(int *sec, int *nano) {
	(*nano) += NANO_INCREMENT; 
	if ((*nano) >= ONE_SEC_NANO) { 
		(*nano) -= ONE_SEC_NANO; 
		(*sec)++; 
	}
}

// Key for shared memory stuff 
#define SH_KEY1 89918991
#define SH_KEY2 89928992
#define PERMS 0777

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
 
// Message queue struct 
typedef struct msgbuffer { 
	long mtype; 
	int intData; 
	char strData[100]; 
} msgbuffer; 

int random_num(int min, int max) { 
	if (min == max) { 
		return min; 
	} else { 
		return min + rand() / (RAND_MAX / (max-min + 1) + 1);
	}
}

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
	printf("Initializing Shared memory process"); 

	// Shared memory system clock 
	// Channel for seconds 
	int sh_sec = shmget(SH_KEY1, sizeof(int) *10, PERMS | IPC_CREAT); 
	if (sh_sec <= 0) { 
		fprintf(stderr,"Shared memory get failed in seconds\n");
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
    }

	msgbuffer buf; 
	int msqid; 
	buf.mtype = 1; 
	key_t msgkey; 
	system("touch msgq.txt"); 

	// Generating key for message queue
	if ((msgkey = ftok("msgq.txt", 1)) == -1) { 
		perror("ftok"); 
		exit(1); 
	} 
	
	if ((msqid = msgget(msgkey, 0666 | IPC_CREAT)) == -1) { 
		perror("msgget in parent"); 
		exit(1); 
	} 
	printf("Message queue sucessfully set up!\n"); 	

	int opt; 
	const char optstr[] = "hn:s:t:i:"; 
	int proc = 0; // total number of processes -n 
	int simul = 0; // maximum number of child processes that can simultaneously run -s 
	int timeLimit = 0; // time limit for children -t
	int intervalMs = 1 * MSCONVERSION; 
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
				intervalMs = atoi(optarg)*pow(10,6);
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


	int launched = 0, simultaneous_count = 0, pid_finished = 0, flag = 0, finished_total = 0, interval = 0;
	pid_t pid; 
	
	while (finished_total < proc) { 
		// Increment the system clock 
		incrementClock(seconds, nanoseconds); 
		
		// This prints process table every half second 
		if (*nanoseconds % PRINT_TABLE == 0 || *nanoseconds == 0) { 
			printTable(getpid(), *seconds, *nanoseconds, processTable); 
		} 
		
		if (flag == 0 && launched < proc && simultaneous_count < simul && (*nanoseconds) % intervalMs == 0) { 
			flag = 1; // flag indicates launching a process 
			interval = 0; 
			simultaneous_count++; 
			launched++; // incremeent count of launched processes 
			pid = fork(); 
		} 
		
		// If child process..
		if (pid == 0 && flag == 1) { 
			char termination_time[3];
			sprintf(termination_time, "%d", timeLimit); 
			char *args[] = {"./worker", termination_time}; 

			execlp(args[0], args[0], args[1], NULL); 
			printf("Exec failed\n"); 
			exit(1); 
		// Parent process and a new process has simultaneously been launched... 
		} else if (pid > 0 && flag > 0) { 
			// reset and add to the table 
			flag = 0; 
			addPCB(processTable, pid, seconds, nanoseconds); 
		} else if (pid > 0) { 
			pid_finished = waitpid(-1, NULL, WNOHANG); 
			if (pid_finished > 0) { 
				simultaneous_count--; 
				updatePCB(processTable, pid_finished); 	
				pid_finished = 0; 
				finished_total++; 
			}
		}
	}
	printf("Detatching shared memory channels in oss - seconds and nanoseconds\n"); 
	shmdt(seconds);
    shmdt(nanoseconds);
    shmctl(sh_sec, IPC_RMID, NULL);
    shmctl(sh_nano, IPC_RMID, NULL);


	return 0; 
}

	// Functions and variables for my signal stuff 
	int *seconds, *nanoseconds; 
	int sh_sec, sh_nano; 
	static void myhandler(int s){
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

