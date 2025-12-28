.. _background:

The Whys and Hows of Cimba, Explained
=====================================

In this section, we will explain the background for Cimba and some of
the key design choices that are made in it. We start with a brief history that is
necessary background for the project goals.

<This section is still work in progress, please check back later to read the rest.>

Project History and Goals
-------------------------

Cimba 3.0.0 is released on GitHub as public beta in late 2025, but as the version number
indicates, there is some history before this first public release.

The earliest ideas that eventually became Cimba date to work done at the Norwegian Defence
Research Establishment in the late 1980s. I built discrete event simulation models
in languages like Simscript and Simula67. Encountering Simula67's coroutines and
object-orientation was revelatory in its essential *rightness*. However, Simula67 was
still severely limited in many other respects and not really a practical option at that
time.

Around 1990, we started building discrete event simulation models in C++ as early adopters
of that language. The first C++ models ran on VAXstations, where spawning a coroutine is
a single assembly instruction. Trying to port that code to a Windows PC was a somewhat
painful experience (and a complete failure). I actually complained to Bjarne Stroustrup in
person about the inconsistent to non-existent support for Simula-like coroutines in C++ at
a conference in Helsingør, probably in 1991. He seemed to agree but I silently resolved
to build my next simulation model in pure Kernighan & Richie C. Which I did.

That opportunity arose at MIT around 1994, where I needed a discrete event simulation
model for cross-checking analytical models of manufacturing systems. For perhaps obvious
reasons, this was a clean sheet design with no code carried forward from the earlier
C++ work at NDRE. It had a collection of standard random number generators and
distributions, and used a linked list for its main event queue. It did the job, running
on a Linux PC, but could be improved. In retrospect, we consider this Cimba version 1.0.

For my PhD thesis research, I needed to run *many* simulations with various parameter
combinations and replications. By then, I had realized that parallelizing a discrete
event simulation model is trivially simple if one looks at it with a telescope instead
of using a microscope. The individual replications are *meant* to be independent
identically distributed trials, implying that there is nearly no interaction between them
at runtime. One can just fork off as many replications in parallel as one has computing
resources for, and use one computing node to dole out the jobs and collect the
statistics.

This was lashed together at the operating system process level using ``rsh`` and a Perl
script to control the individual simulations on a cluster of workstations, and just to
make a point, on at least one computer back at the Norwegian Defence Research
Establishment for a trans-Atlantic distributed simulation model. At the same time, the
core discrete event simulation was rewritten in ANSI C with a binary heap event queue. It
needed to be very efficient and have a very small memory footprint to run at low priority
in the background on occupied workstations without anyone noticing a performance drop.
This was pretty good for 1995, and can safely be considered Cimba version 2.0, but it was
never released to the public.

After that, not much happened to it, until I decided to dust it off and publish it as
open source many years later. The world had evolved quite a bit in the meantime, so the
code required another comprehensive re-write to exploit the computing power in modern
CPU's, this time coded in C17. This is the present Cimba 3.0.

The goals for Cimba 3.0 are quite similar to those for earlier versions:

* Speed and efficiency, where small size in memory translates to execution speed on
  modern CPUs with cached memory pipelines, and multithreading trials on CPU cores
  provides the parallelism.

* Portability, running both on Linux and Windows, initially limited to the AMD64 /
  x86-64 architecture and GCC-like compilers.

* Expressive power, combining process-oriented and event-oriented simulation
  worldviews with a comprehensive collection of state-of-the-art pseudo-random number
  generators and distributions.

* Robustness, using object-oriented design principles and comprehensive unit testing to
  ensure that it works as expected (but do read the Licence, we are not making any
  warranties here).

I believe that Cimba 3.0 meets these goals and hope you will agree.

Coroutines and Processes
------------------------

It is well known that Simula introduced object-oriented programming, see
https://en.wikipedia.org/wiki/Simula for the story. For those of us that
were lucky enough (or just plain old enough) to actually have programmed in Simula67, the
object-orientation with classes and inheritance was only part of the experience, and
perhaps not the most important part.

The most powerful concept for simulation work was the *coroutine*. This
generalizes the concept of a subroutine to several parallel threads of execution that
co-exist and are non-preemptively scheduled. Combined with object-orientation, this
means that one can describe a class of objects as independent threads of execution, often
infinite loops, where the object's code just does its own thing. The complexity in the
simulated world then arises from the interactions between the active processes and various
other passive objects, while the description of each entity's actions is very natural.

Coroutines received significant academic interest in the early years, but were then
overshadowed by the object-oriented inheritance mechanisms. It seems that current
trends are turning away from the more complex inheritance mechanisms, in many cases
using composition instead of (multiple) inheritance, and also reviving the interest in
coroutines. One important article is
https://www.cs.tufts.edu/~nr/cs257/archive/roberto-ierusalimschy/revisiting-coroutines.pdf

Unfortunately, when C++ finally got "coroutines" as a part of the language in 2020,
these turned out both to be less powerful and less efficient than expected. (See
https://probablydance.com/2021/10/31/c-coroutines-do-not-spark-joy/ for the details.)
For our purposes here, it is sufficient to say that these coroutines are not the
coroutines we are looking for.

In Cimba, we have some additional requirements to the coroutines beyond being full-fledged
coroutines, i.e., stackful first class objects. Our coroutines need to be thread-safe,
since we will combine these with multithreading at the next higher level of concurrency.
The Cimba coroutines will interact in parallel universes within each thread.

We also want our coroutines to share information both through pointer arguments to the
context-switching functions ``yield()``, ``resume()``, and ``transfer()``, and by the
values returned by these functions. Control is passed out of a coroutine by calling one
of these functions. Control is also passed back to the coroutine by what appears to be a
normal return from this function call. The two ways of communicating between coroutines
are then sharing a pointer to some mutable context data structure as an argument, and
returning a signal value that can be used to determine if something unexpected happened
during the call. We will use both, since these give very different semantics.

Moreover, we want our coroutines to return an exit value if and when they terminate, and
we want the flexibility of either just returning this exit value from the coroutine
function or by calling a special ``exit()`` function with an argument. These should be
equivalent, and the exit value should be persistent after the coroutine execution ends.

Cimba coroutines
^^^^^^^^^^^^^^^^

We are not aware of any open source coroutine implementation that exactly meets these
requirements, so Cimba contains its own, built from the ground up. There are several
parts to the Cimba coroutine implementation; the *context switch*, the *trampoline*,
the *data structure* with the stack and stack pointer, and the *setup code*.

The *context switch* is straightforward but system dependent assembly code. It stores
the active registers from the current execution context, including the stack pointer,
loads the target stack pointer and registers, puts the apparent return value into the
correct register, and "returns" to wherever the target context came from. See the
function ``cmi_coroutine_context_switch`` in
`src/port/x86-64/linux/cmi_coroutine_context.asm <https://github.com/ambonvik/cimba/blob/main/src/port/x86-64/linux/cmi_coroutine_context.asm>`_
and
`src/port/x86-64/windows/cmi_coroutine_context.asm <https://github.com/ambonvik/cimba/blob/main/src/port/x86-64/windows/cmi_coroutine_context.asm>`_
for Linux and Windows, respectively.

The *trampoline* is a function that gets pre-loaded onto a new coroutine's stack before
it starts executing. It is never called directly. Once started, the trampoline will
call the actual coroutine function and then silently wait for it to return. If it ever
does, the trampoline will catch it and call the ``exit()`` function with the
return value as argument, giving exactly the same effect of a ``return ptr;`` as a
``exit(ptr);``, because it becomes the same thing. The code is in the same assembly
files as above, function ``cmi_coroutine_trampoline``.

The *data structure* ``struct cmi_coroutine`` is defined in `src/cmi_coroutine.h
<https://github.com/ambonvik/cimba/blob/main/src/cmi_coroutine.h>`_. It
contains pointers to the coroutine stack, its parent and latest caller coroutines, the
coroutine function, its context argument, and the coroutine's status and exit value.

Finally, the *setup code* initializes a new coroutine stack and makes it ready to start
execution. This is system-dependent C code, function ``cmi_coroutine_context_init`` in
`src/port/x86-64/linux/cmi_coroutine_context.c <https://github.com/ambonvik/cimba/blob/main/src/port/x86-64/linux/cmi_coroutine_context.c>`_
and
`src/port/x86-64/windows/cmi_coroutine_context.c <https://github.com/ambonvik/cimba/blob/main/src/port/x86-64/windows/cmi_coroutine_context.c>`_.
Basically, it fakes the stack frame that a function would see when executing normally on
the new stack, with the trampoline at its return address, and then transfers control into
the new coroutine. This starts executing the trampoline function, which in turn starts the
actual coroutine function as described above.

This machinery is hidden from the user application which only needs to provide a
coroutine function using the ``yield()``, ``resume()``, and/or ``transfer()`` primitives
to run either asymmetric or symmetric coroutines. The coroutine function
prototype is ``void *(cmi_coroutine_func)(struct cmi_coroutine *cp, void *context)``,
i.e., a function that takes a pointer to a coroutine (itself) and a ``void *`` to some
user application-defined context as arguments and returns a ``void *``.

For even more flexibility, we also allow the user application to define what ``exit()``
function to be called if/when the coroutine function returns. This may seem like an
intricate way of calling the exit function indirectly by returning from the coroutine
function instead of just calling it directly, but as we will see, we will use that
feature at the next higher level.

This gives us a very powerful set of coroutines, fulfilling all requirements to "full"
coroutines, and in addition providing general mechanisms for communication between
coroutines. The Cimba coroutines can both be used as symmetric or as asymmetric
coroutines, or even as a mix of those paradigms by mixing asymmetric yield/resume pairs
with symmetric transfers. (Debugging your program may become rather confusing, though.)

Cimba processes - named coroutines
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Our coroutines are a bit too general and powerful for simulation modeling. We use these
as internal building blocks for the Cimba *processes*. These are essentially named
asymmetric coroutines, inheriting all properties and methods from the coroutine class,
and adding a name, a priority for scheduling processes, and pointers to things it may be
waiting for, resources it may be holding, and other processes that may be waiting for it.

The processes also understand the simulation time, and may ``hold()`` for a certain
amount of simulated time. Underneath this call are the coroutine primitives of ``yield()``
(to the simulation dispatcher) and ``resume()`` (when the simulation clock has advanced
by this amount). Processes can also ``acquire()`` and ``release()`` resources, wait for
some other process to finish, interrupt or stop other processes, wait for some specific
event, or even wait for some arbitrarily complex condition to become true.

When initializing a process, we provide the process ``exit()`` function to be called by
the coroutine trampoline if the process should ever return. The reason is simple:
The parent coroutine class should not have any privileged knowledge about the content
of its child classes. Hence the coroutine module cannot just hard-code this function, but
needs to be handed it as a callback function from the derived class at initialization.

We will soon return to Cimba's processes and their interactions, but if the reader has
been paying attention, there is something else we need to address first: We just said
*"inheriting all properties and methods from the coroutine class"*, but we also just
said "C17" and "assembly".

Object-Oriented Programming. In C and Assembly.
-----------------------------------------------

Object-oriented programming is a way of structuring program design, not necessarily a
language feature. It uses concepts like *encapsulation*, *inheritance*, and
*polymorphism* to create a natural descriptions of the objects in the program.

* *Encapsulation* bundles the properties and methods of objects into a compact
  description, usually called a *class*. The individual *instances*, objects that
  belong to the same class, have the same structure but may have different data values.

* *Inheritance* is the relationship between classes where a class is derived from
  another *parent* class. Objects belonging to the child class also belong to the parent
  class and inherit all properties and methods from there. The child class adds its own
  methods and may *overload* (change) the meaning of parent class methods.

* *Polymorphism* allows the program to deal with parent classes and have each child
  class fill in the details of what should be done. The canonical example is a class
  *shape* with a *draw()* method, where the different child classes *circle*,
  *rectangle*, *triangle*, etc, all contain their own *draw()* method. The program can
  then have a list of *shapes*, ask each one to *draw()* itself, and have the different
  subtypes of shapes do the appropriate thing.

This can easily be implemented in a language like C, without explicit support from
programming language features. The key observation is that the first member of a C
``struct`` is guaranteed to have the same address in memory as the struct itself. We
can then use structs as classes, encapsulating the properties of our "class" as struct
members, and implement inheritance by making the parent ``struct`` the first member of
a derived "class". A pointer to a child class object is then also a pointer to a parent
class object, exactly as we want.

Polymorphism can be implemented by having pointers to functions as struct members, and
then place the appropriate function there when initializing the object. If necessary,
this can be extended by having a dedicated ``vtable`` object for the class to avoid
multiple copies of the function pointers in each class member object, at the cost of
one more redirection per function call.

The encapsulation is then a matter of disciplined modularity in structuring the code,
with the code and header files as a main building block. Careful use of ``static`` and
``extern`` functions and variables provide the equivalents of ``private``, ``public``,
and ``friend`` class properties and methods.

Even if this is the most natural way of describing the entities in our simulated world,
there are other things there that might be less natural to describe as classes and
objects. In particular, we do not consider the pseudo-random number generators objects
in this sense. They just exist in the simulated world and be called without the
complexities of creating an object-oriented framework around them. A clear, modular
structure to encapsulate and protect internal complexities is still needed.

Cimba functions and variables follow a naming convention of
*<namespace>_<module>_<function>*. There are three namespaces:

* *cimba* - functions and objects at the outer level, organizing and executing your
  multithreaded simulation experiment as a series of trials. This is outside the simulated
  world. Example: ``cimba_version()``

* *cmb* - functions and objects in the simulated world. These are the building blocks of
  your simulation. You will probably build a single-threaded version only using
  functions from this namespace first, and parallelize it later when you need the
  computing power. Example: ``cmb_random_uniform()``

* *cmi* - internal functions and objects that for some reason need to be exposed
  globally, but that your simulation model does not need to interact with. Example:
  ``cmi_coroutine_create()``

Static functions and variables internal to each module do not use the
*<namespace>_<module>_* prefix since they do not have global scope.

There is one notable exception to this naming convention: The function ``cmb_time()``,
which returns the current simulation clock value. It is declared and defined in the
``cmb_event`` module, and should perhaps be called ``cmb_event_time()`` according to
our rule, but since it is a global state in the simulated world and not related to any
particular event, it is more intuitive to make this one exception for it.

As part of our convention, the object methods will take a first argument that is a
pointer to the object itself. Again, there are a few exceptions: Some functions that are
called by the current process and act on itself do not have this argument. It is
enough to say ``cmb_process_hold(5)``, not ``cmb_process_hold(me, 5)``. Similarly,
calling ``cmb_process_exit(ptr)`` is enough, calling ``cmb_process_exit(me, ptr)``
would be slightly strange. We believe this exception makes the code more intuitive,
even if it is not entirely consistent.

One can (and wilL!) claim that this approach to object-oriented programming provides
most of the benefits while minimizing the overhead and constraints from a typical
object-oriented programming language. However, there are some features we cannot
provide directly:

* *Multiple inheritance*, where a class is derived from more than one parent class. Our
  parent classes need to go first in each child class struct, and then there can only
  be one parent. This is no big loss, since multiple inheritance quickly becomes very
  confusing. We instead distinguish between *is a* relationships (single inheritance)
  and *has a* relationships (composition). For example, our ``cmb_resource`` *is a*
  ``cmi_holdable`` (an abstract parent class), but it *has a* ``cmb_resourceguard`` to
  maintain an orderly priority queue of processes waiting for the resource, and the
  ``cmb_resourceguard`` itself *is a* ``cmi_hashheap``.

* *Automatic initialization and destruction* for objects that go in and out of scope.
  In C++, Resource Allocation Is Initialization (RAII). In C, it is not. (RAINI?) This
  requires us to distinguish clearly between allocating, initializing, terminating, and
  destroying an object. The allocate/destroy pair handles raw memory allocation. For
  objects declared as local variables or implicitly as parent class objects, these are
  not called. The initialize/terminate pair makes the allocated memory space ready for
  use as an actual object, and handles any necessary clean-up (such as deallocating
  internal arrays allocated during the object's lifecycle). In some cases, there is
  also a reset function, in effect a terminate followed by a new initialize, returning
  the object to a newly initialized state.

When defining your own classes derived from Cimba classes, such as the ``ship`` class in
our second tutorial, your code has the responsibility to follow this pattern. Your
allocator function (e.g., ``ship_create()``) allocates and zero-initializes raw memory,
while the constructor function (``ship_initialize()``) fills it with meaningful values.
The constructor does not get called by itself, so your code is also responsible for
calling it, both for objects allocated on the heap, objects declared as local
variables, and for objects that exist as a parent class to one of your objects. The
last case is done by calling the parent class constructor, here
``cmb_process_initialize()`` from within the child class constructor function.

Similarly, your code needs to provide a destructor to free any memory allocated by the
object (``ship_terminate()``, and a deallocator to free the object itself
(``ship_destroy()``). Your destructor function should also call the parent class
destructor (here ``cmb_process_terminate()``), but your de-allocator should NOT call
the parent class de-allocator, since that would be free'ing the same memory twice and
probably crash your application.

By looking around in the Cimba code, you will find many examples of how we have used
the object-orientation. For example, a ``cmb_resourceguard`` does not actually care or
know what type of resource it guards, only that it is something derived from the
``cmi_resourcebase`` abstract base class. Or, a process may be holding some resource, but
may not really care what kind, only that it is some kind of ``cmi_holdable``, itself
derived from ``cmi_resourcebase``. If the process needs to drop the resource in a
hurry, there is a polymorphic function (really just a pointer to the appropriate
function) for how to handle that for a particular kind of resource.

Events and the Event Queue
--------------------------

The fundamental property of a discrete event simulation model is that state only can
change at the event times. The basic algorithm is to maintain a priority queue of
scheduled events, retrieve the first one, set the simulation clock to its reactivation
time, execute the event, and repeat.

An event may schedule, cancel, or re-prioritize other events, and in general change
the state of the model in arbitrary and application-defined ways. This is why
parallelizing a single model run is near impossible: The scheduler cannot know what
event to execute next or what state the next event will encounter before the current event
is finished executing.

Cimba maintains a single, thread-local event queue and simulation clock. These are
global to the simulated world, but local to each trial thread. Two simulations running
in parallel on separate CPU cores exist in the same shared memory space, but do not
interact or influence each other.

We define an *event* as a triple consisting of a pointer to a function that takes two
pointers to ``void`` as arguments and does not return any value, and the two pointers
to ``void`` that will become its arguments. The function is an *action*, the two
arguments its *subject* and *object*. The event is then executing the one-liner
``(*action)(subject, object);``

Our process interactions are also events. For example, a process calling
``cmb_process_hold(5.0)`` actually schedules a wakeup event for itself at the current
simulation time + 5.0 before it yields to the dispatcher. At some point, that event has
bubbled up to the top of the priority queue and gets executed. Similarly, when a
``cmb_resourceguard`` wakes up a waiting process to inform it that "congratulations,
you now hae the resource", it schedules an event at the current time with the process'
priority that actually resumes the process. This avoids long and complicated call
stacks.

This also happens to be the reason why our events need to be (at least) a triple: The
event to reactivate some process needs to contain the reactivation function, a pointer to
the process, and a pointer to the context argument for its ``resume()`` call.

Note that the event is not an object. It is ephemeral; once it has occurred, it is gone.
You cannot take a pointer to an event. You can schedule an event as a triple ``(action,
subject, object)`` for a certain point in time with a certain priority, and as we soon
will see, you can cancel a scheduled event, reschedule it, or change its priority, but it
is still not an object. In computer sciency terms, it is a *closure*, a function with a
context to be executed at a future time and place.

The Hash-Heap - A Binary Heap meets a Hash Map
----------------------------------------------

Since the basic discrete event simulation algorithm is all about inserting and
retrieving events in a priority queue, the efficiency of this data structure and the
algorithms acting on it becomes e key determinant of the overall application efficiency.

Cimba uses a hash-heap data structure for this. It consists of two interconnected parts:

* The *heap* is a binary tree represented as a partially sorted array. The next event
  to be executed is always in array position 1. Retrieving it will trigger a reshuffle
  of the heap, making insertion and retrieval O(log n) operations. A scheduled event is
  just a value in this array, which may be moved to another location in the array at any
  time. However, cancelling a future event is a O(n) operation, since the array needs
  to be searched from the beginning to find the event.

* The *hash* complements the heap with an open addressing hash map. When an
  event is first scheduled, it is assigned a unique identifier, a *handle*. The hash
  map keeps track of where in the heap that event is at any time. Accessing the event
  using its handle is then an O(1) operation, while canceling it and reshuffling the
  heap is O(log n). For large models with many scheduled events, this may be a useful
  speed improvement. We use Fibonacci hashing and open addressing for simplicity and
  efficiency, see https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/

Once we have this module tightly packaged, it can be used elsewhere than just the main
event queue. We use the same data structure for all priority queues of processes
waiting for some resource, since our ``cmb_resourceguard`` is a derived class from
``cmi_hashheap``. In our second tutorial, the LNG harbor simulation, we even used it at
the modeling level to maintain the set of active ships in the model.

Each entry in the hashheap array provides space for four 64-bit payload items, together
with the handle, a ``double``, and a signed 64-bit integer for use as prioritization
keys. The ``cmi_hashheap`` struct also has a pointer to an application-provided comparator
function that determines the ordering between two entries. For the main event priority
queue, this is based on reactivation time, priority, and handle value (i.e. FIFO
ordering as tie-breaker), while the waiting list for a resource will use the process
priority and the handle value as ordering keys. If no comparator function is provided,
the hashheap will use a default comparator that only uses the ``double`` key and
retrieves the smallest value first.

The hashheap also provides wildcard functions to search for, count, or cancel scheduled
events that match some combination of (action, subject, object). For this purpose,
special values ``CMB_ANY_ACTION``, ``CMB_ANY_SUBJECT``, and ``CMB_ANY_OBJECT`` are
defined. As an example, suppose we are building a large-scale simulation model of an
air war. When some plane in the simulation gets shot down, all its scheduled future
events should be cancelled. In Cimba, this can be done by a simple call like
``cmb_event_pattern_cancel(CMB_ANY_ACTION, my_airplane, CMB_ANY_OBJECT);``

Guarded Resources and Conditions
--------------------------------

Many simulations involve active processes competing for some scarce resource. Cimba
provides four resource classes and one very general condition variable class. Two of
the resource classes are holdable with acquire/release semantics, where ``cmb_resource``
is a simple binary semaphore that only one process can hold at a time, while the
``cmb_resourcestore`` is a counting semaphore where several processes can hold some
amount of the resource. The other two resource types have put/get semantics, where the
``cmb_buffer`` only considers the number of units that goes in and out, while the
``cmb_objectqueue`` allows individual pointers to objects.

The common theme for all these is that a process requests something and may have to
wait in an orderly priority queue for its turn if that something is not immediately
available. Our hashheap is a good starting point for building this. For this purpose, we
derive a class ``cmb_resourceguard`` from the ``cmi_hashheap``, adding a pointer to some
resource (the abstract base class) to be guarded, and a list of observing resource guards.

When a process requests some resource and finds it busy, it enqueues itself in the
hashheap priority queue. It also registers its *demand function*, a predicate function
that takes three arguments, a pointer to the guarded resource, a pointer to the process
itself, and a ``void`` pointer to some application-defined context. Using some
combination of this information, the demand function returns a boolean true or false
answer to whether the demand is satisfied. The demand function is pre-packaged for the
four resource types. For example, a process requesting a simple ``cmb_resource`` demands
that it is not already in use, while a process requesting to put some amount into a
``cmb_buffer`` demands that there is free space in the buffer. After registering itself,
the process yields control to the dispatcher. All of this happens inside the
``acquire()``, ``get()``, or ``put()`` call, invisible to the calling process.

When some other process is done with the resource and releases it, it will *signal* the
``cmb_resourceguard``. This signal causes the resource guard to evaluate the demand
function for its highest-priority waiting process. If satisfied, that process is
removed from the wait list, gets reactivated, and can grab the resource. The resource
guard then forwards the signal to any other resource guards that have registered
themselves as observers of this one, causing these to do the same on their wait lists.

The *condition variable* ``cmb_condition`` is essentially a named resource guard
with a user application defined demand function. The condition demand function can be
anything that is computable from the given arguments and other state of the model at
that point in simulated time. It can be used for arbitrarily complex "wait for any one of
many" or "wait for all of many" scenarios where the ``cmb_condition`` will register
itself as observer to the underlying resource guards, and, as shown in our second
tutorial, it can include continuous-valued state variables.

We believe that the open-ended flexibility of our demand predicate function,
pre-packaged for the common resource types and exposed for the ``cmb_condition``, makes
Cimba a very powerful and expressive simulation tool. There may also be a weak pun here
on the C++ ``promise``: Cimba processes do not promise. They *demand*.

Pseudo-Random Number Generators and Distributions
-------------------------------------------------

Data Collectors
---------------

Experiments and Multi-Threaded Trials
-------------------------------------
