t.. _background:

The Whys and Hows of Cimba
==========================

Project History and Goals
-------------------------

Cimba 3.0.0 is released on GitHub in late 2025, but as the version number indicates,
there is some history before this first public release.

The earliest ideas that eventually became Cimba date to work done at the Norwegian Defence
Research Establishment in the late 1980's. I built discrete event simulation models
in languages like Simscript and Simula67. Encountering Simula67's coroutines and
object-orientation was revelatory in its essential *rightness*. However, Simula67 was
still severely limited in many other respects and not really a practical option at that
time.

Around 1990, we started building discrete event simulation models in C++ as early adopters
of that language. The first C++ models ran on VAXstations, where spawning a coroutine is
a single assembly instruction. Trying to port that code to a Windows PC was a somewhat
painful experience (and an abject failure). I actually complained to Bjarne Stroustrup in
person about the inconsistent to non-existent support for Simula-like coroutines in C++ at
a conference in Helsingør, probably in 1991. He seemed to agree but I silently resolved
to build my next simulation model in pure Kernighan & Richie C. Which I did.

That opportunity arose at MIT around 1994, where I needed a discrete event simulation
model for cross-checking analytical models of manufacturing systems. For fairly obvious
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
CPU's. The goals for Cimba v 3.0 are similar to those for earlier versions:

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

I believe that Cimba 3.0 meets these goals and hope you will agree. In the remainder
of this section, we will describe some of the modules and design choices in more detail.

Object-Oriented Programming. In C and Assembly.
-----------------------------------------------

The Hash-Heap - A Binary Heap meets a Hash Map
----------------------------------------------

Events and the Event Queue
--------------------------

Coroutines and Processes
------------------------

Guarded Resources and Conditions
--------------------------------

Pseudo-Random Number Generators and Distributions
-------------------------------------------------

Data Collectors
---------------

Experiments and Multi-Threaded Trials
-------------------------------------
