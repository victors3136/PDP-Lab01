# Example output

[Main] Starting worker threads  
[Thread 2] Start  
[Thread 3] Start  
[Thread 4] Start  
[Thread 4] Update 7 by -8  
[Thread 2] Update 1 by -9  
[Main] waiting for workers  
[Thread 6] Start  
[Thread 6] Update 7 by -1  
[Thread 3] Update 0 by 9  
[CC] Starting  
[CC] Success:  
[{0 : 0}, {1 : 0}, {2 : 0}, {3 : 0}, {4 : 0}, {5 : 0}, {6 : 0}, {7 : 0}, {8 : 0}, {9 : 0}, {10 : 0}]  
[Thread 7] Start  
...  
[Thread 3] Update 2 by 2  
[Thread 5] Update 2 by 8  
[Thread 2] Update 0 by -1  
[Thread 3] Update 2 by -8  
[Thread 5] End  
[Thread 3] Update 3 by 7  
[Thread 2] End  
[Thread 3] Update 0 by -2  
[Thread 3] Update 1 by 0  
[Thread 3] Update 2 by -3  
[Thread 3] Update 1 by 3  
[Thread 3] End  
[Main] gathered threads  
[CC] Starting  
[CC] Success:  
[{0 : -24}, {1 : -12}, {2 : 2}, {3 : 9}, {4 : 9}, {5 : -10}, {6 : -6}, {7 : -10}, {8 : -16}, {9 : 2}, {10 : -24}]  