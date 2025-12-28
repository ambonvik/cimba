.. _background:

The Whys and Hows of Cimba
==========================

In this section, we will explain a bit more about the background for Cimba and some of
the key design choices that are made in it. We start with a brief history that is
necessary background for the project goals.

<This section is still work in progress, please check back later to read the rest.>

Project History and Goals
-------------------------

Cimba 3.0.0 is released on GitHub as public beta in late 2025, but as the version number
indicates, there is some history before this first public release.

The earliest ideas that eventually became Cimba date to work done at the Norwegian Defence
Research Establishment in the late 1980's. I built discrete event simulation models
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
on a Linux PC, but could be better. In retrospect, we consider this Cimba version 1.0.

For my PhD thesis research, I needed to run *many* simulations with various parameter
combinations and replications. By then, I had realized that parallelizing a discrete
event simulation model is trivially simple if one looks at it with a telescope instead
of using a microscope. The individual replications are *meant* to be independent
identically distributed trials, implying that there is nearly no interaction between them
at runtime. One can just fork off as many replications in parallel as one has computing
resources for, and use one computing node to dole out the jobs and collect the
statistics.

This was lashed together at the process level using `rsh` and a Perl script to control
the individual simulations on a cluster of workstations, and just to make a point, on
at least one computer back at the Norwegian Defence Research Establishment for a
trans-Atlantic distributed simulation model. At the same time, the core discrete event
simulation was rewritten in ANSI C with a binary heap event queue. It needed to be very
efficient and have a very small memory footprint to run at low priority in the background
on occupied workstations without anyone noticing a performance drop. This was pretty good
for 1995, and can safely be considered Cimba version 2.0, but it was never released to
the public.

After that, not much happened to it, until I decided to dust it off and publish it as
open source many years later. The world had evolved quite a bit in the meantime, so the
code required another comprehensive re-write to exploit the computing power in modern
CPU's, this time coded in C17.

The goals for Cimba v 3.0 are similar to those for earlier versions:

* Speed and efficiency, where small size in memory translates to execution speed on
  modern CPUs with cached memory pipelines, and multithreading trials on CPU cores
  provides the parallelism.

* Portability, running both on Linux and Windows, initially limited to the AMD64 /
  x86-64 architecture.

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
were lucky enough (or just old enough) to actually have programmed in Simula67, the
object-orientation with classes and inheritance was only part of the experience, and
perhaps not the most important part.

The most powerful concept was the *coroutine*, in particular for simulation work. This
generalizes the concept of a subroutine to several parallel threads of execution that
are non-preemptively scheduled. Combined with object-orientation, this means that one
can describe a class of objects as independent threads of execution, often infinite
loops, where the object's code just does its own thing. The complexity in the simulated
world then arises from the interactions between the active processes and various other
passive objects, while the description of each entity's actions is very natural.

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
since we will combine these with multithreading at the next higher level of concurrency
The Cimba coroutines will exist in parallel universes within each thread.

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
does, the trampoline will catch it and call the coroutine ``exit()`` function with the
return value as argument, giving exactly the same effect of a ``return ptr;`` as a
``exit(ptr);``, because it becomes the same thing. The code is in the same assembly
files as above, function ``cmi_coroutine_trampoline``.

The *data structure* ``struct cmi_coroutine`` is defined in `src/cmi_coroutine.h
<https://github.com/ambonvik/cimba/blob/main/src/cmi_coroutine.h>`_`. It
contains pointers to the coroutine stack, its parent and latest caller coroutines, the
coroutine function, its context argument, and the coroutine's status and exit value.

Finally, the *setup code* initializes a new coroutine stack and makes it ready to start
execution. This is system-dependent C code, function ``cmi_coroutine_context_init`` in
`src/port/x86-64/linux/cmi_coroutine_context.c <https://github.com/ambonvik/cimba/blob/main/src/port/x86-64/linux/cmi_coroutine_context.c>`_
and
`src/port/x86-64/windows/cmi_coroutine_context.c <https://github
.com/ambonvik/cimba/blob/main/src/port/x86-64/windows/cmi_coroutine_context.c>`_.
Basically, it fakes the stack frame that a function would see when executing normally on
the new stack, with the trampoline at its return address, and then transfers control into
the new coroutine. This starts executing the trampoline function, which in turn starts the
actual coroutine function as described above.

This machinery is hidden from the user application, which only needs to provide a
coroutine function using the ``yield()``, ``resume()``, and ``transfer()`` primitives
to run this as either asymmetric or symmetric coroutines. The coroutine function
prototype is ``void *(cmi_coroutine_func)(struct cmi_coroutine *cp, void *context)``,
i.e., a function that takes a pointer to a coroutine (itself) and a ``void *`` to some
user application-defined context as arguments and returns a ``void *``.
For even more flexibility, we also allow the user application to define what ``exit()``
function to be called if/when the coroutine function returns. This may seem like an
intricate way of calling that exit function indirectly by returning from the coroutine
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

We will soon return to this, but if the reader has been paying attention, there is
something else we need to address first: We just said *"inheriting all properties and
methods from the coroutine class"*, but we also just said "C17" and "assembly".

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
  methods and may *overload* (change) the meaining og parent class methods.

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



Events and the Event Queue
--------------------------

The Hash-Heap - A Binary Heap meets a Hash Map
----------------------------------------------


Guarded Resources and Conditions
--------------------------------

Pseudo-Random Number Generators and Distributions
-------------------------------------------------

Data Collectors
---------------

Experiments and Multi-Threaded Trials
-------------------------------------
