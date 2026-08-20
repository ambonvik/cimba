.. _welcome:

Cimba - Multithreaded Discrete Event Simulation
===============================================
Cimba is a fast discrete event simulation library written in C and assembly,
providing a process-oriented simulation worldview combined with multithreaded
trial parallelism in a shared memory space for high performance on a modern
desktop computer.

Parallelizing discrete event simulation is both a very hard and a trivially simple
problem, depending on the way you look at it. Parallelizing a single simulation
run is hard, since all events and processes inside the simulated world depend
on a shared time variable and cannot race ahead. This requires complex coordination
between the processing nodes to ensure that model causality is maintained. Tools for
this exist, such as the Time Warp algorithm, but are mainly relevant for large-scale
supercomputing, less so for a single workstation or compute server.

Luckily, we rarely want only a single simulation run, but rather a possibly large
experiment consisting of many trials (replications and parameter combinations) to
generate statistical results. These trials are *intended* to be independent,
making them near-trivial to parallelize by simply running them all at the same time,
or at least running as many as you have CPU cores available for.

Somewhat less trivially, we can also use the massive parallelism in today's GPUs to
calculate model physics. The key here is that we do *not* try to parallelize the
simulation events from the event queue, since that is inherently serializing, but
provide hooks for massively parallel GPU computation that is instantaneous in simulated
time without affecting the event queue.

Taken together, Cimba provides a discrete event simulation engine that can utilize
the considerable computing power in a modern multicore, GPU-equipped workstation for
your research purposes.

As far as we know, there is no other tool that can provide this combination.

Main benefits of Cimba
----------------------
It is powerful, fast, reliable, and free.

* *Powerful*: Cimba provides a comprehensive toolkit for well-engineered discrete event
  simulation models, including very large ones, in a process-oriented worldview.

    * Cimba simulated processes are full asymmetric stackful coroutines. It is possible
      to pass control between processes at any depth of the call stack, not just from a
      single generator function. This enables well-structured coding of arbitrarily large
      simulation models. As a first-order object, a simulated process can be passed as
      an argument to other functions, returned from functions, and stored in data
      structures, allowing rich and complex interactions between processes.

    * Cimba provides pre-packaged process interaction mechanisms like resources,
      resource pools, buffers, object queues, priority queues, timeouts, and even
      condition variables where your simulated process can wait for arbitrarily complex
      conditions - anything you can express as a function returning a binary true or
      false result.

    * Cimba includes powerful logging and data collection features that makes it easy
      to get a model running and understand what is happening inside it, including
      custom asserts to pinpoint sources of errors.

    * Cimba includes a wide range of fast, high quality random number generators, both
      academically important and more empirically oriented types. Important
      distributions like normal and exponential are implemented by state-of-the-art
      ziggurat rejection sampling for speed and accuracy.

    * Cimba makes it easy to set up a proper experimental design as an array of trials,
      execute those in parallel, and calculate the necessary statistics, all in a single
      program.

    * As a C program, Cimba is easy to integrate with other libraries and programs. You
      can call CUDA routines for model physics or to enhance your simulation models with
      GPU-powered agentic behavior. You could even call the Cimba simulation engine from
      other programming languages, since the C calling convention is standard and well-
      documented for language bindings.

* *Fast*: The speed from compiled C code and multithreaded parallel execution translates
  to high resolution in your simulation modelling. You can run hundreds of replications
  and parameter variations in just a few seconds, generating tight confidence intervals
  in your experiments and high density of data points along parameter variations.

  In a M/M/1 queue benchmark, Cimba runs about *45-50 times faster* than SimPy with all
  available cores in use. This corresponds to a 98 % reduction in run time.

  In fact, *Cimba runs twice as fast on a single CPU core (left chart, about 32 million
  events per second) than SimPy does with all 64 logical cores (right chart, about 16
  million events per second).*

  .. image:: ../images/Speed_test_AMD_3970x.png

  The reason for this speed difference is that a compiled program in C and hand-rolled
  assembly will always run faster than code that needs to be interpreted on-the-fly at
  runtime.

  The CPU used here has 32 *physical* cores, running two threads per physical core.
  Cimba runs about 1 M events/second on a single core and about 25 M events/second/core
  on 32 physical cores for a scaling efficiency of 76 %.

  Comparing this to the literature on large-scale parallel discrete event simulation
  (PDES), where each simulation trial is distributed across many physical cores,
  `Fujimoto (2015) <https://informs-sim.org/wsc15papers/004.pdf>`_ states that
  performance for the Time Warp-type PDES algorithms has leveled out at around 250 K
  events/second/core on massively parallel supercomputers. Further performance
  improvement in recent years only comes from increasing the number of cores.

  *Cimba runs two orders of magnitude faster than Time Warp PDES measured in events per
  second per core.*

  The reason is, of course, that keeping our entire event queue in "hot" CPU cache memory
  is orders of magnitude faster than communicating the same events across a link between
  separate devices, no matter how fast that link is. The inner loop of identifying and
  executing the next event is the hottest loop in a discrete event simulation. Additional
  delays there will unavoidably slow down the entire simulation. Cimba is built from
  the ground up to make that event-processing loop spin as fast as technically possible.

* *Reliable*: Cimba is well engineered, self-contained open source. There is no mystery to
  the results you get. The code is written with liberal use of assertions to enforce
  preconditions, invariants, and postconditions in each function. The assertions act as
  self-enforcing documentation on expected inputs and outputs from the functions. About
  13 % of all code lines in the Cimba library are assertions.

  There are unit tests for each module. Running the unit test battery in debug mode (all
  assertions active) verifies correct operation in great detail. You can do that by the
  one-liner ``meson test -C build`` from the terminal command line. Moreover, Cimba is
  compatible with sanitizers. A comprehensive test battery with ASan (address
  sanitizer), LSan (memory leak sanitizer), UBSan (undefined behavior sanitizer), and
  TSan (thread sanitizer) runs automatically on every push to the repo. Any issues surfaced
  are promptly fixed.

  Moreover, we have used the latest AI models (e.g., Claude Fable 5) for
  adversial code reviews, pinpointing even obscure errors that would be hard to find in
  traditional regression testing. For transparency, we publish these reviews in the repo,
  with follow-up reviews to verify that all issues are fixed.

* *Free*: Cimba should fit well into the budget of most research groups.

Application areas for Cimba
---------------------------
It is a general purpose discrete event simulation library, in the spirit of a
21st century descendant of Simula67. It may be the right tool for the job if you need
quantitative performance analysis of some system that is so complex that it is
not possible to derive an analytical solution, but where the behavior and interactions
of the constituent parts can be described in C code.

For example, you can use it to model:

* computer networks,

* hospital patient flows,

* transportation networks,

* operating system task scheduling,

* manufacturing systems and job shops,

* military command and control systems,

* queuing systems like bank tellers and store checkouts,

* urban systems like emergency services and garbage collection,

* ...and many other application domains along similar lines.

See :ref:`the tutorials <tutorial>` for illustrations of both expressive power and how to use
it for multi-threaded computing power.

If you look under the hood, you will also find reusable internal components
like stackful coroutines doing their own thing on thread-safe cactus stacks,
fast memory pool allocators for generic small objects, and sophisticated data
structures like hash-heaps combining a binary heap and an open addressing hash
map with Fibonacci hashing for fast access to various objects.

Obtaining and installing Cimba
------------------------------
You simply clone the repository from https://github.com/ambonvik/cimba,
build, and install it. You will need a C build chain and the Meson build manager.
On Linux, you can use gcc or Clang, while the recommended approach on Windows is
MinGW with its gcc compiler. For convenience, we use the CLion integrated
development environment with built-in support for our build chain on both
Linux and Windows.

See also the :ref:`installation guide <installation>` for a more detailed description.