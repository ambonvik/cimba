# Cimba

## A multithreaded discrete event simulation library in C

### What is it?
A very fast discrete event simulation library written in C and assembly for
both Linux and Windows on x86-64 architectures, providing process- and 
event-oriented simulated worldviews combined with multithreaded coarse-trained 
parallelism for high performance on modern CPUs.

Parallelizing discrete event simulation is both a very hard and a trivially simple
problem, depending on the way you look at it. Parallelizing a single simulation
run is near impossible, since all events and processes inside the simulated 
world depend on a shared time variable and cannot race ahead. 
Luckily, we never run only a single simulation
run, but a possibly large experiment consisting of many trials (replications and
parameter combinations) to generate statistical results. These trials are _intended_
to be independent trials, making them near-trivial to parallellize by simply
running them all at the same time, or at least running as many as you have CPU cores
available for. Which is what Cimba does.

### Why should I use it?
* The speed and expressivity translates to high resolution in your simulation
  modelling. You can run hundreds of replications and parameter variations in
  a short time, generating tight confidence intervals in your experiments and
  high density of data points along parameter variations.

* Cimba includes a wide range of fast, high quality random number generators, both 
  of academically important and more empirically oriented types. See 
 [cmb_random.h](include/cmb_random.h)

* Cimba also provides pre-packaged process interaction mechanisms like resources, 
  resource stores, buffers, object queues, and even condition variables where
  your simulated process can wait for arbitrarily complex conditions - essentially for 
  anything you can express as a function returning a binary true or false result.

* Cimba includes powerful logging and data collection features that makes it easy
  to get a model running and understand what is happening inside it.

* Cimba is well engineered, self-contained open source. There is no mystery to the 
  results you get. Each simulated world sits inside its own thread.

* Cimba is free and should as such fit well into the budget of most research groups.

### What can I use Cimba for?
It is a general purpose discrete event library, in the general spirit of a
21st century decendant of Simula67. You can use it
* as a collection of fast random number generators, 
* as a purely event-oriented simulation world view, 
* as a process-oriented simulation world view where your simulated entities take
  on active behaviors and interact in complex ways with each other and with 
  passive objects,
* as a wrapper for multi-threading concurrency on a modern multicore computer,
* or as a mix of all of the above.

See [test_condition.c](test/test_condition.c) for an illustration of the modeling
expressiveness and [test_cimba.c](test/test_cimba.c) for the multithreading.

If you look under the hood, you will also find reuseable internal components
like stackful coroutines doing their own thing on thread-safe cactus stacks,
fast memory pool allocators for generic small objects, and sophisticated data
structures like hash-heaps combining a binary heap and an open adressing hash
map with fibonacci hashing for fast access to various objects. These are not
part of the public Cimba API, but are used internally and part of the codebase.
See [test_condition.c](test/test_condition.c) for an example of how these can
be used in your model code (but please read the relevant source code first).

### So, exactly how fast is it?
The experiment in [test_cimba.c](test/test_cimba.c) simulates a M/G/1 queue at
four different levels of service process variability. For each level, it tries 
five system utilization levels. There are ten replications for each parameter 
combination, in total 4 * 5 * 10 = 200 trials. Each trial lasts for one million 
time units, where the average service time always is 1.0 time unit. This entire 
simulation runs in about 2.7 seconds on an AMD Threadripper 3970X with Arch Linux,
processing some 100-150 million events per second. and producing the chart below. 

![M/G/1 queue](images/MG1%20example.png)

### What do you mean by "well engineered"?
Discrete event simulation fits well with an object-oriented paradigm. That is
why object-oriented programming was invented in the first place for Simula67.
Since OOP is not directly enforced in plain C, we provide the object-oriented
characteristics (such as encapsulation, inheritance, composition, polymorphism, 
message passing, and information hiding) in the Cimba software design instead.

Inside the codebase, you will find namespaces like `cimba_` (overarching code to
manage your experiments and trials), `cmb_`  (used by your simulated world inside
each trial), and `cmi_` (internal stuff that your model does not need to interact 
with). The different functions are then bundled in modules (effectively classes) 
like `cmb_process.h`, containing the API for that part of the library. These
modules form logical inheritance hierarchies, where e.g. a `cmb_process` is a
derived subclass from a `cmi_coroutine`, inheriting all its methods and members.

We distinguish between "is a" (inheritance) and "has a" (composition) relationships.
For example, a `cmb_resource` _is a_ `cmi_holdable`, which _is a_ `cmi_resourcebase`
(a virtual base class), while it _has a_ `cmi_resourceguard` maintaining an orderly
priority queue of waiting processes, where the `cmi_resourceguard` itself _is a_ 
`cmi_hashheap`. Each class of objects has allocator, constructor, destructor, and
de-allocator functions for an orderly object lifecycle, and where derived classes
behave as you would expect with respect to their parent classes.

The code is liberally sprinkled with `assert` statements testing for preconditions,
invariants, and postconditions wherever possible. Cimba contains about 1000 asserts 
in about 10000 lines of code in total, for an assert density of 10 %. These are
custom asserts that will report what trial, what process, the simulated time, the
function and line number, and even the random number seed used, if anything should
go wrong. The asserts come in two flavors, `cmb_assert_debug()` that vanish from 
your code if you define `NDEBUG` in your build (like the standard `assert()`), 
and `cmb_assert_release()` that remain unless `NASSERT` is defined. Most time-
consuming invariants and postconditions are debug asserts, while the release
asserts mostly check preconditions like function argument validity. Turning off
the debug asserts doubles the speed of your model when you are ready for it,
while turning off the release asserts only gives a small incremental improvement.

This is then combined with extensive unit testing of each module, ensuring that
all lower level functionality works as expected before moving on to higher levels. 
We are of course biased, by "well engineered", we mean somewhere 
between "industrial strength" and "mil spec" code.

### Object oriented? In C17 and assembly? Why not just use C++?
Long story made short: C++ exception handling is not very friendly to
the stackful coroutines we need in Cimba. C++ coroutines are something 
entirely different.
Also, C++ has become a very large and feature-rich language, where it will be
hard to guarantee compatibility with every possible combination of features.
Hence (like the Linux kernel), we chose the simpler platform for speed, clarity,
and reliability.

### Version 3.0.0, you say. Why haven't I heard about Cimba before?
Because we did not make it public before. The first ideas that eventually became 
Cimba were built in C++ around 1990. That code ran on VAXstations, where launching a 
coroutine is a single machine instruction. Porting it to other platforms like a
Windows PC was decidedly non-trivial. There is no code remaining from that
predecessor in the current Cimba. What retrospectively can be called Cimba 1.0
was implemented in K&R C at MIT in the early 1990's, followed by a parallelized
version 2.0 in ANSI C and Perl around 1995-96. The present version written in 
C17 with POSIX pthreads is the third major rebuild, and the first public version.

### You had me at "free". How do I get my hands on Cimba?
It is right here. You simply clone the repository, build, and install it. You
will need a C build chain and the Meson build manager. On Linux, you can use gcc 
or Clang, while the recommended approach on Windows is MinGW with its gcc 
compiler. Visual Studio and MVSC should also work, but has not yet been fully 
tested. For convenience, we use the CLion integrated development environment 
with MinGW, gcc, and Meson built-in support on both Linux and Windows.