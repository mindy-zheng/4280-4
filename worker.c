#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/shm.h>
#include<sys/ipc.h>
#include <math.h> 

// By Mindy Zheng

#define SH_KEY1 89918991
#define SH_KEY2 89928992
#define PERMS 0777


int main(int argc, char** argv) {
	printf("We are in worker, setting up memory pointers\n"); // Debugging statement 
	
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

	// Calculate time limit 
	int term_seconds = atoi(argv[1]) + sys_seconds; 
	int term_nanoseconds = sys_nano; 

	int timer = 0; 
	int timeElapsed; 

	// get process ids 
	int pid = getpid(); 
	int ppid = getppid(); 
	
	// Initial state 
	printf("WORKER PID: %d PPID: %d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n----Just Starting\n", pid, ppid, sys_seconds, sys_nano, term_seconds, term_nanoseconds); 
	
	while ((term_seconds == *seconds && term_nanoseconds > *nanoseconds) || term_seconds > *seconds) { 
		timeElapsed = (*seconds - sys_seconds); 

	if (*seconds - timer == 1) { 
			timer = *seconds; 
			printf("WORKER PID: %d PPID: %d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n----%d seconds have passed since starting\n", pid, ppid, *seconds, *nanoseconds, term_seconds, term_nanoseconds, timeElapsed);
        }
	} 
	// Termination state 
	 printf("WORKER PID: %d PPID: %d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n---- TERMINATING\n", pid, ppid, *seconds, *nanoseconds, term_seconds, term_nanoseconds);
			

	printf("Detatching worker shared memory\n"); 	
	// detatch shared memory channels 
	shmdt(seconds); 
	shmdt(nanoseconds); 
	
	return 0; 
} 

