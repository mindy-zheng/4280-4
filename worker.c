#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/shm.h>
#include<sys/ipc.h>
#include<math.h> 
#include<string.h> 
#include<sys/msg.h>
#include<stdbool.h>

// By Mindy Zheng

#define SH_KEY1 89918991
#define SH_KEY2 89928992
#define PERMS 0666
#define ONE_SEC_NANO 1000000000 // 1,000,000,000 (billion) nanoseconds is equal to 1 second 

// Message queue struct
typedef struct msgbuffer {   
    long mtype;
    int intData;
    int TQ; // Time quantum                                                 } msgbuffer;
} msgbuffer; 
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

// Function to check if termination time is greater than current 
bool terminationTimeCheck(int term_seconds, int term_nanoseconds, int* seconds, int* nanoseconds) {
    return ((term_seconds == *seconds && term_nanoseconds > *nanoseconds) || term_seconds > *seconds);
}

int main(int argc, char** argv) {
	printf("We are in worker, setting up memory pointers and message buffers\n"); // Debugging statement 
	msgbuffer buf; 
	buf.mtype = 1; 
	int msqid = 0; 
	key_t msgkey; 

	if ((msgkey = ftok("msgq.txt", 1)) == -1) {                                     
		perror("ftok");
        exit(1);
    }
		
	if ((msqid = msgget(msgkey, 0644)) == -1) { 
		perror("msgget in child"); 
		printf("Error in worker: creating message queue"); 
		exit(1); 
	} 
	
	
	// Setting up shared memory pointer for seconds channel 
	int sh_id = shmget(SH_KEY1, sizeof(int) *10, IPC_CREAT | PERMS);
    if (sh_id <= 0) {
        fprintf(stderr,"Shared memory get failed\n");
        exit(1);
    }
    int* seconds = shmat(sh_id, 0, 0);
	
	// Setting up shared memory channel for nanoseconds 
    sh_id = shmget(SH_KEY2, sizeof(int) *10, IPC_CREAT | PERMS);
    if (sh_id <= 0) {
        fprintf(stderr,"Shared memory get failed\n");
        exit(1);
    }
    int* nanoseconds = shmat(sh_id, 0, 0);

	// Create simulated system clock - seconds & nanoseconds 
	int sys_seconds = *seconds; 
	int sys_nano = *nanoseconds; 

	// Constants for termination chance 
	const int termination_probability = atoi(argv[1]); 
	const int IO_block = atoi(argv[2]); 
	int term_seconds = sys_seconds; 
	int term_nanoseconds = atoi(argv[3]) + sys_nano; 

	if (term_nanoseconds > ONE_SEC_NANO) { 
		term_seconds++; 
		term_nanoseconds = term_nanoseconds - ONE_SEC_NANO; 
	} 
	
	if (randomize()) { 
		fprintf(stderr, "Warning: No sources for randomness.\n"); 
	} 
	
	// get process ids 
	int pid = getpid(); 
	int ppid = getppid(); 
	buf.mtype =ppid; 
	buf.intData = ppid; 
	int TQ_percentage; 
	int early_termination = 0; 
	
	while (terminationTimeCheck(term_seconds, term_nanoseconds, seconds, nanoseconds)) { 
		// Implementing message queue to recieve a message 
		if(msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) { 
			perror("Failed to recieve message\n"); 
			exit(1); 
		} 
		// If the random number generates a number less than or equal to the termination probability... then 
		if (rand() % 501 <= termination_probability) { 
			// Calculate percentage of the time quantum used 
			TQ_percentage = rand() % 101; 
			// Update the time quantum to negative to indicate a process has been terminated 
			buf.TQ = -(buf.TQ * termination_probability / 100); 
			buf.mtype = ppid; 
			buf.intData = pid; 
			
			//  Send message to the parent 
			if (msgsnd(msqid, &buf, sizeof(msgbuffer) - sizeof(long), 0) == -1) { 
				perror("msgsnd to parent failed\n"); 
				exit(1); 
			} 
			
			early_termination = 1; // break out the loop 
			break; 
		// If a random number is less than or equal to the probability of an interrupt / block 
		} else if (rand() % 201 <= IO_block) { 
			TQ_percentage = rand() % 101; 
			buf.TQ = buf.TQ * TQ_percentage/100; 
		} 

		// Default if process uses entire time quantum 
		buf.mtype = ppid; 
		buf.intData = pid; 

		if (msgsnd(msqid, &buf, sizeof(msgbuffer)-sizeof(long), 0) == -1) { 
			perror("msgsnd to parent failed.\n"); 
			exit(1); 
		} 
	}
	// If the process did not terminate early 
	if (early_termination == 0) { 
		buf.mtype = ppid; 
		buf.intData = pid; 
		int time_elapsed = (sys_seconds * ONE_SEC_NANO + sys_nano) - (term_seconds * ONE_SEC_NANO + term_nanoseconds); 
		buf.TQ = -(buf.TQ - time_elapsed);
		
		// Send message to the parent process  
		if(msgsnd(msqid, &buf, sizeof(msgbuffer)-sizeof(long), 0) == -1){
            perror("msgsnd to parent failed\n");
            exit(1);
		}
	} 
		
		
	// detatch shared memory channels 
	shmdt(seconds); 
	shmdt(nanoseconds); 
	
	return 0; 
} 

