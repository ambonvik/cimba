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

  In a M/M/1 queue benchmark, Cimba runs about *30-60 times faster* than SimPy with all
  available cores in use. This corresponds to a 98 % reduction in run time.

  In fact, *Cimba runs more than twice as fast on a single CPU core (left chart, about 42
  million events per second) than SimPy does with all 64 logical cores (right chart, about
  16 million events per second).*

  .. image:: ../images/Speed_test_AMD_3970x.png

  (Cimba built with gcc options `-O3 -fprofile-use -DNDEBUG -DNLOGINFO -DNASSERT
  -DNMXCSR`, SimPy running on Python 3.14.7, host system AMD Ryzen Threadripper 3970x.)

  The reason for this speed difference is that a compiled program in C and hand-rolled
  assembly will always run faster than code that needs to be interpreted on-the-fly at
  runtime.

  The CPU used here has 32 *physical* cores, running two threads per physical core.
  Cimba runs about 42 M events/second on a single core and about 28 M events/second/core
  on 32 physical cores for a scaling efficiency of 67 %. This also compares favorably to
  `the literature on large-scale parallel discrete event simulation <https://informs-sim.org/wsc15papers/004.pdf>`_
  (PDES), where each simulation trial is distributed across many physical cores,
  where it seems that  performance for the Time Warp-type PDES algorithms has leveled
  out at around 250 K events/second/core on massively parallel supercomputers.

  *Cimba runs two orders of magnitude faster than Time Warp PDES measured in events per
  second per core.*

  The reason is that keeping our entire event queue in "hot" CPU cache memory
  is orders of magnitude faster than communicating the same events across a link between
  separate devices, no matter how fast that link is.

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

If you look under the hood, you will also find additional reusable internal components.
Cimba contains stackful coroutines doing their own thing on thread-safe cactus stacks.
There are fast memory pool allocators for generic small objects, intrusive linked
lists, and hash-heaps combining a binary heap and an open addressing hash map using
Fibonacci hashing. Although not part of the public Cimba API, these components can also
be used in your model if needed, but be aware that anything in the ``cmi_`` namespace may
change in future (minor) versions.

Reproducibility
---------------
Cimba is built to give reproducible results by controlling the pseudo-random number
seeds. Running a simulation again with the same seed will give the same result. Still,
since Cimba uses double precision floating point numbers for its time variables and
hence the event ordering, it will be subject to rounding errors in the underlying
floating point math implementation. These can differ between platforms.

It is worth spelling out the exact limits for reproducibility when provided the same seed:

*Same Cimba version, same hardware, same version of the compiler and the math library,
same compiler options:* Will give identical results. A single trial in a multithreaded
experiment will also give identical results to the same trial run singlethreaded with
the same trial seed.

*Same Cimba version, hardware, compiler and math library, different compiler options:*
Mostly identical, with one important exception: Defining the option ``-DNMXCSR`` used, e
.g., in a profiler-guided fully speed optimized Cimba build will not save and restore
the MXCSR register in coroutine context switches. This register contains x86-64
SSE/AVX SIMD floating-point control and status bits, such as the rounding mode.

If the ``-DNMXCSR`` is *not* set, this register will be initialized to inherit the
floating point control bits of the parent and maintained as a per-coroutine value after
that. At the end of a coroutine, the content of that coroutine's ``MXCSR`` is lost. Any
``NaN``'s or ``Inf``'s generated may still be propagated into trial results, even if the
main program's ``MXCSR`` will not carry any exception flags. Each coroutine can set
its own rounding mode or masking bits without changing those of other coroutines. This
is the default behavior.

If the ``-DNMXCSR`` *is* set, the ``MXCSR`` register is *not* initialized or maintained
per coroutine, but continues to exist as a global state. Any floating point exceptions
or control flags raised in one coroutine will affect all others, also after the end
of the coroutine. This may give subtly different numerical values than the default if
the user program sets specific rounding flags from within the simulated processes.

For full details about the x86-64 ``MXCSR`` register, see the
`Intel <https://software.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-1-manual.pdf>`_,
section 10.2.3.

*Same Cimba version, different hardware (AMD vs Intel CPU, say), different compiler, or
different versions of the compiler and its libraries:* There may be numerical differences.
For example, transcendental functions like logarithms and exponentials may round
differently at a scale of 1e-15 or so. Taking a difference between two almost equal
numbers calculated this way *will* give different numerical values on e.g., Ubuntu and
Arch Linux distros.

If a model schedules events at time values containing the result from such
calculations, it might (in rare cases) get different ordering of events between
platforms. This is a property of floating point math, not of Cimba as such. See
`the Wikipedia article on rounding <https://en.wikipedia.org/wiki/Rounding#Table-maker's_dilemma>`_
for more details.

Also note that the evaluation order of C arguments is not specified in the standard.
That implies that the compiler is free to execute this statement

  .. code-block:: c

    cmb_event_schedule(action, subject, object, cmb_time() + cmb_random_exponential(10.0), cmb_random_dice(1, 5));

as either

  .. code-block:: c

    const double t = cmb_time() + cmb_random_exponential(10.0);
    const int64_t p = cmb_random_dice(1, 5);
    cmb_event_schedule(test_action, subject, object, t, p);

or as

  .. code-block:: c

    const int64_t p = cmb_random_dice(1, 5);
    const double t = cmb_time() + cmb_random_exponential(10.0);
    cmb_event_schedule(test_action, subject, object, t, p);

Each ``cmb_random_`` call returns a pseudo-random number. The values returned for ``t``
and ``p`` will depend on the sequence they are drawn in. If two compilers choose
different evaluation orders of the embedded function calls, your simulation will schedule
the event at different times and with different priorities. Always make the evaluation
order explicit by writing out the sequence as one of the two examples above if you want
to have identical sequences of events across compilers.

*Different Cimba versions:* Results may or may not be identical. In semantic versioning,
patch versions are by definition bug fixes. A bug worth fixing probably had some
influence on the output of some model, and fixing it produces more correct output.
Also, Cimba logging messages include code line numbers. These may change between
versions, and model output with detailed logging may not be bitwise identical even if
it is logically and numerically identical.


Obtaining and installing Cimba
------------------------------
You simply clone the repository from https://github.com/ambonvik/cimba,
build, and install it. You will need a C build chain and the Meson build manager.
On Linux, you can use gcc or Clang, while the recommended approach on Windows is
MinGW with its gcc compiler. For convenience, we use the CLion integrated
development environment with built-in support for our build chain on both
Linux and Windows.

See also the :ref:`installation guide <installation>` for a more detailed description.