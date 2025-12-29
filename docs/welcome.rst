.. _welcome:

Cimba - Multithreaded Discrete Event Simulation in C
====================================================

Cimba is a very fast discrete event simulation library written in C and assembly for
both Linux and Windows on x86-64 architectures, providing process- and
event-oriented simulated worldviews combined with multithreaded coarse-trained
parallelism for high performance on modern CPUs.

Parallelizing discrete event simulation is both a very hard and a trivially simple
problem, depending on the way you look at it. Parallelizing a single simulation
run is near impossible, since all events and processes inside the simulated
world depend on a shared time variable and cannot race ahead.

Luckily, we almost never run only a single simulation run, but a possibly large
experiment consisting of many trials (replications and parameter combinations) to
generate statistical results. These trials are *intended* to be independent trials,
making them near-trivial to parallelize by simply running them all at the same time,
or at least running as many as you have CPU cores available for.

Which is what Cimba does.

Why should I use Cimba?
-----------------------

It is powerful, fast, reliable, and free.

* *Powerful*: The speed and expressivity translates to high resolution in your simulation
  modelling. You can run hundreds of replications and parameter variations in
  a few seconds, generating tight confidence intervals in your experiments and
  high density of data points along parameter variations.

    * Cimba includes a wide range of fast, high quality random number generators, both
      academically important and more empirically oriented types.

    * Cimba provides pre-packaged process interaction mechanisms like resources,
      resource stores, buffers, object queues, and even condition variables where
      your simulated process can wait for arbitrarily complex conditions -
      anything you can express as a function returning a binary true or false result.

    * Cimba includes powerful logging and data collection features that makes it easy
      to get a model running and understand what is happening inside it.

* *Fast*: The speed from multithreaded parallel execution translates to high
  resolution in your simulation modelling. You can run hundreds of replications
  and parameter variations in just a few seconds, generating tight confidence
  intervals in your experiments and high density of data points along parameter
  variations.

* *Reliable*: Cimba is well engineered, self-contained open source. There is no mystery to
  the results you get. Each simulated world sits inside its own thread.

* *Free*: Cimba should fit well into the budget of most research groups.

What can I use Cimba for?
-------------------------

It is a general purpose discrete event library, in the spirit of a
21st century descendant of Simula67. You can use it:

* as a collection of fast random number generators,

* as a purely event-oriented simulation world view,

* as a process-oriented simulation world view where your simulated entities take
  on active behaviors and interact in complex ways with each other and with
  passive objects,

* as a wrapper for multi-threading concurrency on a modern multicore computer,

* or as a mix of all of the above.

See the tutorials for illustrations of both expressive power and multi-threaded
computing power.

If you look under the hood, you will also find reusable internal components
like stackful coroutines doing their own thing on thread-safe cactus stacks,
fast memory pool allocators for generic small objects, and sophisticated data
structures like hash-heaps combining a binary heap and an open addressing hash
map with fibonacci hashing for fast access to various objects.

You had me at "free". How do I get my hands on Cimba?
-----------------------------------------------------

It is right here. You simply clone the repository from https://github.com/ambonvik/cimba,
build, and install it. You will need a C build chain and the Meson build manager.
On Linux, you can use gcc or Clang, while the recommended approach on Windows is
MinGW with its gcc compiler. For convenience, we use the CLion integrated
development environment with built-in support for our build chain on both
Linux and Windows.