# Cimba
## A parallelized discrete event simulation library in C

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
parameter combination. This entire simulation runs in about 25 seconds on an 
old Xeon E5 2640 v4 running Windows 10, producing the data in the chart below.

![M/G/1 queue](images/MG1%20example.png)

Current status: Code complete for gcc build chain on
Windows using CMake. Next step to add Linux support (expected to
give significantly faster execution due to simpler
context switching between coroutines), then documentation, before returning
to Windows to add a MVSC build chain. 64-bit AMD64/x86-64 
architecture only, no plans to add support for 32-bit CPU's. Other 64-bit 
architectures may happen later.
