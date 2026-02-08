.. _welcome:

Cimba - Multithreaded Discrete Event Simulation
===============================================
Cimba is a fast discrete event simulation library written in C and assembly,
providing process-oriented simulation combined with multithreaded parallelism for
high performance on modern CPUs.

Parallelizing discrete event simulation is both a very hard and a trivially simple
problem, depending on the way you look at it. Parallelizing a single simulation
run is near impossible, since all events and processes inside the simulated
world depend on a shared time variable and cannot race ahead.

Luckily, we almost never run only a single simulation run, but a possibly large
experiment consisting of many trials (replications and parameter combinations) to
generate statistical results. These trials are *intended* to be independent,
making them near-trivial to parallelize by simply running them all at the same time,
or at least running as many as you have CPU cores available for.

Why should I use Cimba?
-----------------------
It is fast, powerful, reliable, and free.

* *Fast*: The speed from multithreaded parallel execution translates to high
  resolution in your simulation modelling. You can run hundreds of replications
  and parameter variations in just a few seconds, generating tight confidence
  intervals in your experiments and high density of data points along parameter
  variations.

  In a M/M/1 queue benchmark, Cimba runs about *45 times faster* than SimPy with all
  available cores in use. This corresponds to a 97.8 % reduction in run time. In fact,
  Cimba runs faster on a single core (left chart, about 20 million events per second)
  than SimPy does with all 64 cores (right chart, about 16 million events per second).

  .. image:: ../images/Speed_test_AMD_3970x.png

* *Powerful*: Cimba provides a comprehensive toolkit for well-engineered discrete event
  simulation models, including very large ones.

    * Cimba processes are full asymmetric stackful coroutines. It is possible to pass
      control between processes at any depth of the call stack, not just in a single
      generator function. This enables well-structured coding of arbitrarily large
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

    * As a C program, easy integration with other libraries and programs. You could call
      CUDA routines to enhance your simulation models with GPU-powered agentic behavior
      or drive a fancy graphics interface like a 3D visualization of a manufacturing
      plant. You could even call the Cimba simulation engine from other programming
      languages, since the C calling convention is standard and well-documented.

* *Reliable*: Cimba is well engineered, self-contained open source. There is no mystery to
  the results you get. The code is written with liberal use of assertions to enforce
  preconditions, invariants, and postconditions in each function. The assertions act as
  self-enforcing documentation on expected inputs and outputs from the functions. About
  13 % of all code lines in the Cimba library are assertions.

  There are unit tests for each module. Running the unit test battery in debug mode (all
  assertions active) verifies correct operation in great detail. You can do that by the
  one-liner ``meson test -C build`` from the terminal command line.

* *Free*: Cimba should fit well into the budget of most research groups.

What can I use Cimba for?
-------------------------
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
map with fibonacci hashing for fast access to various objects.

How can I get it?
-----------------
You simply clone the repository from https://github.com/ambonvik/cimba,
build, and install it. You will need a C build chain and the Meson build manager.
On Linux, you can use gcc or Clang, while the recommended approach on Windows is
MinGW with its gcc compiler. For convenience, we use the CLion integrated
development environment with built-in support for our build chain on both
Linux and Windows.

See also the :ref:`installation guide <installation>` for a more detailed description.