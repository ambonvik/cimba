.. _tutorial:

Tutorial: Introducing Cimba
===========================

Getting started
---------------

You will need a a C compiler like gcc or clang, and a development toolchain of
git, meson, and ninja. For some of the examples in this tutorial, you will need
the free plotting program gnuplot, but this is not strictly necessary for using
Cimba.

We also recommend using a modern integrated development environment (IDE) like CLion,
which has the advantage of being available both on Linux and Windows, integrated
with a gcc toolchain (called MinGW under Windows), and free for open source
work. Microsoft Visual Studio with MVSC should also work, but we have not tested
it yet.

Once the build chain is installed, you need to obtain Cimba itself.
Cimba is distributed as fre open source code through the Github repository at
https://github.com/ambonvik/cimba. You download, build, and install Cimba with
a command sequence similar to the following. The details will depend on your
operating system and preferred build chain; the below is for Linux:

.. code-block:: bash

    git clone https://github.com/ambonvik/cimba
    cd cimba
    meson setup build
    meson compile -C build
    meson install -C build

You will probably need elevated privileges for the last step, since it installs
the library and header files in system locations like /usr/local/include.

After installation, you can write a C program like this (tutorial/hello.c):

.. code-block:: c

    #include <cimba.h>
    #include <stdio.h>

    int main(void) {
        printf("Hello world, I am Cimba %s.\n", cimba_version());
    }

You can compile and run it as any other C program, linking to the Cimba library:

.. code-block:: bash

    gcc hello.c -lcimba -o hello
    ./hello

If all goes well, this should produce output similar to::

    Hello world, I am Cimba 3.0.0 beta.

You now have a working Cimba installation.

Our first simulation - a M/M/1 queue
------------------------------------

In this section, we will walk through the development of a simple model from
the basic model entities and interactions to parallelizing it on all available
CPU cores and producing presentation-ready output.

Our first simulated system is a M/M/1 queue. In queuing theory (Kendall)
notation, this abbreviation indicates a queuing system where the arrival process is memoryless
with exponentially distributed intervals, the service process is the same, there is
only one server, and the queue has unlimited capacity. This is a mathematically
well understood system.

Our simulation will verify if the formula for expected
queue length is correct (or vice versa).

Arrival, service, and the queue
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

We model this in a straight-forward manner: We need an arrival process that puts
customers into the queue at random intervals, a service process that gets
customers from the queue and services them for a random duration, and the queue
itself. We are not concerned with the characteristics of each customer, just how
many there are in the queue, so we do not need a separate object for each customer.
We will use a cmb_buffer for this. In this first iteration, we will hard-code
parameter values for simplicity, such as 75 % utilization, and then do it properly
shortly.

Let us start with the arrival and service processes. The code can be very simple:

.. code-block:: c

     void *arrivals(struct cmb_process *me, void *ctx)
     {
         struct cmb_buffer *bp = ctx;
         while (true) {
             double t_ia = cmb_random_exponential(1.0 / 0.75);
             cmb_process_hold(t_ia);
             uint64_t n = 1;
             cmb_buffer_put(bp, &n);
         }
     }

.. code-block:: c

     void *service(struct cmb_process *me, void *ctx)
     {
         struct cmb_buffer *bp = ctx;
         while (true) {
             uint64_t m = 1;
             cmb_buffer_get(bp, &m);
             double t_srv = cmb_random_exponential(1.0);
             cmb_process_hold(t_srv);
         }
     }

This should hopefully be quite intuitive.
The arrivals process generates an exponentially distributed random value with
mean 1/0.75, holds for that amount of interarrival time, puts one new customer
into the queue, and does it again.

Similarly, the service process gets a customer from the queue (waiting for one to
arrive if there are none waiting), generates a random service time with mean 1.0,
holds for the service time, and does it all over again. An average arrival rate
of 0.75 and service rate of 1.0 gives the 0.75 utilization we wanted.

Note that the number of customers to put or get is given as a pointer to a variable
containing the number, not just a value. In more complex scenarios than this, the
process may encounter a partially completed put or get, and we need a way to capture
the actual state in these cases. For now, just note that the amount argument to
`cmb_buffer_put()` and `cmb_buffer_get()` is a pointer to an unsigned 64-bit integer
variable.

The process function signature is a function returning a pointer to void (i.e. a
raw address to anything). It takes two arguments, the first one a pointer to a
cmb_process (itself), the second a pointer to void that gives whatever context
the process needs to execute. For now, we only use the context pointer as a
pointer to the queue, and do not use the `me` pointer or the return value.

Note also that all Cimba functions used here start with `cmb_`, indicating that they
belong in the namespace of things in the simulated world. There are functions
from three Cimba modules here, `cmb_process`, `cmb_buffer`, and `cmb_random`. We will
encounter other namespaces and modules soon.

We also need a main function to set it all up, run the simulation, and clean up
afterwards. Let us try this:

.. code-block:: c

    int main(void)
    {
        const uint64_t seed = cmb_random_get_hwseed();
        cmb_random_initialize(seed);

        cmb_event_queue_initialize(0.0);

        struct cmb_buffer *que = cmb_buffer_create();
        cmb_buffer_initialize(que, "Queue", CMB_BUFFER_UNLIMITED);

        struct cmb_process *arr_proc = cmb_process_create();
        cmb_process_initialize(arr_proc, "Arrivals", arrivals, que, 0);
        cmb_process_start(arr_proc);

        struct cmb_process *serv_proc = cmb_process_create();
        cmb_process_initialize(serv_proc, "Service", service, que, 0);
        cmb_process_start(serv_proc);

        cmb_event_queue_execute();

        cmb_process_terminate(serv_proc);
        cmb_process_destroy(serv_proc);

        cmb_process_terminate(arr_proc);
        cmb_process_destroy(arr_proc);

        cmb_buffer_terminate(que);
        cmb_buffer_destroy(que);

        cmb_event_queue_terminate();
        cmb_random_terminate();

        return 0;
    }

The first thing it does is to get a suitable random number seed from a hardware
entropy source and initialize our pseudo-random number generators with it.

It then initializes the simulation event queue, specifying at our clock will
start from 0.0. (It could be any other value, but why not 0.0.)

Next, it creates and initializes the cmb_buffer, naming it "Queue" and setting
it to unlimited size.

After that, it creates, initializes, and starts the arrival and service processes.
They get a pointer to the newly created `cmb_buffer` as their context argument, and
the event queue is ready, so they can just start immediately.

Notice the pattern here: Objects are first *created*, then *initialized* in a
separate call. The create method allocates heap memory for the object, the initialize
method makes it ready for use. Some objects, such as the event queue, already
exist in pre-allocated memory and cannot be created. There are no
`cmb_event_queue_create()` or `cmb_event_queue_destroy()` functions.

In later examples, we will also
see cases where some Cimba object is simply declared as a local variable and
allocated memory on the stack. We still need to initialize it, since we are not
in C++ with its "Resource Allocation Is Initialization" (RAII) paradigm. In C,
resource allocation is *not* initialization (RAINI?), and we need to be very
clear on each object's memory allocation and initialization status. We have tried
to be as consistent as possible in Cimba in the create/initialize/terminate/destroy
object lifecycle.

Having made it this far, `main()` calls `cmb_event_queue_execute()` to run the
simulation before cleaning up.

Note that there are matching `_terminate()` calls for each `_initialize()` and
matching `_destroy()` for each `_create()`. These functions un-initialize and
and de-allocate the objects that were created and initialized.

We can now run our first simulation and see what happens. It will generate
output like this::

    [ambonvik@Threadripper cimba]$ build/tutorial/tut_1_1 | more
        0.0000	dispatcher	cmb_process_start (121):  Start Arrivals 0x56016396e490
        0.0000	dispatcher	cmb_process_start (121):  Start Service 0x56016396ee10
        0.0000	dispatcher	cmb_event_queue_execute (252):  Starting simulation run
        0.0000	Arrivals	cmb_process_hold (283):  Hold until time 0.361960
        0.0000	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 0
        0.0000	Service	cmb_buffer_get (217):  Gets 1 from Queue
        0.0000	Service	cmb_buffer_get (254):  Waiting for content
        0.0000	Service	cmb_buffer_get (256):  Queue capacity unlimited, level now 0
        0.0000	Service	cmb_resourceguard_wait (128):  Waits in line for Queue
       0.36196	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
       0.36196	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
       0.36196	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
       0.36196	Arrivals	cmb_process_hold (283):  Hold until time 0.806194
       0.36196	Service	cmb_buffer_get (270):  Returned successfully from wait
       0.36196	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
       0.36196	Service	cmb_buffer_get (217):  Gets 1 from Queue
       0.36196	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
       0.36196	Service	cmb_process_hold (283):  Hold until time 0.767340
       0.76734	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 0
       0.76734	Service	cmb_buffer_get (217):  Gets 1 from Queue
       0.76734	Service	cmb_buffer_get (254):  Waiting for content
       0.76734	Service	cmb_buffer_get (256):  Queue capacity unlimited, level now 0
       0.76734	Service	cmb_resourceguard_wait (128):  Waits in line for Queue
       0.80619	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
       0.80619	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
       0.80619	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
       0.80619	Arrivals	cmb_process_hold (283):  Hold until time 3.353821
       0.80619	Service	cmb_buffer_get (270):  Returned successfully from wait
       0.80619	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
       0.80619	Service	cmb_buffer_get (217):  Gets 1 from Queue
       0.80619	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
       0.80619	Service	cmb_process_hold (283):  Hold until time 0.919833
       0.91983	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 0
       0.91983	Service	cmb_buffer_get (217):  Gets 1 from Queue
       0.91983	Service	cmb_buffer_get (254):  Waiting for content
       0.91983	Service	cmb_buffer_get (256):  Queue capacity unlimited, level now 0
       0.91983	Service	cmb_resourceguard_wait (128):  Waits in line for Queue
        3.3538	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
        3.3538	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
        3.3538	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
        3.3538	Arrivals	cmb_process_hold (283):  Hold until time 3.387948
        3.3538	Service	cmb_buffer_get (270):  Returned successfully from wait
        3.3538	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
        3.3538	Service	cmb_buffer_get (217):  Gets 1 from Queue
        3.3538	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
        3.3538	Service	cmb_process_hold (283):  Hold until time 5.620490
        3.3879	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
        3.3879	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
        3.3879	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
        3.3879	Arrivals	cmb_process_hold (283):  Hold until time 3.458393
        3.4584	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 1
        3.4584	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
        3.4584	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
        3.4584	Arrivals	cmb_process_hold (283):  Hold until time 4.127857
        4.1279	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 2
        4.1279	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
        4.1279	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
        4.1279	Arrivals	cmb_process_hold (283):  Hold until time 6.525126
        5.6205	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 3
        5.6205	Service	cmb_buffer_get (217):  Gets 1 from Queue
        5.6205	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
        5.6205	Service	cmb_process_hold (283):  Hold until time 6.760735

...and will keep on doing that forever. We have to press Ctrl-C or similar
to stop it. The good news is that it works. Each line in the trace contains the
time stamp in simulated time, the name of the currently executing process, exactly
what function and line number is logging, and a formatted message about what is
happening. Our simulated processes seem to be doing what we asked them to do,
but there is a wee bit too much information here.

Stopping a simulation
^^^^^^^^^^^^^^^^^^^^^

We will address stopping first. The processes are *coroutines*, executing
concurrently on a separate stack for each process. Only one process can execute
at a time. It continues executing until it voluntarily *yields* the CPU to some
other coroutine. Calling `cmb_process_hold()` will do exactly that, transferring
control to the hidden dispatcher process that determines what to do next.

However, the
dispatcher only knows about events, not coroutines or processes. To ensure that
the process returns to the other end of its `cmb_process_hold()` call, it will
schedule a wakeup event at the expected time before it yields control to the
dispatcher. When executed, this event will *resume* the coroutine where it left
off, returning through the `cmb_process_hold()` call with a return value that
indicates normal or abormal return. (We have ignored the return values for now
in the example above.) So, whenever there are more than one process running,
there will be future events scheduled in the event queue.

To stop the simulation, we simply schedule an "end simulation" event, which
stops any running processes at that point.

A Cimba *event* is something that
happens at a single point in simulated time. It is expessed as a function with
two pointer arguments and no return value. The idea is for the event function
to be an *action* and its two arguments the *subject* and *object* for that
action, making its own little verb-subject-object grammar (like in Gaelic).

The event does not have a fixed location in memory. You cannot get a
pointer to an event, only a *handle*, a reference number that makes it possible
to cancel or reschedule a future event. Once executed, the event no longer
exists. If some lasting effect is needed, the event function needs to do that
through the content of its subject and object pointer arguments.

This is perhaps easier to do in code than to describe in prose. We define a
`struct simulation` that contains the entities of our simulated world and an
end simulation event:

.. code-block:: c

    struct simulation {
        struct cmb_process *arr;
        struct cmb_buffer *que;
        struct cmb_process *srv;
    };

    void end_sim(void *subject, void *object)
    {
        struct simulation *sim = object;
        cmb_process_stop(sim->arr, NULL);
        cmb_process_stop(sim->srv, NULL);
    }

We then store pointers to the simulation entities in the `struct simulation`
and schedule an `end_sim` event before executing the event queue:

.. code-block:: c

    int main(void)
    {
        const uint64_t seed = cmb_random_get_hwseed();
        cmb_random_initialize(seed);

        cmb_event_queue_initialize(0.0);

        struct simulation sim = {};
        sim.que = cmb_buffer_create();
        cmb_buffer_initialize(sim.que, "Queue", CMB_BUFFER_UNLIMITED);

        sim.arr = cmb_process_create();
        cmb_process_initialize(sim.arr, "Arrivals", arrivals, sim.que, 0);
        cmb_process_start(sim.arr);

        sim.srv = cmb_process_create();
        cmb_process_initialize(sim.srv, "Service", service, sim.que, 0);
        cmb_process_start(sim.srv);

        cmb_event_schedule(end_sim, NULL, &sim, 10.0, 0);
        cmb_event_queue_execute();

        cmb_process_terminate(sim.srv);
        cmb_process_destroy(sim.srv);

        cmb_process_terminate(sim.arr);
        cmb_process_destroy(sim.arr);

        cmb_buffer_terminate(sim.que);
        cmb_buffer_destroy(sim.que);

        cmb_event_queue_terminate();
        cmb_random_terminate();

        return 0;
    }

The arguments to `cmb_event_schedule` are the event function, its subject and
object pointers, the simulated time when this event will happen, and an event
priority. We have set end time 10.0 here. It could also be expressed as
`cmb_time() + 10.0` for "in 100 time units from now".

In Cimba, it does not even have to be at a predetermined time. It would be
equally valid for some process in the simulation to schedule an end simulation
event at the current time whenever some condition is met, such as a certain
number of customers having been serviced, a statistics collector having a
certain number of samples, or something else.

We gave the end simulation event a default priority of 0 as the last argument to
`cmb_event_schedule()`. Priorities are signed 64-bit integers, `int64_t`. The
Cimba dispatcher will always execute the next scheduled event with the lowest
scheduled time. The simulation clock then jumps to that time. If several events
have the *same* scheduled time, the dispatcher will execute the one with the
highest priority first. If several events have the same scheduled time *and*
the same priority, it will execute them in first in first out order.

Again, we roundly ignored the event handle returned by `cmb_event_schedule()`,
since we will not be using it in this example. If we wanted to keep it, it is an
unsigned 64-bit integer (`uint64_t`).

When initializing our arrivals and service processes, we quietly set the last
argument to `cmb_process_initialize()` to 0. This was the inherent process
priority for scheduling any events pertaining to this process, its
priority when waiting for some resource, and so on. The processes can adjust
their own (or each other's) priorities during the simulation, dynamically
moving themselves up or down in various queues. Cimba does not attempt to
adjust any priorities by it self, it just acts on whatever the priorities are,
and reshuffles any existing queues as needed if priorities are changed.

We compile and run, and get something like this::

    [ambonvik@Threadripper cimba]$ build/tutorial/tut_1_2
        0.0000	dispatcher	cmb_process_start (121):  Start Arrivals 0x557d933e7490
        0.0000	dispatcher	cmb_process_start (121):  Start Service 0x557d933e7e10
        0.0000	dispatcher	cmb_event_queue_execute (252):  Starting simulation run
        0.0000	Arrivals	cmb_process_hold (283):  Hold until time 0.073871
        0.0000	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 0
        0.0000	Service	cmb_buffer_get (217):  Gets 1 from Queue
        0.0000	Service	cmb_buffer_get (254):  Waiting for content
        0.0000	Service	cmb_buffer_get (256):  Queue capacity unlimited, level now 0
        0.0000	Service	cmb_resourceguard_wait (128):  Waits in line for Queue
      0.073871	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
      0.073871	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
      0.073871	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
      0.073871	Arrivals	cmb_process_hold (283):  Hold until time 0.532651
      0.073871	Service	cmb_buffer_get (270):  Returned successfully from wait
      0.073871	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
      0.073871	Service	cmb_buffer_get (217):  Gets 1 from Queue
      0.073871	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
      0.073871	Service	cmb_process_hold (283):  Hold until time 1.433233
       0.53265	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
       0.53265	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
       0.53265	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
       0.53265	Arrivals	cmb_process_hold (283):  Hold until time 0.582416
       0.58242	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 1
       0.58242	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
       0.58242	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
       0.58242	Arrivals	cmb_process_hold (283):  Hold until time 2.210148
        1.4332	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 2
        1.4332	Service	cmb_buffer_get (217):  Gets 1 from Queue
        1.4332	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
        1.4332	Service	cmb_process_hold (283):  Hold until time 1.616264
        1.6163	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
        1.6163	Service	cmb_buffer_get (217):  Gets 1 from Queue
        1.6163	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
        1.6163	Service	cmb_process_hold (283):  Hold until time 2.385136
        2.2101	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
        2.2101	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
        2.2101	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
        2.2101	Arrivals	cmb_process_hold (283):  Hold until time 2.412749
        2.3851	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
        2.3851	Service	cmb_buffer_get (217):  Gets 1 from Queue
        2.3851	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
        2.3851	Service	cmb_process_hold (283):  Hold until time 2.986251
        2.4127	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
        2.4127	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
        2.4127	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
        2.4127	Arrivals	cmb_process_hold (283):  Hold until time 3.161421
        2.9863	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
        2.9863	Service	cmb_buffer_get (217):  Gets 1 from Queue
        2.9863	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
        2.9863	Service	cmb_process_hold (283):  Hold until time 3.371444
        3.1614	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
        3.1614	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
        3.1614	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
        3.1614	Arrivals	cmb_process_hold (283):  Hold until time 8.927410
        3.3714	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
        3.3714	Service	cmb_buffer_get (217):  Gets 1 from Queue
        3.3714	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
        3.3714	Service	cmb_process_hold (283):  Hold until time 4.494141
        4.4941	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 0
        4.4941	Service	cmb_buffer_get (217):  Gets 1 from Queue
        4.4941	Service	cmb_buffer_get (254):  Waiting for content
        4.4941	Service	cmb_buffer_get (256):  Queue capacity unlimited, level now 0
        4.4941	Service	cmb_resourceguard_wait (128):  Waits in line for Queue
        8.9274	Arrivals	cmb_buffer_put (311):  Queue capacity unlimited, level now 0
        8.9274	Arrivals	cmb_buffer_put (320):  Puts 1 into Queue
        8.9274	Arrivals	cmb_buffer_put (326):  Success, found room for 1, has 0 remaining
        8.9274	Arrivals	cmb_process_hold (283):  Hold until time 15.199148
        8.9274	Service	cmb_buffer_get (270):  Returned successfully from wait
        8.9274	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 1
        8.9274	Service	cmb_buffer_get (217):  Gets 1 from Queue
        8.9274	Service	cmb_buffer_get (223):  Success, 1 was available, got 1
        8.9274	Service	cmb_process_hold (283):  Hold until time 9.166237
        9.1662	Service	cmb_buffer_get (208):  Queue capacity unlimited, level now 0
        9.1662	Service	cmb_buffer_get (217):  Gets 1 from Queue
        9.1662	Service	cmb_buffer_get (254):  Waiting for content
        9.1662	Service	cmb_buffer_get (256):  Queue capacity unlimited, level now 0
        9.1662	Service	cmb_resourceguard_wait (128):  Waits in line for Queue
        10.000	dispatcher	cmb_process_stop (588):  Stop Arrivals value (nil)
        10.000	dispatcher	cmb_process_stop (588):  Stop Service value (nil)
        10.000	dispatcher	cmb_event_queue_execute (255):  No more events in queue

    Process finished with exit code 0

Progress: It started, ran, and stopped.

Setting logging levels
^^^^^^^^^^^^^^^^^^^^^^

Next, the verbiage. As you would expect at this point in the tutorial, Cimba has
powerful and flexible logging that gives you fine-grained control of what to log.

The core logging function is called `cmb_vfprintf()`. As the name says, it is
similar to the standard function `vfprintf()` but with some Cimba-specific added
features. You will rarely interact directly with this function, but instead call
wrapper functions (actually macros) like `cmb_logger_user()` or `cmb_logger_error()`.

The key concept to understand here is the *logger flags*. Cimba uses a 32-bit
unsigned integer (`int32_t`) as a bitmask to determine what log entries to print and which
to ignore. Cimba reserves the top four bits for its own use, identifying messages
of various severities, leaving the 28 remaining bits for the user application.

There is a central bit pattern, and a bit mask in each call. If a simple bitwise
and (`&`) between the central bit pattern and the caller's bit mask gives a non-
zero result, that line is printed, otherwise not. Initally, all bits in the central
bit pattern are on, `0xFFFFFFFF`. You can turn selected bits on and off with
`cmb_logger_flags_on()` and `cmb_logger_flags_off()`.

Again, it may be easier to show this in code than to explain. We add a user-defined logging
message to the end event and the two processes. The messages take `printf`-style
format strings and arguments:

.. code-block:: c

    #include <cimba.h>
    #include <stdio.h>

    #define USERFLAG1 0x00000001

    struct simulation {
        struct cmb_process *arr;
        struct cmb_buffer *que;
        struct cmb_process *srv;
    };

    void end_sim(void *subject, void *object)
    {
        struct simulation *sim = object;
        cmb_logger_user(stdout, USERFLAG1, "--- Game Over ---");
        cmb_process_stop(sim->arr, NULL);
        cmb_process_stop(sim->srv, NULL);
    }

    void *arrivals(struct cmb_process *me, void *ctx)
    {
        struct cmb_buffer *bp = ctx;
        while (true) {
            double t_ia = cmb_random_exponential(1.0 / 0.75);
            cmb_logger_user(stdout, USERFLAG1, "Holds for %f time units", t_ia);
            cmb_process_hold(t_ia);
            uint64_t n = 1;
            cmb_logger_user(stdout, USERFLAG1, "Puts one into the queue");
            cmb_buffer_put(bp, &n);
        }
    }

    void *service(struct cmb_process *me, void *ctx)
    {
        struct cmb_buffer *bp = ctx;
        while (true) {
            uint64_t m = 1;
            cmb_logger_user(stdout, USERFLAG1, "Gets one from the queue");
            cmb_buffer_get(bp, &m);
            double t_srv = cmb_random_exponential(1.0);
            cmb_logger_user(stdout, USERFLAG1, "Got one, services it for %f time units", t_srv);
            cmb_process_hold(t_srv);
        }
    }

We also suppress the Cimba informationals from the main function:

.. code-block:: c

        cmb_logger_flags_off(CMB_LOGGER_INFO);

We compile and run, and get something like this::

    /home/ambonvik/github/cimba/build/tutorial/tut_1_3
        0.0000	Arrivals	arrivals (25):  Holds for 3.350108 time units
        0.0000	Service	service (38):  Gets one from the queue
        3.3501	Arrivals	arrivals (28):  Puts one into the queue
        3.3501	Arrivals	arrivals (25):  Holds for 5.545685 time units
        3.3501	Service	service (41):  Got one, services it for 1.647196 time units
        4.9973	Service	service (38):  Gets one from the queue
        8.8958	Arrivals	arrivals (28):  Puts one into the queue
        8.8958	Arrivals	arrivals (25):  Holds for 2.147751 time units
        8.8958	Service	service (41):  Got one, services it for 0.392234 time units
        9.2880	Service	service (38):  Gets one from the queue
        10.000	dispatcher	end_sim (15):  --- Game Over ---

Only our user-defined logging messages are printed. Note how the simulation time,
the name of the active process, the calling function, and the line number are
automagically prepended to the user-defined message.

We turn off our user-defined messages like this:

.. code-block:: c

    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USERFLAG1);

As you would expect, this version of the program produces no output.

Collecting and reporting statistics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Which brings us to getting some *useful* output. By now, we are suitably convinced
that our simulated M/M/1 queuing system is behaving as we expect, so we want it to
start reporting some statistics on the queue lenght.

It will be no surprise that Cimba contains flexible and powerful statistics
collectors and reporting functions. There is actually one built into the
`cmb_buffer` we have been using. We just need to turn it on:

.. code-block::

    struct simulation sim = {};
    sim.que = cmb_buffer_create();
    cmb_buffer_initialize(sim.que, "Queue", CMB_BUFFER_UNLIMITED);
    cmb_buffer_start_recording(sim.que);

After the simulation is finished, we can make it report its history like this:

.. code-block::

    cmb_buffer_stop_recording(sim.que);
    cmb_buffer_print_report(sim.que, stdout);

We increase the running time from ten to one million time units, compile, and run.
Very shortly thereafter, output appears::

    /home/ambonvik/github/cimba/build/tutorial/tut_1_4
    Buffer levels for Queue
    N  1314513  Mean    2.249  StdDev    2.867  Variance    8.221  Skewness    2.743  Kurtosis    11.94
    --------------------------------------------------------------------------------
    ( -Infinity,      0.000)   |
    [     0.000,      2.050)   |##################################################
    [     2.050,      4.100)   |##########-
    [     4.100,      6.150)   |#####=
    [     6.150,      8.200)   |###-
    [     8.200,      10.25)   |#=
    [     10.25,      12.30)   |=
    [     12.30,      14.35)   |=
    [     14.35,      16.40)   |-
    [     16.40,      18.45)   |-
    [     18.45,      20.50)   |-
    [     20.50,      22.55)   |-
    [     22.55,      24.60)   |-
    [     24.60,      26.65)   |-
    [     26.65,      28.70)   |-
    [     28.70,      30.75)   |-
    [     30.75,      32.80)   |-
    [     32.80,      34.85)   |-
    [     34.85,      36.90)   |-
    [     36.90,      38.95)   |-
    [     38.95,      41.00)   |-
    [     41.00,  Infinity )   |-
    --------------------------------------------------------------------------------

The text-mode histogram uses the character `#` to indicate a full pixel, `=` for
one that is more than half full, and `-` for one that contains something but less
than half filled.

We can also get a pointer to the `cmb_timeseries` object by
calling `cmb_buffer_get_history()` and doing further analysis on that. As an
example, let's do the first 20 partial autocorrelation coefficients of the queue
length time series and print a correlogram of that as well:

.. code-block:: c

    struct cmb_timeseries *ts = cmb_buffer_get_history(sim.que);
    double pacf_arr[21];
    cmb_timeseries_PACF(ts, 20, pacf_arr, NULL);
    cmb_timeseries_print_correlogram(ts, stdout, 20, pacf_arr);

Output::

               -1.0                              0.0                              1.0
    --------------------------------------------------------------------------------
       1   0.952                                  |###############################-
       2   0.348                                  |###########-
       3  -0.172                            =#####|
       4   0.138                                  |####=
       5  -0.089                               =##|
       6   0.087                                  |##=
       7  -0.058                                =#|
       8   0.064                                  |##-
       9  -0.046                                =#|
      10   0.051                                  |#=
      11  -0.037                                -#|
      12   0.042                                  |#-
      13  -0.032                                -#|
      14   0.036                                  |#-
      15  -0.026                                 =|
      16   0.033                                  |#-
      17  -0.023                                 =|
      18   0.029                                  |=
      19  -0.022                                 =|
      20   0.025                                  |=
    --------------------------------------------------------------------------------

This is not quite publication-ready graphics, but can be useful at the model
development stage we are at now: We have numbers. Theory predicts an average
queue length of 2.25 for a M/M/1 queue at 75 % utilization. We just got 2.249.

Close, but is it close enough? We need more resolving power.

Refactoring for parallelism
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Before we go there, we will clean up a few rough edges. The compiler keeps warning
us about unused function arguments. That is not unusual with Cimba, since we
often do not need all arguments to event and process functions. We will inform
the compiler (and human readers of the code) that this is intentional.

.. code-block:: c

    void end_sim(void *subject, void *object)
    {
        cmb_unused(subject);

        struct simulation *sim = object;
        cmb_logger_user(stdout, USERFLAG1, "--- Game Over ---");
        cmb_process_stop(sim->arr, NULL);
        cmb_process_stop(sim->srv, NULL);
    }

Warnings gone. Next, we want to tidy up the hard-coded parameters to a proper
data structure. We define a `struct trial` to contain parameters and output
values, and bundle both our existing `struct simulation` and `struct trial` in
a `struct context`, and pass that between the functions.

.. code-block:: c

    struct simulation {
        struct cmb_process *arr;
        struct cmb_buffer *que;
        struct cmb_process *srv;
    };

    struct trial {
        /* Parameters */
        double arr_rate;
        double srv_rate;
        double warmup_time;
        double duration;
        /* Results */
        double avg_queue_length;
    };

    struct context {
        struct simulation *sim;
        struct trial *trl;
    };

For now, we just declare these structs as local variables on the stack.

We define a small helper function to load the parameters into the `trial`:

.. code-block:: c

    void load_params(struct trial *trl)
    {
        cmb_assert_release(trl != NULL);

        trl->arr_rate = 0.75;
        trl->srv_rate = 1.0;
        trl->warmup_time = 1000.0;
        trl->duration = 1e6;
    }

.. admonition:: Asserts and debuggers

    Notice the `cmb_assert_release` in this code
    fragment. It is a custom version of the standard `assert()` macro, triggering
    a hard stop if the condition evaluates to `false`. Our custom asserts come in
    two flavors, `cmb_assert_debug()` and `cmb_assert_release()`. The `_debug` assert
    behaves like the standard one and goes away if the preprosessor symbol `NDEBUG`
    is `#defined`. The `_release` assert is still there, but also goes away if `NASSERT`
    is `#defined`.

    The usage pattern is typically that `cmb_assert_debug()` is used to test
    time-consuming invariants and post-conditions in the code, while
    `cmb_assert_release()` is used for simple input argument validity checks
    and similar pre-conditions that should be left in the production version
    of your model. Internally, you will find this pattern nearly everywhere in
    Cimba, as an example of programming by contract to ensure reliability and
    clarity. See also https://en.wikipedia.org/wiki/Design_by_contract

    We will trip one and see how it looks. We temporarily replace the
    exponentially distributed service time with a normally distributed, mean still
    1.0 and sigma 0.25. This will almost surely generate a negative value
    sooner or later, which will cause the service process to try to hold for a
    negative time, resuming in its own past. That should not be possible:

    .. code-block:: c

                // const double t_srv = cmb_random_exponential(t_srv_mean);
                const double t_srv = cmb_random_normal(1.0, 0.25);

    Sure enough::

        /home/ambonvik/github/cimba/build/tutorial/tut_1_5
        0x9bec8a16f0aa802a	    9359.5	Service	cmb_process_hold (272):  Fatal: Assert "dur >= 0.0" failed, source file cmb_process.c

        Process finished with exit code 134 (interrupted by signal 6:SIGABRT)

    The output line starts with the random seed that was used, to make it possible
    to reproduce intermittent issues. It then lists the simulated time, the process,
    the function and the exact line of code, the condition that failed, and the source
    code file where it blew up. It looks a lot like the logger output, because it is
    the logger output, called through a function called `cmi_assert_failed()`.

    If using a debugger, place a breakpoint  in `cmi_assert_failed()`.
    If some assert trips, control will always
    go there. You can then page up the stack and see exactly what happened.

    .. image:: ../images/debugger_assert.png


We also need a pair of events to turn data recording on and off at specified times:

.. code-block:: c

    static void start_rec(void *subject, void *object)
    {
        cmb_unused(subject);

        const struct context *ctx = object;
        const struct simulation *sim = ctx->sim;
        cmb_buffer_start_recording(sim->que);
    }

    static void stop_rec(void *subject, void *object)
    {
        cmb_unused(subject);

        const struct context *ctx = object;
        const struct simulation *sim = ctx->sim;
        cmb_buffer_stop_recording(sim->que);
    }

As the last refactoring step before we parallelize, we move the simulation driver
code from `main()` to a separate function, say `run_MM1_trial()`, and call it from
`main()`. For reasons that soon will be evident, its argument is a single pointer
to void, even if we immediately cast this to our `struct trial *` once inside the
function. We remove the call to `cmb_buffer_report()`, calculate the average
queue length, and store it in the `trial` results field:

.. code-block:: c

        struct cmb_wtdsummary wtdsum;
        const struct cmb_timeseries *ts = cmb_buffer_get_history(ctx.sim->que);
        cmb_timeseries_summarize(ts, &wtdsum);
        ctx.trl->avg_queue_length = cmb_wtdsummary_mean(&wtdsum);

The `main()` function is now reduced to this:

.. code-block:: c

    int main(void)
    {
        struct trial trl = {};
        load_params(&trl);

        run_MM1_trial(&trl);

        printf("Avg %f\n", trl.avg_queue_length);

        return 0;
    }

We will not repeat the rest of the code here. You can find it in tutorial/tut_1_5.c
Instead, we compile and run it, receiving output similar to this::

    /home/ambonvik/github/cimba/build/tutorial/tut_1_5
    Avg 2.234060

Parallelization
^^^^^^^^^^^^^^^

So far, we have been developing the model in a single thread, running on a single
CPU core. All the concurrency between our simulated processes (coroutines) is
happening inside this single thread. The simulation is still pretty fast, but a
modern CPU has many cores, most of them idly watching our work so far with
detached interest. Let's put them to work.

Cimba is built from the ground up for coarse-grained parallelism. Depending on
the viewpoint, parallelizing a discrete event simulation is either terribly
hard or trivially simple. The hard way to do it is to try to parallelize a
single simulation run. This is near impossible, since the outcome of each event
may influence all future events in complex and model-dependent ways.

The easy way is to realize that we never do a *single* simulation run. We want to
run *many* to generate statistically significant answers to questions and/or to test
many parameter combinations, perhaps in a full factorial experimental design.
Even if we could answer a question by a single
very long run, we may get a better understanding by splitting it into many shorter runs
to not just get an average, but also a sense of the variability of our results.

When we do multiple replications of a simulation, these are by design intended to be
independent, identically distributed trials. Multiple parameter combinations are
no less independent. This is what is called an "embarassingly parallel" problem.
There is no interaction between the trials, and they can be trivially parallelized
by just running them at the same time and collecting the output.

Cimba creates a pool of worker threads, one per (logical) CPU core on the system.
You describe your experiment as an array of trials and the function to execute each
trial, and pass these to `cimba_run_experiment()`.
The worker threads will pull trials from the experiment array and run them,
storing the results back in your trial struct, before moving to the next un-executed
trial in the array. This gives an inherent load balancing with minimal overhead. When all
trials have been executed, it stops.

Returning to our M/M/1 queue, suppose that we want to test the commonly accepted
queuing theory by testing utilization factors from 0.025 to 0.975 in steps of 0.025,
and that we want to run 10 replications of each parameter combination. We then
want to calculate and plot the mean and 95 % confidence bands for each parameter
combination, and compare that to the analytically calculated value in publication-
ready graphics. (Not that any famous journal would accept an article purporting to confirm
the queue length formula for the M/M/1 queue, but this is only a tutorial after all.)

We can set up the experiment like this:

.. code-block:: c

    const unsigned n_rhos = 39;
    const double rho_start = 0.025;
    const double rho_step = 0.025;
    const unsigned n_reps = 10;

    const double srv_rate = 1.0;
    const double warmup_time = 1000.0;
    const double duration = 1.0e6;

    printf("Setting up experiment\n");
    const unsigned n_trials = n_rhos * n_reps;
    struct trial *experiment = calloc(n_trials, sizeof(*experiment));

    uint64_t ui_exp = 0u;
    double rho = rho_start;
    for (unsigned ui_rho = 0u; ui_rho < n_rhos; ui_rho++) {
        for (unsigned ui_rep = 0u; ui_rep < n_reps; ui_rep++) {
            experiment[ui_exp].arr_rate = rho * srv_rate;
            experiment[ui_exp].srv_rate = srv_rate;
            experiment[ui_exp].warmup_time = warmup_time;
            experiment[ui_exp].duration = duration;
            experiment[ui_exp].seed_used = 0u;
            experiment[ui_exp].avg_queue_length = 0.0;

            ui_exp++;
        }

        rho += rho_step;
    }

We allocate the experiment array on the heap, since its size will depend on the
parameters. Here, we have hard-coded them for the sake of the tutorial, but they
would probably be given as an input file or as interactive input in real usage.

.. note:

   Do not use any writeable global variables. The entire parallelized experiment
   exists in a shared memory space. Threads will be sharing CPU cores in unpredictable
   ways. A global variable accessible to several threads can be read and written
   by any thread, creating potential hard-to-diagnose bugs.

   Do not use any static local variables either. The functions are called by all
   threads. A static local variable remembers its value from the last call, which
   may have been a completely different thread. Diagnosing those bugs will not be
   easy either.

   Regular local variables, function arguments, and heap memory (`malloc()` /
   `free()`) is thread safe.

   If you absolutely *must* have a global or static variable, consider prefixing
   it by `CMB_THREAD_LOCAL` to make it global or static within that thread only,
   creating separate copies per thread.

We can then run the experiment:

.. code-block:: c

        cimba_run_experiment(experiment, n_trials, sizeof(*experiment), run_MM1_trial);

The first argument is the experiment array, the last argument the simulation
driver function we have developed earlier. It will take a pointer to a trial as
its argument, but the internals of `cimba_run_experiment()` cannot know the
detailed structure of your `struct trial`, so it will be passed as a `void *`.
We need to explain the number of trials and the size of each tiral struct as the
second and third arguments to `cimba_run_experiment()` for it to do correct
pointer arithmetic internally.

When done, we can collect the results like this:

.. code-block:: c

        ui_exp = 0u;
        FILE *datafp = fopen("tut_1_6.dat", "w");
        fprintf(datafp, "# utilization\tavg_queue_length\tconf_interval\n");
        for (unsigned ui_rho = 0u; ui_rho < n_rhos; ui_rho++) {
            const double ar = experiment[ui_exp].arr_rate;
            const double sr = experiment[ui_exp].srv_rate;
            const double rho_used = ar / sr;

            struct cmb_datasummary cds;
            cmb_datasummary_initialize(&cds);
            for (unsigned ui_rep = 0u; ui_rep < n_reps; ui_rep++) {
                cmb_datasummary_add(&cds, experiment[ui_exp].avg_queue_length);
                ui_exp++;
            }

            cmb_assert_debug(cmb_datasummary_count(&cds) == n_reps);
            const double sample_avg = cmb_datasummary_mean(&cds);
            const double sample_sd = cmb_datasummary_stddev(&cds);
            const double t_crit = 2.228;
            fprintf(datafp, "%f\t%f\t%f\n", rho_used, sample_avg, t_crit * sample_sd);
            cmb_datasummary_terminate(&cds);
        }

        fclose(datafp);
        free(experiment);

        write_gnuplot_commands();
        system("gnuplot -persistent tut_1_6.gp");

We use a `cmb_datasummary` to simplify the calculation of confidence intervals,
knowing that it will calculate correct moments in a single pass of the data. We
then write the results to an output file, write a gnuplot command file to plot
the results, and spawn a gnuplot window to display the chart.

Also, we would like to know the progress of our experiment, so we define a
separate level of logger messages like this:

.. code-block:: c

    #define USERFLAG1 0x00000001
    #define USERFLAG2 0x00000002

.. note::

    The logging flags are bitmasks, not consecutive numbers. The next three
    values would be `0x00000004`, `0x00000008`, and `0x00000010`. You can
    combine flag values bit-wise. For instance, a call to `cmb_logger_user()`
    with flag level 63 (`0xFF`) will print a line if *any* of the lowest 8 bits
    are set.


We add a logging call to our `run_MM1_trial()`:

.. code-block:: c

    cmb_logger_user(stdout, USERFLAG2,
                    "seed: 0x%016" PRIx64 " rho: %f",
                    trl->seed_used, trl->arr_rate / trl->srv_rate);

We use the macro `PRIx64` from `#include <inttypes.h>` for portable formatting
of the `uint64_t` seed value.

We add some code to measure run time and some extra `printf()` calls, compile
and run, and get output similar to this::

    /home/ambonvik/github/cimba/build/tutorial/tut_1_6
    Cimba version 3.0.0-beta
    Setting up experiment
    Executing experiment
    0	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xc81e7ac2d54abef1 rho: 0.025000
    2	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xb995a846d37f1522 rho: 0.025000
    1	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x246364a107f945e7 rho: 0.025000
    5	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xa7b5e743900ccc53 rho: 0.025000
    4	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x775e54d85c8760eb rho: 0.025000
    3	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x6d4f0bb78fab7321 rho: 0.025000
    6	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xa0d4d65c953e6ba9 rho: 0.025000
    7	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xc66885b9c3e01198 rho: 0.025000
    10	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x0d0324ac1ad47314 rho: 0.050000
    9	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x60ea3e25c23886cd rho: 0.025000
    8	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xb2cf2d84fb2cd36e rho: 0.025000
    11	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x04b83d03be4d5393 rho: 0.050000
    12	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x6a3c8c7d7657b5a2 rho: 0.050000
    13	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x79e878ab601d9ba9 rho: 0.050000
    14	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xcd50fbb55578f7d2 rho: 0.050000
    15	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xfabb1c5f934c9aad rho: 0.050000

    [...]

    384	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x8aa3bb76ccc324a6 rho: 0.975000
    385	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x9290801927a4f348 rho: 0.975000
    386	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xeaff225d8d61a4ad rho: 0.975000
    387	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xc2c20c0bef3959b7 rho: 0.975000
    388	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0xc11445ad99b4a5c9 rho: 0.975000
    389	    0.0000	dispatcher	run_MM1_trial (120):  seed: 0x5faccc75f803deef rho: 0.975000
    Finished experiment, writing results to file
    It took 3.41133 sec

Note that the Cimba logger now understands that it is running multithreaded and
prepends each logging line with the trial index in your experiment array. Note
also that the trials may not be executed in strict sequence, since we do not
control the detailed interleaving of the threads. That is up to the operating
system.

Each of simulations was one million time units long, the average service time was
one time unit, and we ran 390 trials, adding up to 390 million time units.
We think executing that in 3.4 seconds is pretty fast.

We also get this image in a separate window:

    .. image:: ../images/tut_1_6.png

Evidently, we cannot reject the null hypothesis that conventional queuing theory
may be correct. Nor can we reject the hypthesis that Cimba may be working correctly.

This concludes our first tutorial. We have followed the development steps from a
first minimal model to demonstrate process interactions to a complete parallellized
experiment with graphical output. The files `tutorial/tut_1_*.c` include working
code for each stage of development. There is also a complete version with inline
explanatory comments in `tutorial/tut_1_x.c`.