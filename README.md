# Cimba

## A multithreaded discrete event simulation library in C

Work in progress. Using pthreads to execute a threadsafe mixed model 
event- and process-based discrete event simulation in parallel on all
available CPU cores. Includes fast pseudo-random number
generators for a wide range of distributions, simple data 
collectors, and common resource types such as buffers and 
queues. Optimized for speed, modularity, and an intuitive API.
Written in an object-oriented style of C with certain sections in assembly.

See test/test_cimba.c for an integrated example simulating 
a M/G/1 queue. The experiment consists of 200 trials, each 1e6 time units, 
average service time 1.0 time units in every trial, 10 replications of each 
parameter combination. This entire simulation runs in about 4.8 seconds on an 
AMD Threadripper 3970X with Arch Linux, producing the data in the chart below.
The simulation processed about 75 million events per second.

![M/G/1 queue](images/MG1%20example.png)

Current status: Code complete for gcc/gwmin build chain on Linux and Windows.
Now updating documentation, before returning to Windows to add MVSC build chain. 
64-bit AMD64/x86-64 architecture only, no plans to add support for 32-bit CPU's.
