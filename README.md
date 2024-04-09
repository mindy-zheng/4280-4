# OS-4: Scheduling Simulator 
-----------------------------------
For this project, we'll implement a queue-based scheduler and use message queues for synchronization. We have 3 queues representing different priorities (from highest to lowest): q0, q1, and q2. Each process is assigned a TQ (time quantum), and a message queue is impelmented for inter-process communication. The messages represent a simulated scheduler with the parent being the scheduler.  

## How to run: 
Run 'make'. 
	Ex: ./oss -n 10 -s 2 -t 10 -i 100 -f logfile.txt 


## Sources 
- Queue: https://www.geeksforgeeks.org/queue-linked-list-implementation/
- Random number generator: https://stackoverflow.com/questions/62649232/use-rand-in-c-to-get-a-different-number-every-run-without-using-time-or-getp

