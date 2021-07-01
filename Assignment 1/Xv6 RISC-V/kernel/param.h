#define NPROC 64                  // maximum number of processes
#define NCPU 8                    // maximum number of CPUs
#define NOFILE 16                 // open files per process
#define NFILE 100                 // open files per system
#define NINODE 50                 // maximum number of active i-nodes
#define NDEV 10                   // maximum major device number
#define ROOTDEV 1                 // device number of file system root disk
#define MAXARG 32                 // max exec arguments
#define MAXOPBLOCKS 10            // max # of blocks any FS op writes
#define LOGSIZE (MAXOPBLOCKS * 3) // max data blocks in on-disk log
#define NBUF (MAXOPBLOCKS * 3)    // size of disk block cache
#define FSSIZE 1000               // size of file system in blocks
#define MAXPATH 128               // maximum file path name
#define QUANTUM 5                 // size of clock tick
#define ALPHA 50                  // alpha burst approximation
#define INT_MAX 2147483647        // max int value
#define Test_High_Priority 1      // test high priority decay factory value
#define High_Priority 3           // high priority decay factory value
#define Normal_Priority 5         // normal priority decay factory value
#define Low_Priority 7            // low priority decay factory value
#define Test_Low_Priority 25      // test low priority decay factory value