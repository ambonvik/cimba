# Cimba
Discrete event simulation library in C

Work in progress. Using pthreads to execute a mixed 
event- and process-based simulation in parallel on all
available CPU cores. Includes fast pseudo-random number
generators for a wide range of distributions, simple data 
collectors, and common resource types such as buffers and 
queues. 

See test/test_cimba.c for an integrated example simulating 
a M/G/1 queue. On an old Xeon E5 2640, this simulation 
(200 trials, each 1e6 time units) runs in about 25 seconds.

![M/G/1 queue](images/MG1%20example.png)

Current status: Code complete for gcc build chain on
Windows. Next step to add Linux support (expected to
give significantly faster execution due to simpler
context switching), then documentation, before returning
to Windows to add a MVSC build chain.
