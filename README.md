![large logo](images/logo_large.JPG)

## A multithreaded discrete event simulation library in C

### What is it?
A very fast discrete event simulation library written in C and assembly for
both Linux and Windows on x86-64 architectures, providing process- and 
event-oriented simulated world views combined with multithreaded coarse-trained 
parallelism for high performance on modern CPUs.

Parallelizing discrete event simulation is both a very hard and a trivially 
simple problem, depending on the way you look at it. Parallelizing a single 
simulation run is near impossible, since all events and processes inside the 
simulated world depend on a shared time variable and cannot race ahead.

Luckily, we almost never run only a _single_ simulation run, but a possibly 
large experiment consisting of many trials (replications and parameter 
combinations) to generate statistical results. These trials are _intended_
to be independent trials, making them near-trivial to parallelize by simply
running them all at the same time, or at least running as many as you have CPU 
cores available for.

Which is what Cimba does.

### Why should I use it?
It is powerful, fast, reliable, and free.

* *Powerful*: Cimba provides a comprehensive toolkit for discrete event simulation:

  * Support for both process- and event-based simulation world views, and 
    combinations of the two.
  
  * Pre-packaged process interaction mechanisms like resources,
    resource stores, buffers, object queues, and even condition variables where
    your simulated process can wait for arbitrarily complex conditions – essentially
    for anything you can express as a function returning a binary true or false result.
  
  * A wide range of fast, high-quality random number generators, both
    of academically important and more empirically oriented types.
  
  * Integrated logging and data collection features that make it easy
    to get a model running and understand what is happening inside it.

* *Fast*: The speed from multithreaded parallel execution translates to high 
  resolution in your simulation modeling. You can run hundreds of replications 
  and parameter variations in just a few seconds, generating tight confidence 
  intervals in your experiments and a high density of data points along parameter 
  variations.

* *Reliable*: Cimba is well-engineered open source. There is no
  mystery to the results you get. Each simulated world sits inside its own thread.

* *Free*: Cimba should fit well into the budget of most research groups.

### What can I use Cimba for?
It is a general-purpose discrete event library, in the general spirit of a
21st century descendant of Simula67. You can use it
* as a collection of fast random number generators, 
* as a purely event-oriented simulation world view, 
* as a process-oriented simulation world view where your simulated entities take
  on active behaviors and interact in complex ways with each other and with 
  passive objects,
* as a wrapper for multi-threading concurrency on a modern multicore computer,
* or as a mix of all of the above.

See the tutorial examples at [tut_1_7.c](tutorial/tut_1_7.c), 
[tut_2_2.c](tutorial/tut_2_2.c), and [tut_3_1.c](tutorial/tut_3_1.c) for 
illustrations of both model expressiveness and multithreading.

If you look under the hood, you will also find reusable internal components
like stackful coroutines doing their own thing on thread-safe cactus stacks,
fast memory pool allocators for generic small objects, and sophisticated data
structures like hash-heaps combining a binary heap and an open addressing hash
map with fibonacci hashing for fast access to various objects. These are not
part of the public Cimba API but are used internally and part of the codebase.

### So, exactly how fast is it?
The experiment in [test_cimba.c](test/test_cimba.c) simulates an M/G/1 queue at
four different levels of service process variability. For each level, it tries 
five system utilization levels. There are ten replications for each parameter 
combination, in total 4 * 5 * 10 = 200 trials. Each trial lasts for one million 
time units, where the average service time always is 1.0 time units. This entire 
simulation runs in about 2.7 seconds on an AMD Threadripper 3970X with Arch Linux,
processing some 100–150 million events per second, producing the chart below. 

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
modules form logical inheritance hierarchies, where e.g., a `cmb_process` is a
derived subclass from a `cmi_coroutine`, inheriting all its methods and members.

We distinguish between "is a" (inheritance) and "has a" (composition) relationships.
For example, a `cmb_resource` _is a_ `cmi_holdable`, which _is a_ `cmi_resourcebase`
(a virtual base class), while it _has a_ `cmb_resourceguard` maintaining an orderly
priority queue of waiting processes, where the `cmi_resourceguard` itself _is a_ 
`cmi_hashheap`. Each class of objects has allocator, constructor, destructor, and
de-allocator functions for an orderly object lifecycle, and where derived classes
behave as you would expect with respect to their parent classes.

The code is liberally sprinkled with `assert` statements testing for preconditions,
invariants, and postconditions wherever possible, applying Design by Contracts 
principles for reliability. Cimba contains about 1000 asserts in about 10 000 lines of 
code in total, for an assert density of 10 %. These are custom asserts that will report 
what trial, what process, the simulated time, the function and line number, and even the 
random number seed used, if anything should go wrong. All time-consuming invariants and 
postconditions are debug asserts, while the release asserts mostly check preconditions 
like function argument validity. Turning off the debug asserts doubles the speed of your
model when you are ready for it, while turning off the release asserts as well only gives 
a small incremental improvement.

This is then combined with extensive unit testing of each module, ensuring that
all lower level functionality works as expected before moving on to higher levels. 
You will find the test files corresponding to each code module in the `test` directory.

But do read the [LICENSE](LICENSE). We do not give any warranties here.

### Object-oriented? In C17 and assembly? Why not just use C++?
Long story made short: C++ exception handling is not very friendly to the stackful 
coroutines we need in Cimba. C++ coroutines are something entirely different.

C++ has also become a very large and feature-rich language, where it will be
hard to ensure compatibility with every possible combination of features.

Hence (like the Linux kernel), we chose the simpler platform for speed, clarity,
and reliability. If you need to call Cimba from some other language, the C calling
convention is well-known and well-documented.

### Version 3.0.0, you say. Why haven't I heard about Cimba before?
Because we did not make it public before. The first ideas that eventually became 
Cimba were built in C++ around 1990. What retrospectively can be called Cimba 1.0
was implemented in K&R C at MIT in the early 1990's, followed by a parallelized
version 2.0 in ANSI C and Perl around 1995–96. The present version written in 
C17 with POSIX pthreads is the third major rebuild, and the first public version.

### You had me at "free." How do I get my hands on Cimba?
It is right here. You clone the repository, build, and install it. You
will need a C compiler and the Meson build manager. On Linux, you can use GCC 
or Clang, while the recommended approach on Windows is MinGW with its GCC 
compiler. For convenience, we recommend the CLion integrated development environment 
with GCC, Meson, and Ninja built-in support on both Linux and Windows.