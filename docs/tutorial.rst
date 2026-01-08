.. _tutorial:

Tutorial: Modeling with Cimba
=============================

Our first simulation - M/M/1 queue
------------------------------------

In this section, we will walk through the development of a simple model from
connecting basic entities and interactions to parallelizing the model on all
available CPU cores and producing presentation-ready output.

Our first simulated system is a M/M/1 queue. In queuing theory (Kendall)
notation, this abbreviation indicates a queuing system where the arrival process
is memoryless with exponentially distributed intervals, the service process is
the same, there is only one server, and the queue has unlimited capacity. This
is a mathematically well understood system.

The simulation model will verify if the well-known formula for expected
queue length is correct (or vice versa).

Arrival, service, and the queue
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

We model this in a straightforward manner: We need an arrival process that puts
customers into the queue at random intervals, a service process that gets
customers from the queue and services them for a random duration, and the queue
itself. We are not concerned with the characteristics of each customer, just how
many there are in the queue, so we do not need a separate object for each customer.
We will use a ``cmb_buffer`` for this. In this first iteration, we will hard-code
parameter values for simplicity, such as 75 % utilization, and then do it properly
shortly.

Let us start with the arrival and service processes. The code can be very simple:

.. code-block:: c

     void *arrival(struct cmb_process *me, void *ctx)
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

Note that the number of customers to ``put`` or ``get`` is given as a *pointer to
a variable* containing the number, not just a value. In more complex scenarios
than this, the process may encounter a partially completed put or get, and we
need a way to capture the actual state in these cases. For now, just note that
the amount argument to ``cmb_buffer_put()`` and ``cmb_buffer_get()`` is a
pointer to an unsigned 64-bit integer variable.

The process function signature is a function returning a pointer to void (i.e. a
raw address to anything). It takes two arguments, the first one a pointer to a
``cmb_process`` (itself), the second a pointer to void that gives whatever context
the process needs to execute. For now, we only use the context pointer as a
pointer to the queue, and do not use the ``me`` pointer or the return value.

Note also that all Cimba functions used here start with ``cmb_``, indicating
that they belong in the namespace of things in the simulated world. There are
functions from three Cimba modules here, ``cmb_process``, ``cmb_buffer``, and
``cmb_random``. We will encounter other namespaces and modules soon.

We also need a main function to set it all up, run the simulation, and clean up
afterwards. Let us try this:

.. code-block:: c

    int main(void)
    {
        const uint64_t seed = cmb_random_hwseed();
        cmb_random_initialize(seed);

        cmb_event_queue_initialize(0.0);

        struct cmb_buffer *que = cmb_buffer_create();
        cmb_buffer_initialize(que, "Queue", CMB_UNLIMITED);

        struct cmb_process *arr_proc = cmb_process_create();
        cmb_process_initialize(arr_proc, "Arrival", arrival, que, 0);
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

Next, it creates and initializes the ``cmb_buffer``, naming it "Queue" and setting
it to unlimited size.

After that, it creates, initializes, and starts the arrival and service processes.
They get a pointer to the newly created ``cmb_buffer`` as their context argument,
and the event queue is ready, so they can just start immediately.

Notice the pattern here: Objects are first *created*, then *initialized* in a
separate call. The create method allocates heap memory for the object, the initialize
method makes it ready for use. Some objects, such as the event queue, already
exist in pre-allocated memory and cannot be created. There are no
``cmb_event_queue_create()`` or ``cmb_event_queue_destroy()`` functions.

In later examples, we will also
see cases where some Cimba object is simply declared as a local variable and
allocated memory on the stack. We still need to initialize it, since we are not
in C++ with its "Resource Allocation Is Initialization" (RAII) paradigm. In C,
resource allocation is *not* initialization (RAINI?), and we need to be very
clear on each object's memory allocation and initialization status. We have tried
to be as consistent as possible in the Cimba create/initialize/terminate/destroy
object lifecycle.

Having made it this far, ``main()`` calls ``cmb_event_queue_execute()`` to run the
simulation before cleaning up.

Note that there are matching ``_terminate()`` calls for each ``_initialize()`` and
matching ``_destroy()`` for each ``_create()``. These functions un-initialize and
and de-allocate the objects that were created and initialized.

We can now run our first simulation and see what happens. It will generate
output like this:

.. code-block:: none

    [ambonvik@Threadripper cimba]$ ./build/tutorial/tut_1_1 | more
        0.0000	dispatcher	cmb_process_start (121):  Start Arrival 0x55ea2f505490
        0.0000	dispatcher	cmb_process_start (121):  Start Service 0x55ea2f505e10
        0.0000	dispatcher	cmb_event_queue_execute (252):  Starting simulation run
        0.0000	Arrival	cmb_process_hold (289):  Hold until time 2.182560
        0.0000	Service	cmb_buffer_get (207):  Gets 1 from Queue, level 0
        0.0000	Service	cmb_buffer_get (245):  Waiting for more, level now 0
        0.0000	Service	cmb_resourceguard_wait (128):  Waits for Queue
        2.1826	dispatcher	proc_holdwu_evt (255):  Wakes Arrival signal 0 wait type 1
        2.1826	Arrival	cmb_buffer_put (292):  Puts 1 into Queue, level 0
        2.1826	Arrival	cmb_buffer_put (299):  Success, found room for 1, has 0 remaining
        2.1826	Arrival	cmb_resourceguard_signal (196):  Scheduling wakeup event for Service
        2.1826	Arrival	cmb_process_hold (289):  Hold until time 2.894681
        2.1826	dispatcher	resgrd_waitwu_evt (149):  Wakes Service signal 0 wait type 4
        2.1826	Service	cmb_buffer_get (252):  Returned successfully from wait
        2.1826	Service	cmb_buffer_get (207):  Gets 1 from Queue, level 1
        2.1826	Service	cmb_buffer_get (214):  Success, 1 was available, got 1
        2.1826	Service	cmb_process_hold (289):  Hold until time 2.703327
        2.7033	dispatcher	proc_holdwu_evt (255):  Wakes Service signal 0 wait type 1
        2.7033	Service	cmb_buffer_get (207):  Gets 1 from Queue, level 0
        2.7033	Service	cmb_buffer_get (245):  Waiting for more, level now 0
        2.7033	Service	cmb_resourceguard_wait (128):  Waits for Queue
        2.8947	dispatcher	proc_holdwu_evt (255):  Wakes Arrival signal 0 wait type 1
        2.8947	Arrival	cmb_buffer_put (292):  Puts 1 into Queue, level 0
        2.8947	Arrival	cmb_buffer_put (299):  Success, found room for 1, has 0 remaining
        2.8947	Arrival	cmb_resourceguard_signal (196):  Scheduling wakeup event for Service
        2.8947	Arrival	cmb_process_hold (289):  Hold until time 7.249741
        2.8947	dispatcher	resgrd_waitwu_evt (149):  Wakes Service signal 0 wait type 4
        2.8947	Service	cmb_buffer_get (252):  Returned successfully from wait
        2.8947	Service	cmb_buffer_get (207):  Gets 1 from Queue, level 1
        2.8947	Service	cmb_buffer_get (214):  Success, 1 was available, got 1
        2.8947	Service	cmb_process_hold (289):  Hold until time 5.111000
        5.1110	dispatcher	proc_holdwu_evt (255):  Wakes Service signal 0 wait type 1
        5.1110	Service	cmb_buffer_get (207):  Gets 1 from Queue, level 0
        5.1110	Service	cmb_buffer_get (245):  Waiting for more, level now 0
        5.1110	Service	cmb_resourceguard_wait (128):  Waits for Queue

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
other coroutine. Calling ``cmb_process_hold()`` will do exactly that, transferring
control to the hidden dispatcher process that determines what to do next.

However, the dispatcher only knows about events, not coroutines or processes. It will
run as long as there are scheduled events to execute. Our little simulation will always
have scheduled events, and the dispatcher will not stop on its own. These events
originate from our two processes: To ensure that a process returns to the other end of
its ``cmb_process_hold()`` call, it will schedule a wakeup event at the expected time
before it yields control to the dispatcher. When executed, this event will *resume*
the coroutine where it left off, returning through the ``cmb_process_hold()`` call with a
return value that indicates normal or abnormal return. (We have ignored the
return values for now in the example above.) So, whenever there are more than
one process running, there may be future events scheduled in the event queue.

To stop the simulation, we simply schedule an "end simulation" event, which
stops any running processes at that point. The dispatcher then ends the run.

This is perhaps easier to do in code than to describe in text. We define a
``struct simulation`` that contains pointers to the entities of our simulated world and
the function for an end simulation event:

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

We then store pointers to the simulation entities in the ``struct simulation``
and schedule an ``end_sim`` event before executing the event queue:

.. code-block:: c

    int main(void)
    {
        const uint64_t seed = cmb_random_hwseed();
        cmb_random_initialize(seed);

        cmb_event_queue_initialize(0.0);

        struct simulation sim = {};
        sim.que = cmb_buffer_create();
        cmb_buffer_initialize(sim.que, "Queue", CMB_UNLIMITED);

        sim.arr = cmb_process_create();
        cmb_process_initialize(sim.arr, "Arrival", arrival, sim.que, 0);
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

The arguments to ``cmb_event_schedule`` are the event function, its subject and
object pointers, the simulated time when this event will happen, and an event
priority. We have set end time 10.0 here. It could also be expressed as
``cmb_time() + 10.0`` for "in 10.0 time units from now".

In Cimba, the simulation end does not even have to be at a predetermined time. It is
equally valid for some process in the simulation to schedule an end simulation
event at the current time whenever some condition is met, such as a certain
number of customers having been serviced, a statistics collector having a
certain number of samples, or something else. Or, perhaps even easier, the arrival
process could just stop generating new arrivals, the event queue would clear, and the
simulation would stop. (See ``benchmark/MM1_single.c`` for an example doing exactly that.)

We gave the end simulation event a default priority of 0 as the last argument to
``cmb_event_schedule()``. Priorities are signed 64-bit integers, ``int64_t``. The
Cimba dispatcher will always select the scheduled event with the lowest
scheduled time as the next event. The simulation clock then jumps to that time and that
event will be executed. If several events
have the *same* scheduled time, the dispatcher will execute the one with the
highest priority first. If several events have the same scheduled time *and*
the same priority, it will execute them in first in first out order.

Again, we roundly ignored the event handle returned by ``cmb_event_schedule()``,
since we will not be using it in this example. If we wanted to keep it, it is an
unsigned 64-bit integer (``uint64_t``).

When initializing our arrivals and service processes, we quietly set the last
argument to ``cmb_process_initialize()`` to 0. This was the inherent process
priority for scheduling any events pertaining to this process, its
priority when waiting for some resource, and so on. The processes can adjust
their own (or each other's) priorities during the simulation, dynamically
moving themselves up or down in various queues. Cimba does not attempt to
adjust any priorities by itself, it just acts on whatever the priorities are,
and reshuffles any existing queues as needed if priorities change.

We compile and run, and get something like this:

.. code-block:: none

    [ambonvik@Threadripper cimba]$ ./build/tutorial/tut_1_2
        0.0000	dispatcher	cmb_process_start (121):  Start Arrival 0x559e2bbea490
        0.0000	dispatcher	cmb_process_start (121):  Start Service 0x559e2bbeae10
        0.0000	dispatcher	cmb_event_queue_execute (252):  Starting simulation run
        0.0000	Arrival	cmb_process_hold (289):  Hold until time 0.385819
        0.0000	Service	cmb_buffer_get (207):  Gets 1 from Queue, level 0
        0.0000	Service	cmb_buffer_get (245):  Waiting for more, level now 0
        0.0000	Service	cmb_resourceguard_wait (128):  Waits for Queue
       0.38582	dispatcher	proc_holdwu_evt (255):  Wakes Arrival signal 0 wait type 1
       0.38582	Arrival	cmb_buffer_put (292):  Puts 1 into Queue, level 0
       0.38582	Arrival	cmb_buffer_put (299):  Success, found room for 1, has 0 remaining
       0.38582	Arrival	cmb_resourceguard_signal (196):  Scheduling wakeup event for Service
       0.38582	Arrival	cmb_process_hold (289):  Hold until time 1.216324
       0.38582	dispatcher	resgrd_waitwu_evt (149):  Wakes Service signal 0 wait type 4
       0.38582	Service	cmb_buffer_get (252):  Returned successfully from wait
       0.38582	Service	cmb_buffer_get (207):  Gets 1 from Queue, level 1
       0.38582	Service	cmb_buffer_get (214):  Success, 1 was available, got 1
       0.38582	Service	cmb_process_hold (289):  Hold until time 0.503544

            ...

        9.8545	dispatcher	proc_holdwu_evt (255):  Wakes Service signal 0 wait type 1
        9.8545	Service	cmb_buffer_get (207):  Gets 1 from Queue, level 4
        9.8545	Service	cmb_buffer_get (214):  Success, 1 was available, got 1
        9.8545	Service	cmb_process_hold (289):  Hold until time 12.376642
        10.000	dispatcher	cmb_process_stop (612):  Stop Arrival value (nil)
        10.000	dispatcher	cmb_process_stop (612):  Stop Service value (nil)
        10.000	dispatcher	proc_stop_evt (574):  Stops Arrival signal 0 wait type 0
        10.000	dispatcher	proc_stop_evt (574):  Stops Service signal 0 wait type 0
        10.000	dispatcher	cmb_event_queue_execute (255):  No more events in queue

Progress: It started, ran, and stopped.

Setting logging levels
^^^^^^^^^^^^^^^^^^^^^^

Next, the verbiage. As you would expect at this point in the tutorial, Cimba has
powerful and flexible logging that gives you fine-grained control of what to log.

The core logging function is called ``cmb_logger_vfprintf()``. As the name says, it is
similar to the standard function ``vfprintf()`` but with some Cimba-specific added
features. You will rarely interact directly with this function, but instead call
wrapper functions (actually macros) like ``cmb_logger_user()`` or ``cmb_logger_error()``.

The key concept to understand here is the *logger flags*. Cimba uses a 32-bit
unsigned integer (``uint32_t``) as a bit mask to determine what log entries to print
and which to ignore. Cimba reserves the top four bits for its own use, identifying
messages of various severities, leaving the 28 remaining bits for the user application.

There is a central bit field and a bit mask in each call. If a simple bitwise
and (``&``) between the central bit field and the caller's bit mask gives a non-
zero result, that line is printed, otherwise not. Initially, all bits in the central
bit field are on, ``0xFFFFFFFF``. You can turn selected bits on and off with
``cmb_logger_flags_on()`` and ``cmb_logger_flags_off()``.

Again, it may be easier to show this in code than to explain. We add a user-defined logging
message to the end event and the two processes. The messages take ``printf``-style
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

    void *arrival(struct cmb_process *me, void *ctx)
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

We compile and run, and get something like this:

.. code-block:: none

    [ambonvik@Threadripper cimba]$ build/tutorial/tut_1_3
    0.0000	Arrival	arrival (25):  Holds for 5.990663 time units
    0.0000	Service	service (38):  Gets one from the queue
    5.9907	Arrival	arrival (28):  Puts one into the queue
    5.9907	Arrival	arrival (25):  Holds for 0.687769 time units
    5.9907	Service	service (41):  Got one, services it for 0.758971 time units
    6.6784	Arrival	arrival (28):  Puts one into the queue
    6.6784	Arrival	arrival (25):  Holds for 2.199251 time units
    6.7496	Service	service (38):  Gets one from the queue
    6.7496	Service	service (41):  Got one, services it for 0.589826 time units
    7.3395	Service	service (38):  Gets one from the queue
    8.8777	Arrival	arrival (28):  Puts one into the queue
    8.8777	Arrival	arrival (25):  Holds for 0.523423 time units
    8.8777	Service	service (41):  Got one, services it for 1.277856 time units
    9.4011	Arrival	arrival (28):  Puts one into the queue
    9.4011	Arrival	arrival (25):  Holds for 1.751825 time units
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
start reporting some statistics on the queue length.

It will be no surprise that Cimba contains flexible and powerful statistics
collectors and reporting functions. There is actually one built into the
``cmb_buffer`` we have been using. We just need to turn it on:

.. code-block::

    struct simulation sim = {};
    sim.que = cmb_buffer_create();
    cmb_buffer_initialize(sim.que, "Queue", CMB_UNLIMITED);
    cmb_buffer_start_recording(sim.que);

After the simulation is finished, we can make it report its history like this:

.. code-block::

    cmb_buffer_stop_recording(sim.que);
    cmb_buffer_print_report(sim.que, stdout);

We increase the running time from ten to one million time units, compile, and run.
Very shortly thereafter, output appears:

.. code-block:: none

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

The text-mode histogram uses the character ``#`` to indicate a full pixel, ``=`` for
one that is more than half full, and ``-`` for one that contains something but less
than half filled.

We can also get a pointer to the ``cmb_timeseries`` object by
calling ``cmb_buffer_history()`` and doing further analysis on that. As an
example, let's do the first 20 partial autocorrelation coefficients of the queue
length time series and print a correlogram of that as well:

.. code-block:: c

    struct cmb_timeseries *ts = cmb_buffer_history(sim.que);
    double pacf_arr[21];
    cmb_timeseries_PACF(ts, 20, pacf_arr, NULL);
    cmb_timeseries_print_correlogram(ts, stdout, 20, pacf_arr);

Output:

.. code-block:: none

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
us about unused function arguments. Unused arguments is not unusual with Cimba,
since we often do not need all arguments to event and process functions. However,
we do not want to turn off the warning altogether, but will inform the compiler
(and human readers of the code) that this is intentional by using the ``cmb_unused()``
macro:

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
data structure. We define a ``struct trial`` to contain parameters and output
values, and bundle both our existing ``struct simulation`` and ``struct trial`` in
a ``struct context``, and pass that between the functions.

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

We define a small helper function to load the parameters into the ``trial``:

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

    Notice the ``cmb_assert_release`` in this code
    fragment. It is a custom version of the standard ``assert()`` macro, triggering
    a hard stop if the condition evaluates to ``false``. Our custom asserts come in
    two flavors, ``cmb_assert_debug()`` and ``cmb_assert_release()``. The ``_debug`` assert
    behaves like the standard one and goes away if the preprocessor symbol ``NDEBUG``
    is ``#defined``. The ``_release`` assert is still there, but also goes away if ``NASSERT``
    is ``#defined``.

    The usage pattern is typically that ``cmb_assert_debug()`` is used to test
    time-consuming invariants and post-conditions in the code, while
    ``cmb_assert_release()`` is used for simple input argument validity checks
    and similar pre-conditions that should be left in the production version
    of your model. Internally, you will find this pattern nearly everywhere in
    Cimba, as an example of programming by contract to ensure reliability and
    clarity. See also https://en.wikipedia.org/wiki/Design_by_contract

    We will trip one and see how it looks. We temporarily replace the
    exponentially distributed service time with a normally distributed one, mean
    1.0 and sigma 0.25. This will almost surely generate a negative value
    sooner or later, which will cause the service process to try to hold for a
    negative time, resuming in its own past. That should not be possible:

    .. code-block:: c

                // const double t_srv = cmb_random_exponential(t_srv_mean);
                const double t_srv = cmb_random_normal(1.0, 0.25);

    Sure enough::

        /home/ambonvik/github/cimba/build/tutorial/tut_1_5
        9359.5	Service	cmb_process_hold (272):  Fatal: Assert "dur >= 0.0" failed, source file cmb_process.c, seed 0x9bec8a16f0aa802a

        Process finished with exit code 134 (interrupted by signal 6:SIGABRT)

    The output line lists the simulated time, the process, the function and line of code,
    the condition that failed, the source code file where it blew up, and the random
    number seed that was used to initialize the run.

    If using a debugger, place a breakpoint  in ``cmi_assert_failed()``.
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
code from ``main()`` to a separate function, say ``run_MM1_trial()``, and call it from
``main()``. For reasons that soon will be evident, its argument is a single pointer
to void, even if we immediately cast this to our ``struct trial *`` once inside the
function. We remove the call to ``cmb_buffer_report()``, calculate the average
queue length, and store it in the ``trial`` results field:

.. code-block:: c

        struct cmb_wtdsummary wtdsum;
        const struct cmb_timeseries *ts = cmb_buffer_history(ctx.sim->que);
        cmb_timeseries_summarize(ts, &wtdsum);
        ctx.trl->avg_queue_length = cmb_wtdsummary_mean(&wtdsum);

The ``main()`` function is now reduced to this:

.. code-block:: c

    int main(void)
    {
        struct trial trl = {};
        load_params(&trl);

        run_MM1_trial(&trl);

        printf("Avg %f\n", trl.avg_queue_length);

        return 0;
    }

We will not repeat the rest of the code here. You can find it in tutorial/tut_1_5.c.
Instead, we compile and run it, receiving output similar to this:

.. code-block:: none

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
no less independent. This is what is called an "embarrassingly parallel" problem.
There is no interaction between the trials, and they can be trivially parallelized
by just running them at the same time and collecting the output.

Cimba creates a pool of worker threads, one per (logical) CPU core on the system.
You describe your experiment as an array of trials and the function to execute each
trial, and pass these to ``cimba_run_experiment()``.
The worker threads will pull trials from the experiment array and run them,
storing the results back in your trial struct, before moving to the next un-executed
trial in the array. This gives an inherent load balancing with minimal overhead. When all
trials have been executed, it stops.

Returning to our M/M/1 queue, suppose that we want to test the commonly accepted
queuing theory by testing utilization factors from 0.025 to 0.975 in steps of 0.025,
and that we want to run 10 replications of each parameter combination. We then
want to calculate and plot the mean and 95 % confidence bands for each parameter
combination, and compare that to the analytically calculated value in publication
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

.. note::

    Do not use any writeable global variables in your model. The entire parallelized
    experiment exists in a shared memory space. Threads will be sharing CPU cores
    in unpredictable ways. A global variable accessible to several threads can be
    read and written by any thread, creating potential hard-to-diagnose bugs.

    Do not use any static local variables in your model either. Your model
    functions will be called by all threads. A static local variable remembers its
    value from the last call, which may have been a completely different thread.
    Diagnosing those bugs will not be any easier.

    Regular local variables, function arguments, and heap memory (``malloc()`` /
    ``free()``) is thread safe.

    If you absolutely *must* have a global or static variable, consider prefixing
    it by ``CMB_THREAD_LOCAL`` to make it global or static *within that thread only*,
    creating separate copies per thread.

We can then run the experiment:

.. code-block:: c

        cimba_run_experiment(experiment, n_trials, sizeof(*experiment), run_MM1_trial);

The first argument is the experiment array, the last argument the simulation
driver function we have developed earlier. It will take a pointer to a trial as
its argument, but the internals of ``cimba_run_experiment()`` cannot know the
detailed structure of your ``struct trial``, so it will be passed as a ``void *``.
We need to explain the number of trials and the size of each trial struct as the
second and third arguments to ``cimba_run_experiment()`` for it to do correct
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

We use a ``cmb_datasummary`` to simplify the calculation of confidence intervals,
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
    values would be ``0x00000004``, ``0x00000008``, and ``0x00000010``. You can
    combine flag values bit-wise. For instance, a call to ``cmb_logger_user()``
    with flag level 63 (``0xFF``) will print a line if *any* of the lowest 8 bits
    are set.


We add a logging call to our ``run_MM1_trial()``:

.. code-block:: c

    cmb_logger_user(stdout, USERFLAG2,
                    "seed: 0x%016" PRIx64 " rho: %f",
                    trl->seed_used, trl->arr_rate / trl->srv_rate);

We use the macro ``PRIx64`` from ``#include <inttypes.h>`` for portable formatting
of the ``uint64_t`` seed value.

We add some code to measure run time and some extra ``printf()`` calls, compile
and run, and get output similar to this:

.. code-block:: none

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

We also get this image in a separate window:

    .. image:: ../images/tut_1_6.png

Evidently, we cannot reject the null hypothesis that conventional queuing theory
may be correct. Nor can we reject the hypothesis that Cimba may be working correctly.

This concludes our first tutorial. We have followed the development steps from a
first minimal model to demonstrate process interactions to a complete parallelized
experiment with graphical output. The files ``tutorial/tut_1_*.c`` include working
code for each stage of development. The version ``tutorial/tut_1_7.c`` is
functionally the same as our final ``tutorial\tut_1_6.c`` but with additional inline
explanatory comments.


Our second simulation - LNG tanker harbor
-------------------------------------------

Once upon a time, a harbor simulation with tugs puttering about was the author's
first exposure to Simula67, coroutines, and object-oriented programming. The
essential *rightness* made a lasting impression. Building a 21st century version
will be our second Cimba tutorial.

In our first tutorial, the active processes interacted through a ``cmb_buffer`` with
``put`` and ``get`` methods. We will now introduce other process interactions through
``cmb_resource`` and ``cmb_resourcepool`` with their ``acquire``, ``hold``, and ``release``
semantics, and the extremely powerful ``cmb_condition`` that allows arbitrarily
complex ``wait`` calls. We will also show how to create a derived "class" of ships
from our ``cmb_process`` class, itself derived from the ``cmi_coroutine`` class.

Since a simulation model only should be built in order to answer some specific
question or set of questions, we will assume that our Simulated Port Authority
needs to decide whether to spend next year's investment budget on buying more
tugs, building another berth, or dredging a deeper harbor channel. The relevant
performance metric is to minimize the average time spent in the harbor for the
ships. The ships come in two sizes, large and small, with different requirements
to wind, water depth, tugs, and berths. Our model will help the SPA decide. The
time unit in our simulation will be hours.

An empty simulation template
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Our starting point will be an empty shell from the first tutorial, giving the
correct initial structure to the model. You will find it in ``tutorial/tut_2_0.c``.
It does not do anything, so there is no point in compiling it as it is, but you
can use it as a starting template for your own models as well.

The first functional version is in ``tutorial/tut_2_1.c``. We will not repeat all
the code here, just point out the highlights.

Processes, resources, and conditions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The simulated world is described in ``struct simulation``. Again, there is an
arrival and a departure process generating and removing ships, but the ships
themselves are now also active processes. We have two pools of resources, the
tugs and the berths (of two different sizes), and one single resource, the
communication channel used to announce that a ship is moving.

There are also two condition variables. One, the harbormaster, ensures that all
necessary conditions (including tides and wind) are met and resources are available
before it permits a ship to start acquiring resources and proceed towards docking.
The other one, Davy Jones, just watches for ships leaving harbor and triggers the
departure process.

There is no guarantee that ships will be leaving in first in first out order, so
we use a ``cmi_hashheap`` as a fast data set for active ships. Departed ships can be
handled by simpler means, so a simple linked list with LIFO (stack) ordering will
be sufficient. These two classes are not considered part of the external Cimba
API, but happen to be available tools that fit the task.

Our environment is next, describing the current wind and tide state for use by
the harbormaster condition. Obviously, water depth will be different at different
locations in a harbor, but we assume that the local topography is known and
that a single tide gauge is sufficient. The ships' requirements are expressed
against this tide gauge.

Building our ships
^^^^^^^^^^^^^^^^^^

Our ships will come in two sizes, small and large. We define an ``enum`` for this,
explicitly starting from zero to use it directly as an array index later. We then
define a ``struct ship`` to be a derived class from ``struct cmb_process`` by placing
a ``struct cmb_process`` as the first member of ``struct ship``. (Not a pointer to a
``cmb_process`` - the ship *is a* process.) The rest of the ship struct contains the
characteristics of a particular ship object to be instantiated in the simulation.

.. code-block:: c

    enum ship_size {
        SMALL = 0,
        LARGE
    };

    /* A ship is a derived class from cmb_process */
    struct ship {
        struct cmb_process core;       /* <= Note: The real thing, not a pointer */
        enum ship_size size;
        unsigned tugs_needed;
        double max_wind;
        double min_depth;
    };

.. note::

    We do not use ``typedef`` for our object classes. It would only confuse matters
    by hiding the nature of the object. We want that to be very clear from the
    code. The only exception is for certain convoluted function prototypes like
    ``cmb_process_func`` and ``cmb_event_func``. These are ``typedef`` under those
    names to avoid complex and error-prone declarations and argument lists.

Weather and tides
^^^^^^^^^^^^^^^^^

Weather and tides are modelled as simple processes that update the
environment state once per hour, using a suitable stochastic and/or periodic
model of the physical world. The weather process can look like this, only
concerned about wind magnitude and direction:

.. code-block:: c

    /* A process that updates the weather once per hour */
    void *weather_proc(struct cmb_process *me, void *vctx)
    {
        cmb_unused(me);
        cmb_assert_debug(vctx != NULL);

        const struct context *ctxp = vctx;
        struct environment *envp = ctxp->env;
        const struct simulation *simp = ctxp->sim;

        while (true) {
            /* Wind magnitude in meters per second */
            const double wmag = cmb_random_rayleigh(5.0);
            const double wold = envp->wind_magnitude;
            envp->wind_magnitude = 0.5 * wmag + 0.5 * wold;

            /* Wind direction in compass degrees, dominant from the southwest */
            envp->wind_direction = cmb_random_PERT(0.0, 225.0, 360.0);

            /* Requesting the harbormaster to read the new weather bulletin */
            cmb_condition_signal(simp->harbormaster);

            /* ... and wait until the next hour */
            cmb_process_hold(1.0);
        }
    }

Notice that just before holding, we ``signal`` the harbormaster
condition, informing it that some state has changed, requiring it to re-evaluate
its list of waiting ships.

The tide process is similar, but combines the periodicity of astronomical tides
with the randomness of weather-driven tide calculated from the environmental state
left by the weather process. It also signals the harbormaster at the end of each
iteration. You find it in the source code,
``void *tide_proc(struct cmb_process *me, void *vctx)``.

The details of the weather and tide models are not important for this tutorial,
only that:

1. We can calculate arbitrary state variables, such as the wind and tide here,
   using relevant mathematical methods. We could embed an AI model or some custom
   CUDA programming here if we needed to, as long as it is thread safe for our
   concurrent execution. Our simulated world just stands still until the calculation
   is done, possibly leaving the CPU to some other trial thread in the meantime
   if this thread is waiting for a response from a GPU, I/O from disk, or another
   blocking system call.

2. We *signal* a ``cmb_condition`` that the state has changed and that
   it needs to re-evaluate the requirements of any processes waiting for it. The
   ``cmb_condition`` is not busy-polling the state, but depends on being signalled
   by whatever process changes the state. In a discrete event simulation, state
   only changes due to some event, and no polling is needed between events.

Resources, resourcepools, and condition variables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To understand the general ``cmb_condition`` class, it may be helpful to start
with the ``cmb_resource`` as a special case.

The ``cmb_resource`` is essentially a binary semaphore. Only one process can hold
it at a time. If some process tries to acquire the resource while it is already
in use, that process will have to wait in a priority queue.

There is a ``cmb_resourceguard`` managing the priority queue. When adding itself
to the queue, a process files its *demand* function with the guard. The demand
function is a predicate returning a ``bool``, either ``true`` or ``false``, essentially
saying to the guard "wake me up when this becomes true".

For a ``cmb_resource``, the demand function is internal and pre-defined, evaluating
to ``true`` if the resource is available. When some other process releases the
resource, the guard is signaled, the predicate evaluates to ``true``, and the
highest priority waiting process gets the resource, returning successfully from
its ``cmb_resource_acquire()`` call as the new holder of the resource.

Similarly, the ``cmb_resourcepool`` is a counting semaphore, where there is a
certain number of resource items, and a process can acquire and release more
than one unit at a time. Again, if not enough is available, the process files its
demand with the guard and waits. This demand function is also internal and
pre-defined.

The ``cmb_condition`` exposes the resource guard and demand mechanism to the user
application. It does not provide any particular resource object, but lets a
process wait until an arbitrary condition is satisfied. The demand function may
even be different for each waiting process. The condition will evaluate them in
turn, and will schedule a wakeup event at the current time for every waiting process
whose demand function evaluates to ``true``. What to do next is up to the user
application.

Making this mechanism even more powerful, the ``cmb_resourceguard`` class maintains
a list of other resource guards observing this one. This is a signal forwarding
mechanism. When signaled, a resource guard will evaluate its own priority queue,
possibly schedule wakeup events for waiting processes, and then forward the signal
to each observing resource guard for them to do the same. This makes it possible
for the ``cmb_condition`` to provide complex combinations of "wait for all", "wait
for any", and so forth by registering itself as an observer. If some observed
resource guard gets signaled, the ``cmb_condition`` will also be signaled.

The ``cmb_condition`` is still a passive object, not an active process. It only
responds to calls from the active processes, such as ``cmb_condition_wait()`` and
``cmb_condition_signal()``.

Armed with this knowledge, we can now define the demand predicate function for
a ship requesting permission from the harbormaster to dock:

.. code-block:: c

    /* The demand predicate function for a ship wanting to dock */
    bool is_ready_to_dock(const struct cmb_condition *cvp,
                          const struct cmb_process *pp,
                          const void *vctx) {
        cmb_unused(cvp);
        cmb_assert_debug(pp != NULL);
        cmb_assert_debug(vctx != NULL);

        const struct ship *shpp = (struct ship *)pp;
        const struct context *ctxp = vctx;
        const struct environment *envp = ctxp->env;
        const struct simulation *simp = ctxp->sim;

        if (envp->water_depth < shpp->min_depth) {
            cmb_logger_user(stdout, USERFLAG1,
                            "Water %f m too shallow for %s, needs %f",
                            envp->water_depth, pp->name, shpp->min_depth);
            return false;
        }

        if (envp->wind_magnitude > shpp->max_wind){
            cmb_logger_user(stdout, USERFLAG1,
                            "Wind %f m/s too strong for %s, max %f",
                            envp->wind_magnitude, pp->name, shpp->max_wind);
            return false;
        }

        if (cmb_resourcepool_available(simp->tugs) < shpp->tugs_needed) {
            cmb_logger_user(stdout, USERFLAG1,
                            "Not enough available tugs for %s",
                            pp->name);
            return false;
        }

        if (cmb_resourcepool_available(simp->berths[shpp->size]) < 1u) {
            cmb_logger_user(stdout, USERFLAG1,
                            "No available berth for %s",
                            pp->name);
            return false;
        }

        cmb_logger_user(stdout, USERFLAG1, "All good for %s", pp->name);
        return true;
    }

The ship demands all this to hold true *before* it starts acquiring any tugs or
other resources. This prevents ships from grabbing any tugs before their berth is
available and the conditions permit docking. Cimba's observer pattern for signal
forwarding ensures that this function is evaluated for each ship whenever the
environmental state is updated or some resource has been released.

The life of a ship
^^^^^^^^^^^^^^^^^^

Our ships are derived from the ``cmb_process`` class, inherit all properties from
there, and adds some of its own. The function to execute in a ship process has
the same signature as for the ``cmb_process``. It can look like this:

.. code-block:: c

    /* The ship process function */
    void *ship_proc(struct cmb_process *me, void *vctx)
    {
        cmb_assert_debug(me != NULL);
        cmb_assert_debug(vctx != NULL);

        /* Unpack some convenient shortcut names */
        struct ship *shpp = (struct ship *)me;
        const struct context *ctxp = vctx;
        struct simulation *simp = ctxp->sim;
        struct cmb_condition *hbmp = simp->harbormaster;
        const struct trial *trlp = ctxp->trl;

        /* Note ourselves as active */
        cmb_logger_user(stdout, USERFLAG1, "%s arrives", me->name);
        const double t_arr = cmb_time();
        const uint64_t hndl = cmi_hashheap_enqueue(simp->active_ships, shpp,
                                                   NULL, NULL, NULL, t_arr, 0u);

        /* Wait for suitable conditions to dock */
        while (!is_ready_to_dock(NULL, me, ctxp)) {
            /* Loop to catch any spurious wakeups, such as several ships waiting for
             * the tide and one of them grabbing the tugs before we can react. */
            cmb_condition_wait(hbmp, is_ready_to_dock, ctxp);
        }

        /* Resources are ready, grab them for ourselves */
        cmb_logger_user(stdout, USERFLAG1, "%s cleared to dock", me->name);
        cmb_resourcepool_acquire(simp->berths[shpp->size], 1u);
        cmb_resourcepool_acquire(simp->tugs, shpp->tugs_needed);

        /* Announce our intention to move */
        cmb_resource_acquire(simp->comms);
        cmb_process_hold(cmb_random_gamma(5.0, 0.01));
        cmb_resource_release(simp->comms);

        const double docking_time = cmb_random_PERT(0.4, 0.5, 0.8);
        cmb_process_hold(docking_time);

        /* Safely at the quay to unload cargo, dismiss the tugs for now */
        cmb_logger_user(stdout, USERFLAG1, "%s docked, unloading", me->name);
        cmb_resourcepool_release(simp->tugs, shpp->tugs_needed);
        const double tua = trlp->unloading_time_avg[shpp->size];
        const double unloading_time = cmb_random_PERT(0.75 * tua, tua, 2 * tua);
        cmb_process_hold(unloading_time);

        /* Need the tugs again to get out of here */
        cmb_logger_user(stdout, USERFLAG1, "%s ready to leave", me->name);
        cmb_resourcepool_acquire(simp->tugs, shpp->tugs_needed);

        /* Announce our intention to move */
        cmb_resource_acquire(simp->comms);
        cmb_process_hold(cmb_random_gamma(5.0, 0.01));
        cmb_resource_release(simp->comms);

        const double undocking_time = cmb_random_PERT(0.4, 0.5, 0.8);
        cmb_process_hold(undocking_time);

        /* Cleared berth, done with the tugs */
        cmb_logger_user(stdout, USERFLAG1, "%s left harbor", me->name);
        cmb_resourcepool_release(simp->berths[shpp->size], 1u);
        cmb_resourcepool_release(simp->tugs, shpp->tugs_needed);

        /* One pass process, remove ourselves from the active set */
        cmi_hashheap_remove(simp->active_ships, hndl);
        /* List ourselves as departed instead */
        cmi_list_push(&(simp->departed_ships), shpp);
        /* Inform Davy Jones that we are coming his way */
        cmb_condition_signal(simp->davyjones);

        /* Store the time we spent as an exit value in a separate heap object.
         * The exit value is a void*, so we could store anything there, but for this
         * demo, we keep it simple. */
        const double t_dep = cmb_time();
        double *t_sys_p = malloc(sizeof(double));
        *t_sys_p = t_dep - t_arr;

        cmb_logger_user(stdout, USERFLAG1, "%s arr %g dep %f time in system %f",
            me->name, t_arr, t_dep, *t_sys_p);

        /* Note that returning from a process function has the same effect as calling
         * cmb_process_exit() with the return value as argument. */
        return t_sys_p;
    }


Note the loop on ``cmb_condition_wait()``. The condition will schedule a wakeup event
for all waiting processes with a satisfied demand, but it is entirely possible
that some other ship wakes first and grabs the resources before control passes here.
Therefore, we test and wait again if it is no longer satisfied.

For readers familiar with POSIX condition variables, there is a notable lack of
a protecting mutex here. It is not needed in a coroutine-based concurrency model.
Once control is back in this process, it will not be interrupted before we yield
the execution through some call like ``cmb_process_hold()``. In particular, this
is safe:

.. code-block:: c

        /* Wait for suitable conditions to dock */
        while (!is_ready_to_dock(NULL, me, ctxp)) {
            /* Loop to catch any spurious wakeups, such as several ships waiting for
             * the tide and one of them grabbing the tugs before we can react. */
            cmb_condition_wait(hbmp, is_ready_to_dock, ctxp);
        }

        /* Resources are ready, grab them for ourselves */
        cmb_logger_user(stdout, USERFLAG1, "%s cleared to dock", me->name);
        cmb_resourcepool_acquire(simp->berths[shpp->size], 1u);
        cmb_resourcepool_acquire(simp->tugs, shpp->tugs_needed);

        /* Announce our intention to move */
        cmb_resource_acquire(simp->comms);
        cmb_process_hold(cmb_random_gamma(5.0, 0.01));
        cmb_resource_release(simp->comms);

We know that tugs and berths are available from the ``cmb_condition_wait()``, so
the ``acquire()`` calls will return immediately and successfully.

On the other hand, this is not safe at all:

.. code-block:: c

        /* Wait for suitable conditions to dock */
        while (!is_ready_to_dock(NULL, me, ctx)) {
            /* Loop to catch any spurious wakeups, such as several ships waiting for
             * the tide and one of them grabbing the tugs before we can react. */
            cmb_condition_wait(hbm, is_ready_to_dock, ctx);
        }

        /* Do NOT do this: Hold and/or request a resource not part of the condition
         * predicate, yielding execution to other processes that may invalidate our
         * condition before we act on it. */
        cmb_resource_acquire(simp->comms);
        cmb_process_hold(cmb_random_gamma(5.0, 0.01));
        cmb_resource_release(simp->comms);

        /* Who knows what happened to the resources in the meantime? */
        cmb_logger_user(stdout, USERFLAG1, "%s cleared to dock", me->name);
        cmb_resourcepool_acquire(simp->berths[shpp->size], 1u);
        cmb_resourcepool_acquire(simp->tugs, shpp->tugs_needed);

A mutex is not needed, but only because a coroutine has atomic execution between
explicit yield points. It is the application program's own responsibility to avoid
doing something that could invalidate the condition before acting on it. If your code
needs a simulated mutex for some reason, a simple ``cmb_resource`` will do, since it is
a binary semaphore that only can be released by the process that acquired it.

We next write the arrival process generating ships:

.. code-block:: c

    /* The arrival process generating new ships */
    void *arrival_proc(struct cmb_process *me, void *vctx)
    {
        cmb_unused(me);
        cmb_assert_debug(vctx != NULL);

        const struct context *ctxp = vctx;
        const struct trial *trlp = ctxp->trl;
        const double mean = 1.0 / trlp->arrival_rate;
        const double p_large = trlp->percent_large;

        uint64_t cnt = 0u;
        while (true) {
            cmb_process_hold(cmb_random_exponential(mean));

            /* The ship class is a derived sub-class of cmb_process, we malloc it
             * directly instead of calling cmb_process_create() */
            struct ship *shpp = malloc(sizeof(struct ship));

            /* Remember to zero-initialize it if malloc'ing on your own! */
            memset(shpp, 0, sizeof(struct ship));

            /* We started the ship size enum from 0 to match array indexes. If we
             * had more size classes, we could use cmb_random_dice(0, n) instead. */
            shpp->size = cmb_random_bernoulli(p_large);

            /* We would probably not hard-code parameters except in a demo like this */
            shpp->max_wind = 10.0 + 2.0 * (double)(shpp->size);
            shpp->min_depth = 8.0 + 5.0 * (double)(shpp->size);
            shpp->tugs_needed = 1u + 2u * shpp->size;

            /* A ship needs a name */
            char namebuf[20];
            snprintf(namebuf, sizeof(namebuf),
                     "Ship_%06" PRIu64 "%s",
                     ++cnt, ((shpp->size == SMALL) ? "_small" : "_large"));
            cmb_process_initialize((struct cmb_process *)shpp, namebuf, ship_proc, vctx, 0);

            /* Start our brand new ship heading into the harbor */
            cmb_process_start((struct cmb_process *)shpp);
            cmb_logger_user(stdout, USERFLAG1, "%s started", namebuf);
        }
    }

The important point to remember here is to zero-initialize the ``struct ship``
with ``memset()`` after allocating it with ``malloc()``, or equivalently,
allocating it with ``calloc()`` instead. The ship is a ``cmb_process``, but
we are bypassing the ``cmb_process_create()`` here and take the responsibility
for the allocation step.

In this example, we just did the ship allocation and initialization inline. If we were to
create and/or initialize ships from more than one place in the
code, we would wrap these in proper ``ship_create()`` and ``ship_initialize()``
functions to avoid repeating ourselves, but there is nothing that forces us to write
pro forma constructor and destructor functions. For illustration and code style, we
do this "properly" in the next iteration of the example, ``tutorial/tut_2_2.c``, where
the ship class looks like this:

.. code-block:: C

    * A ship is a derived class from cmb_process */
    struct ship {
        struct cmb_process core;       /* <= Note: The real thing, not a pointer */
        enum ship_size size;
        unsigned tugs_needed;
        double max_wind;
        double min_depth;
    };

    /* We'll do the object lifecycle properly with constructors and destructors. */
    struct ship *ship_create(void)
    {
        struct ship *shpp = malloc(sizeof(struct ship));
        memset(shpp, 0, sizeof(struct ship));

        return shpp;
    }

    /* Process function to be defined later, for now just declare that it exists */
    void *ship_proc(struct cmb_process *me, void *vctx);

    void ship_initialize(struct ship *shpp, const enum ship_size sz, uint64_t cnt, void *vctx)
    {
        cmb_assert_release(shpp != NULL);
        shpp->size = sz;

        /* We would probably not hard-code parameters except in a demo like this */
        shpp->max_wind = 10.0 + 2.0 * (double)(shpp->size);
        shpp->min_depth = 8.0 + 5.0 * (double)(shpp->size);
        shpp->tugs_needed = 1u + 2u * shpp->size;

        char namebuf[20];
        snprintf(namebuf, sizeof(namebuf),
                 "Ship_%06" PRIu64 "%s",
                 ++cnt, ((shpp->size == SMALL) ? "_small" : "_large"));

        /* Done initializing the child class properties, pass it on to the parent class */
        cmb_process_initialize((struct cmb_process *)shpp, namebuf, ship_proc, vctx, 0);
    }

    void ship_terminate(struct ship *shpp)
    {
        /* Nothing needed for the ship itself, pass it on to parent class */
        cmb_process_terminate((struct cmb_process *)shpp);
    }

    void ship_destroy(struct ship *shpp)
    {
        free(shpp);
    }

The departure process is reasonably straightforward, capturing the exit value from
the ship process and then recycling the entire ship. A ``cmb_condition`` is used
to know that one or more ships have departed, triggering the departure process
to do something. Here, we use our new destructor functions:

.. code-block:: c

    void *departure_proc(struct cmb_process *me, void *vctx)
    {
        cmb_unused(me);
        cmb_assert_debug(vctx != NULL);

        const struct context *ctxp = vctx;
        struct simulation *simp = ctxp->sim;
        const struct trial *trlp = ctxp->trl;
        struct cmi_list_tag **dep_head = &(simp->departed_ships);

        while (true) {
            /* We do not need to loop here, this is the only process waiting */
            cmb_condition_wait(simp->davyjones, is_departed, vctx);

            /* There is one, collect its exit value */
            struct ship *shpp = cmi_list_pop(dep_head);
            double *t_sys_p = cmb_process_exit_value((struct cmb_process *)shpp);
            cmb_assert_debug(t_sys_p != NULL);
            cmb_logger_user(stdout, USERFLAG1,
                            "Recycling %s, time in system %f",
                            ((struct cmb_process *)shpp)->name,
                            *t_sys_p);

            if (cmb_time() > trlp->warmup_time) {
                /* Add it to the statistics */
                cmb_dataset_add(simp->time_in_system[shpp->size], *t_sys_p);
            }

            ship_terminate(shpp);
            ship_destroy(shpp);

            /* The exit value was malloc'ed in the ship process, free it as well */
            free(t_sys_p);
        }
    }

Running a trial
^^^^^^^^^^^^^^^

Our simulation driver function ``run_trial()`` does in principle the same as in our
first tutorial: Sets up the simulated world, runs the simulation, collects the
results, and cleans up everything after itself. There are more objects involved
this time, so we will not reproduce the entire function here, just call your
attention to these two sections:

.. code-block:: c

        /* Create weather and tide processes, ensuring that weather goes first */
        sim.weather = cmb_process_create();
        cmb_process_initialize(sim.weather, "Wind", weather_proc, &ctx, 1);
        cmb_process_start(sim.weather);
        sim.tide = cmb_process_create();
        cmb_process_initialize(sim.tide, "Depth", tide_proc, &ctx, 0);
        cmb_process_start(sim.tide);

Since the calculations of tide level depends on the weather state, we give the
weather process a higher priority than the tide process. It will then always
execute first, giving the tide process guaranteed updated information rather
than possibly acting on the previous hour's data.

As an efficiency optimization, we can now also remove the ``cmb_condition_signal()``
call from the weather process, since we know that the harbormaster will be
signalled by the tide process immediately thereafter, saving one set of demand
recalculations per simulated hour.

This is where the resource guard observer/signal forwarding becomes useful:

.. code-block:: c

        /* Create the harbormaster and Davy Jones himself */
        sim.harbormaster = cmb_condition_create();
        cmb_condition_initialize(sim.harbormaster, "Harbormaster");
        cmb_resourceguard_register(&(sim.tugs->guard), &(sim.harbormaster->guard));
        for (int i = 0; i < 2; i++) {
            cmb_resourceguard_register(&(sim.berths[i]->guard), &(sim.harbormaster->guard));
        }

        sim.davyjones = cmb_condition_create();
        cmb_condition_initialize(sim.davyjones, "Davy Jones");

The harbormaster registers itself as an observer at the tugs and berths to receive
a signal whenever one is released by some other process. Otherwise, it would need
to wait until the top of the next hour when it is signaled by the weather and tide
processes before it noticed.

Building and running our new harbor simulation, we get output similar to this:

.. code-block:: none

    [ambonvik@Threadripper tutorial]$ ./tut_2_1 | more
    1.5696	Arrivals	arrival_proc (335):  Ship_000001_large started
    1.5696	Ship_000001_large	ship_proc (227):  Ship_000001_large arrives
    1.5696	Ship_000001_large	is_ready_to_dock (209):  All good for Ship_000001_large
    1.5696	Ship_000001_large	ship_proc (240):  Ship_000001_large cleared to dock, acquires berth and tugs
    2.1582	Ship_000001_large	ship_proc (253):  Ship_000001_large docked, releases tugs, unloading
    3.2860	Arrivals	arrival_proc (335):  Ship_000002_small started
    3.2860	Ship_000002_small	ship_proc (227):  Ship_000002_small arrives
    3.2860	Ship_000002_small	is_ready_to_dock (209):  All good for Ship_000002_small
    3.2860	Ship_000002_small	ship_proc (240):  Ship_000002_small cleared to dock, acquires berth and tugs
    3.9669	Ship_000002_small	ship_proc (253):  Ship_000002_small docked, releases tugs, unloading
    4.7024	Arrivals	arrival_proc (335):  Ship_000003_small started
    4.7024	Ship_000003_small	ship_proc (227):  Ship_000003_small arrives
    4.7024	Ship_000003_small	is_ready_to_dock (209):  All good for Ship_000003_small
    4.7024	Ship_000003_small	ship_proc (240):  Ship_000003_small cleared to dock, acquires berth and tugs
    5.1600	Arrivals	arrival_proc (335):  Ship_000004_small started
    5.1600	Ship_000004_small	ship_proc (227):  Ship_000004_small arrives
    5.1600	Ship_000004_small	is_ready_to_dock (209):  All good for Ship_000004_small
    5.1600	Ship_000004_small	ship_proc (240):  Ship_000004_small cleared to dock, acquires berth and tugs
    5.2328	Ship_000003_small	ship_proc (253):  Ship_000003_small docked, releases tugs, unloading
    5.7241	Arrivals	arrival_proc (335):  Ship_000005_small started
    5.7241	Ship_000005_small	ship_proc (227):  Ship_000005_small arrives
    5.7241	Ship_000005_small	is_ready_to_dock (209):  All good for Ship_000005_small
    5.7241	Ship_000005_small	ship_proc (240):  Ship_000005_small cleared to dock, acquires berth and tugs
    5.7273	Ship_000004_small	ship_proc (253):  Ship_000004_small docked, releases tugs, unloading
    6.3406	Ship_000005_small	ship_proc (253):  Ship_000005_small docked, releases tugs, unloading
    10.614	Ship_000002_small	ship_proc (260):  Ship_000002_small ready to leave, requests tugs

    [...]

    330.08	Ship_000145_small	ship_proc (227):  Ship_000145_small arrives
    330.08	Ship_000145_small	is_ready_to_dock (189):  Wind 10.491782 m/s too strong for Ship_000145_small, max 10.000000
    330.26	Arrivals	arrival_proc (335):  Ship_000146_small started
    330.26	Ship_000146_small	ship_proc (227):  Ship_000146_small arrives
    330.26	Ship_000146_small	is_ready_to_dock (189):  Wind 10.491782 m/s too strong for Ship_000146_small, max 10.000000
    330.92	Ship_000140_small	ship_proc (260):  Ship_000140_small ready to leave, requests tugs
    330.92	Ship_000140_small	is_ready_to_dock (189):  Wind 10.491782 m/s too strong for Ship_000145_small, max 10.000000
    331.00	Depth	is_ready_to_dock (189):  Wind 10.258885 m/s too strong for Ship_000145_small, max 10.000000
    331.00	Depth	is_ready_to_dock (189):  Wind 10.258885 m/s too strong for Ship_000146_small, max 10.000000
    331.39	Ship_000140_small	ship_proc (272):  Ship_000140_small left harbor, releases berth and tugs
    331.39	Ship_000140_small	is_ready_to_dock (189):  Wind 10.258885 m/s too strong for Ship_000145_small, max 10.000000
    331.39	Ship_000140_small	is_ready_to_dock (189):  Wind 10.258885 m/s too strong for Ship_000145_small, max 10.000000
    331.39	Departures	departure_proc (374):  Recycling Ship_000140_small, time in system 9.899306
    331.48	Ship_000143_small	ship_proc (260):  Ship_000143_small ready to leave, requests tugs
    331.48	Ship_000143_small	is_ready_to_dock (189):  Wind 10.258885 m/s too strong for Ship_000145_small, max 10.000000
    332.00	Depth	is_ready_to_dock (209):  All good for Ship_000145_small
    332.00	Depth	is_ready_to_dock (209):  All good for Ship_000146_small
    332.00	Ship_000145_small	is_ready_to_dock (209):  All good for Ship_000145_small
    332.00	Ship_000145_small	ship_proc (240):  Ship_000145_small cleared to dock, acquires berth and tugs

    [...]

    434.49	Ship_000189_large	is_ready_to_dock (203):  No available berth for Ship_000191_large
    434.87	Ship_000198_small	ship_proc (253):  Ship_000198_small docked, releases tugs, unloading
    434.87	Ship_000198_small	is_ready_to_dock (203):  No available berth for Ship_000191_large
    435.00	Depth	is_ready_to_dock (203):  No available berth for Ship_000191_large
    435.00	Depth	is_ready_to_dock (203):  No available berth for Ship_000193_large
    435.07	Ship_000189_large	ship_proc (272):  Ship_000189_large left harbor, releases berth and tugs
    435.07	Ship_000189_large	is_ready_to_dock (209):  All good for Ship_000191_large
    435.07	Ship_000189_large	is_ready_to_dock (209):  All good for Ship_000193_large
    435.07	Ship_000191_large	is_ready_to_dock (209):  All good for Ship_000191_large
    435.07	Ship_000191_large	ship_proc (240):  Ship_000191_large cleared to dock, acquires berth and tugs
    435.07	Ship_000193_large	is_ready_to_dock (203):  No available berth for Ship_000193_large
    435.07	Departures	departure_proc (374):  Recycling Ship_000189_large, time in system 12.530678
    435.16	Ship_000190_large	ship_proc (260):  Ship_000190_large ready to leave, requests tugs
    435.16	Ship_000190_large	is_ready_to_dock (203):  No available berth for Ship_000193_large
    435.59	Ship_000191_large	ship_proc (253):  Ship_000191_large docked, releases tugs, unloading
    435.59	Ship_000191_large	is_ready_to_dock (203):  No available berth for Ship_000193_large
    435.78	Ship_000190_large	ship_proc (272):  Ship_000190_large left harbor, releases berth and tugs
    435.78	Ship_000190_large	is_ready_to_dock (209):  All good for Ship_000193_large
    435.78	Ship_000193_large	is_ready_to_dock (209):  All good for Ship_000193_large
    435.78	Ship_000193_large	ship_proc (240):  Ship_000193_large cleared to dock, acquires berth and tugs
    435.78	Departures	departure_proc (374):  Recycling Ship_000190_large, time in system 12.268849
    436.42	Ship_000193_large	ship_proc (253):  Ship_000193_large docked, releases tugs, unloading
    436.68	Ship_000184_large	ship_proc (260):  Ship_000184_large ready to leave, requests tugs

...and so on. It looks rather promising, so we turn off the logging and rerun. Output:

.. code-block:: none

    /home/ambonvik/github/cimba/build/tutorial/tut_2_1

    System times for small ships:
    N     3278  Mean    10.81  StdDev    2.408  Variance    5.798  Skewness    1.346  Kurtosis    3.049
    --------------------------------------------------------------------------------
    ( -Infinity,      7.051)   |
    [     7.051,      7.989)   |###################=
    [     7.989,      8.927)   |##############################################=
    [     8.927,      9.865)   |##################################################
    [     9.865,      10.80)   |################################################=
    [     10.80,      11.74)   |########################################-
    [     11.74,      12.68)   |############################-
    [     12.68,      13.62)   |#####################-
    [     13.62,      14.55)   |#############-
    [     14.55,      15.49)   |#######-
    [     15.49,      16.43)   |#####-
    [     16.43,      17.37)   |#=
    [     17.37,      18.31)   |#-
    [     18.31,      19.24)   |#=
    [     19.24,      20.18)   |=
    [     20.18,      21.12)   |=
    [     21.12,      22.06)   |=
    [     22.06,      22.99)   |-
    [     22.99,      23.93)   |-
    [     23.93,      24.87)   |-
    [     24.87,      25.81)   |
    [     25.81,  Infinity )   |-
    --------------------------------------------------------------------------------

    System times for large ships:
    N     1060  Mean    17.34  StdDev    5.548  Variance    30.78  Skewness    2.024  Kurtosis    7.243
    --------------------------------------------------------------------------------
    ( -Infinity,      10.38)   |
    [     10.38,      12.67)   |###################################=
    [     12.67,      14.96)   |##################################################
    [     14.96,      17.25)   |#########################################-
    [     17.25,      19.54)   |##############################=
    [     19.54,      21.83)   |#################-
    [     21.83,      24.12)   |##############-
    [     24.12,      26.41)   |#####=
    [     26.41,      28.70)   |#####=
    [     28.70,      30.99)   |####-
    [     30.99,      33.28)   |##=
    [     33.28,      35.56)   |-
    [     35.56,      37.85)   |-
    [     37.85,      40.14)   |-
    [     40.14,      42.43)   |-
    [     42.43,      44.72)   |-
    [     44.72,      47.01)   |
    [     47.01,      49.30)   |
    [     49.30,      51.59)   |-
    [     51.59,      53.88)   |-
    [     53.88,      56.17)   |
    [     56.17,  Infinity )   |-
    --------------------------------------------------------------------------------

    Utilization of small berths:
    N     5890  Mean    3.809  StdDev    2.069  Variance    4.280  Skewness  -0.2231  Kurtosis   -1.621
    --------------------------------------------------------------------------------
    ( -Infinity,      0.000)   |
    [     0.000,      1.000)   |#####-
    [     1.000,      2.000)   |#################-
    [     2.000,      3.000)   |################################=
    [     3.000,      4.000)   |#######################################-
    [     4.000,      5.000)   |########################################-
    [     5.000,      6.000)   |##################################=
    [     6.000,  Infinity )   |##################################################
    --------------------------------------------------------------------------------

    Utilization of large berths:
    N     1766  Mean    1.797  StdDev    2.347  Variance    5.509  Skewness  -0.1321  Kurtosis   -2.636
    --------------------------------------------------------------------------------
    ( -Infinity,      0.000)   |
    [     0.000,      1.000)   |####################-
    [     1.000,      2.000)   |#######################################-
    [     2.000,      3.000)   |######################################=
    [     3.000,  Infinity )   |##################################################
    --------------------------------------------------------------------------------

    Utilization of tugs:
    N    16311  Mean   0.8651  StdDev   0.9467  Variance   0.8962  Skewness    2.449  Kurtosis    8.635
    --------------------------------------------------------------------------------
    ( -Infinity,      0.000)   |
    [     0.000,      1.000)   |##################################################
    [     1.000,      2.000)   |######################-
    [     2.000,      3.000)   |####=
    [     3.000,      4.000)   |#######=
    [     4.000,      5.000)   |###-
    [     5.000,      6.000)   |=
    [     6.000,      7.000)   |=
    [     7.000,      8.000)   |-
    [     8.000,      9.000)   |-
    [     9.000,      10.00)   |-
    [     10.00,  Infinity )   |-
    --------------------------------------------------------------------------------
    Avg time in system, small ships: 10.812688
    Avg time in system, large ships: 17.341350

You can find the code for this stage in ``tutorial/tut_2_1.c``.

Turning up the power
^^^^^^^^^^^^^^^^^^^^

We still find it fascinating to see our simulated ships and tugs scurrying about, but our client,
the Simulated Port Authority, reminds us that next year's budget is soon due and they would
prefer getting answers to their questions soon. And, by the way, could we add scenarios where
traffic increases by 10 % and 25 % above today's baseline levels?

Time to fire up our computing power.

Setting up our experiment, we believe that the factors depth, number of tugs, and number
of small and large berths are largely independent. We can probably vary one at a time
rather than setting up a factorial experiment (which may still be computationally more
efficient to do). To ensure that the SPA also has numbers it can use beyond next
year's budget, we try five levels of each parameter, dredging in steps of 0.5 meters
and adding tugs and berths in steps of one. We again run ten replications of each
parameter set. This gives us 4 * 5 * 3 = 60 parameter combinations and 60 * 10 = 600 trials.
We will run each trial for 10 years of simulated time, i.e. 10 * 365 * 24 = 87360 time units,
allowing 30 days' warmup time before we start collecting data.

Writing the ``main()`` function is straightforward, albeit somewhat tedious. It does
the same as in the previous tutorial: Sets up the experiment as an array of trials,
executes it in parallel on the available CPU cores, assembles the output as a data
file, and plots it in a separate gnuplot window.

We compile and run, and 4.1 seconds later, this chart appears, showing our 60
parameter combinations, the average time in the system for small and large ships
under each set of parameters, and tight 95 % confidence intervals based
on our 10 replications of each parameter combination:

.. image:: ../images/tut_2_2.png

We see that we can tell our client, the SPA, that they have enough tugs and do
not need to dredge, but that they really should consider building one more large
berth, especially if traffic is expected to increase. However, building more than
one does not make much sense even at the highest traffic scenario. The SPA should
rather consider building another one or two small berths next.

This concludes our second tutorial. We have introduced ``cmb_resource``,
``cmb_resourcepool``, and the very powerful ``cmb_condition`` allowing processes
to wait for arbitrary combinations of conditions. Along the way, we demonstrated
that user applications can build derived classes from Cimba parent classes using
single inheritance. For example, the ``ship`` class in this tutorial was derived
from a ``cmb_process`` which in turn is derived from a ``cmi_coroutine``.

Our third simulation - When the Cat is Away...
----------------------------------------------

In our third tutorial, we will introduce additional process interactions where
the active process is acting directly on some other process. We will
demonstrate these through a somewhat cartoonish example. First, some necessary
background.

Interrupts, preempts, and return values
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

We have already explained that Cimba processes (and the coroutines they are
derived from) execute atomically until they explicitly yield control. These
yield (and resume) points are hidden inside functions like ``cmb_process_hold()``
or ``cmb_resource_acquire``. Inside the call, control may (or may not) be
passed to some other process. The call will only return when control is transferred
back to this process. To the calling process just sitting on its own stack, it looks very
simple, but a lot of things may be happening elsewhere in the meantime.

A yielded process does not have any guarantees for what may be happening to it
before it resumes control. Other processes may act on this process, perhaps
stopping it outright, waking it up early, or snatching a resource away from it.

To be able to tell the difference, functions like ``cmb_process_hold()`` and
``cmb_resource_acquire()`` have an integer return value, an ``int64_t``. We have
quietly ignored the return values in our earlier examples, but they are an
important signalling mechanism between Cimba processes.

Cimba has reserved a few possible return values for itself. Most importantly,
``CMB_PROCESS_SUCCESS`` (numeric value 0) means a normal and successful return.
For example, if ``cmb_process_hold()`` returns ``CMB_PROCESS_SUCCESS``, the calling
process knows that it was woken up at the scheduled time without any shenanigans.

The second most important value is ``CMB_PROCESS_PREEMPTED``. That means that a
higher priority process just forcibly took away some resource held by this
process. There are also ``CMB_PROCESS_INTERRUPTED``, ``CMB_PROCESS_STOPPED``,
and ``CMB_PROCESS_CANCELLED``. These are defined as small negative values,
leaving an enormous number of available signal values to application-defined
meanings. In particular, all positive integers are available to the application
for coding various interrupt signals between processes.

These signal values create a rich set of direct process interactions. As an
example, suppose some process currently holds 10 units from some resource pool.
It then calls ``cmb_resourcepool_acquire()`` requesting 10 more units. At that
moment, only 5 are available. The process takes these 5 and adds itself to the
priority queue maintained by the resource guard, asking to be woken whenever some
more is available, intending to return from its acquire call only when all 10
units have been collected.

There are now three different outcomes for the acquire call:

1. All goes as expected, 5 more units eventually become available, the process
   takes them, and returns ``CMB_PROCESS_SUCCESS``. It now holds 20 units.

2. Some higher priority process calls ``cmb_resourcepool_preempt()`` and this
   process is targeted. The higher priority process takes *all* units held by
   the victim process. Its acquire call returns ``CMB_PROCESS_PREEMPTED``. It
   now holds 0 units.

3. Some other process calls ``cmb_process_interrupt()`` on this process. It
   excuses itself from the resource guard priority queue and returns whatever
   signal value was given to ``cmb_process_interrupt()``, perhaps
   ``CMB_PROCESS_INTERRUPTED`` or some other value that has an
   application-defined meaning. It unwinds the 5 units it collected during the
   call and returns holding the same amount as it held before
   calling ``cmb_resourcepool_acquire()``, 10 units.

Preempt calls can themselves be preempted by higher priority processes or
interrupted in the same way as acquire calls if the preempt was not immediately
fulfilled and the process had to wait at the resource guard. Once there, it is
fair game for preempts and interrupts.

Another potential complexity: Suppose a process holds more than one type of
resource, for example:

.. code-block:: c

    cmb_resource_acquire(rp);
    cmb_resourcepool_acquire(rsp1, 10);
    cmb_resourcepool_acquire(rsp2, 15);

    int64_t signal = cmb_process_hold(100,0);

    if (signal == CMB_PROCESS_PREEMPTED) {
        /* ??? */
    }

In cases like this, the functions ``cmb_resource_held_by_process`` and
``cmb_resourcepool_held_by_process()`` with a pointer to itself as the second
argument can be useful to figure out which resource was preempted. If the caller
does not have a pointer to itself handy (it is always the first argument to the
process function), it can get one by calling ``cmb_process_current()``,
returning a pointer to the currently executing process, i.e. the caller.

Buffers and object queues, interrupted
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The semantics of buffers and object queues are different from the resources and
resource pools. A process can acquire and hold a resource, making it unavailable
for other processes until it is released. Preempting it naturally means taking
the resource away from the process because someone else needs it more, right now.

Buffers and their cousins are not like that. Once something is put in, other
processes can get it and consume it immediately. Preempting a put or get operation
does not have any obvious meaning. If a buffer is empty, a process get call is waiting
at the resource guard, and a higher priority process wants to get some first, it
just calls ``cmb_buffer_get()`` and goes first in the priority queue.

However, waiting puts and gets can still be interrupted. For the ``cmb_objectqueue``
and ``cmb_priorityqueue``, it is very simple. If the respective ``_put()`` or ``_get()``
call returned ``CMB_PROCESS_SUCCESS`` the object was successfully added to the queue.
If it returned anything else, it was not.

The ``cmb_buffer`` is similarly intuitive. Recall from our first tutorial that
the amount argument is given as a pointer to a variable, not as a value. As
the put and get calls get underway, the value at this location gets updated to
reflect the progress. If interrupted, this value indicates how much was placed
or obtained. The call returns at this point with no attempt to roll back to the
state at the beginning of the call. If successful, the put call will have a
zero value in this location, the get call will have the requested amount. If not,
there will be some other value between zero and the requested amount.

...the Mice will Play
^^^^^^^^^^^^^^^^^^^^^

It is again probably easier to demonstrate with code than explain in computer
sciencey terms how all this works.

On a table, we have some pieces of cheese in a pile. There are several mice
trying to collect the cheese and hold it for a while. Each mouse can carry
different numbers of cheese cubes. They tend to drop it again quite fast,
inefficient hoarders as they are. Unfortunately, there are also some
rats, bigger and stronger than the mice. The rats will preempt the cheese from
the mice, but only if the rat has higher priority. Otherwise, the rat will
politely wait its turn. There is also a cat. It sleeps a lot, but when awake,
it will select random rodents and interrupt whatever it is doing.

Since we do not plan to run any statistics here, we simplify the context struct
to just the simulation struct. We can then write something like:

.. code-block:: c

    /* The busy life of a mouse */
    void *mousefunc(struct cmb_process *me, void *ctx)
    {
        cmb_assert_release(me != NULL);
        cmb_assert_release(ctx != NULL);

        const struct simulation *simp = ctx;
        struct cmb_resourcepool *sp = simp->cheese;
        uint64_t amount_held = 0u;

        while (true) {
            /* Verify that the amount matches our own calculation */
            cmb_assert_debug(amount_held == cmb_resourcepool_held_by_process(sp, me));

            /* Decide on a random amount to get next time and set a random priority */
            const uint64_t amount_req = cmb_random_dice(1, 5);
            const int64_t pri = cmb_random_dice(-10, 10);
            cmb_process_set_priority(me, pri);
            cmb_logger_user(stdout, USERFLAG1, "Acquiring %" PRIu64, amount_req);
            int64_t sig = cmb_resourcepool_acquire(sp, amount_req);
            if (sig == CMB_PROCESS_SUCCESS) {
                /* Acquire returned successfully */
                amount_held += amount_req;
                cmb_logger_user(stdout, USERFLAG1, "Success, new amount held: %" PRIu64, amount_held);
            }
            else if (sig == CMB_PROCESS_PREEMPTED) {
                /* The acquire call did not end well */
                cmb_logger_user(stdout, USERFLAG1, "Preempted during acquire, all my %s is gone",
                                cmb_resourcepool_name(sp));
                amount_held = 0u;
            }
            else {
                /* Interrupted, but we still have the same amount as before */
                cmb_logger_user(stdout, USERFLAG1, "Interrupted by signal %" PRIi64, sig);
            }

            /* Hold on to it for a while */
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            if (sig == CMB_PROCESS_SUCCESS) {
                /* We still have it */
                cmb_logger_user(stdout, USERFLAG1, "Hold returned successfully");
            }
            else if (sig == CMB_PROCESS_PREEMPTED) {
                /* Somebody snatched it all away from us */
                cmb_logger_user(stdout, USERFLAG1, "Someone stole all my %s from me!",
                                cmb_resourcepool_name(sp));
                amount_held = 0u;
            }
            else {
                /* Interrupted while holding. Still have the cheese, though */
                cmb_logger_user(stdout, USERFLAG1, "Interrupted by signal %" PRIi64, sig);
            }

            /* Drop some amount */
            if (amount_held > 1u) {
                const uint64_t amount_rel = cmb_random_dice(1, (long)amount_held);
                cmb_logger_user(stdout, USERFLAG1, "Holds %" PRIu64 ", releasing %" PRIu64,
                                amount_held, amount_rel);
                cmb_resourcepool_release(sp, amount_rel);
                amount_held -= amount_rel;
            }

            /* Hang on a moment before trying again */
            cmb_logger_user(stdout, USERFLAG1, "Holding, amount held: %" PRIu64, amount_held);
            sig = cmb_process_hold(cmb_random_exponential(1.0));
            if (sig == CMB_PROCESS_PREEMPTED) {
                cmb_logger_user(stdout, USERFLAG1,
                                "Someone stole the rest of my %s, signal %" PRIi64,
                                cmb_resourcepool_name(sp), sig);
                amount_held = 0u;
           }
        }
    }

The rats are pretty much the same as the mice, just a bit hungrier and stronger
(i.e. assigning themselves somewhat higher priorities), and using
``cmb_resourcepool_preempt()`` instead of ``_acquire()``:

.. code-block:: c

    /* Decide on a random amount to get next time and set a random priority */
    const uint64_t amount_req = cmb_random_dice(3, 10);
    const int64_t pri = cmb_random_dice(-5, 15);
    cmb_process_set_priority(me, pri);
    cmb_logger_user(stdout, USERFLAG1, "Preempting %" PRIu64, amount_req);
    int64_t sig = cmb_resourcepool_preempt(sp, amount_req);

The cats, on the other hand, are never interrupted and just ignore return values:

.. code-block:: c

    void *catfunc(struct cmb_process *me, void *ctx)
    {
        cmb_unused(me);
        cmb_assert_release(ctx != NULL);

        struct simulation *simp = ctx;
        struct cmb_process **cpp = (struct cmb_process **)simp;
        const long num = NUM_MICE + NUM_RATS;

        while (true) {
            /* Nobody interrupts a sleeping cat, disregard return value */
            cmb_logger_user(stdout, USERFLAG1, "Zzzzz...");
            (void)cmb_process_hold(cmb_random_exponential(5.0));
            do {
                cmb_logger_user(stdout, USERFLAG1, "Awake, looking for rodents");
                (void)cmb_process_hold(cmb_random_exponential(1.0));
                struct cmb_process *tgt = cpp[cmb_random_dice(0, num - 1)];
                cmb_logger_user(stdout, USERFLAG1, "Chasing %s", cmb_process_name(tgt));

                /* Send it a random interrupt signal */
                const int64_t sig = (cmb_random_flip()) ?
                                     CMB_PROCESS_INTERRUPTED :
                                     cmb_random_dice(10, 100);
                cmb_process_interrupt(tgt, sig, 0);

                /* Flip a coin to decide whether to go back to sleep */
            } while (cmb_random_flip());
        }
    }

We compile and run, and get output similar to this:

.. code-block:: none

    [ambonvik@Threadripper cimba]$ build/tutorial/tut_3_1 | more
    Create a pile of 20 cheese cubes
    Create 5 mice to compete for the cheese
    Create 2 rats trying to preempt the cheese
    Create 1 cats chasing all the rodents
    Schedule end event
    Execute simulation...
        0.0000	Cat_1	catfunc (218):  Zzzzz...
        0.0000	Rat_2	ratfunc (151):  Preempting 4
        0.0000	Rat_2	ratfunc (156):  Success, new amount held: 4
        0.0000	Mouse_4	mousefunc (77):  Acquiring 1
        0.0000	Mouse_4	mousefunc (82):  Success, new amount held: 1
        0.0000	Rat_1	ratfunc (151):  Preempting 8
        0.0000	Rat_1	ratfunc (156):  Success, new amount held: 8
        0.0000	Mouse_1	mousefunc (77):  Acquiring 5
        0.0000	Mouse_1	mousefunc (82):  Success, new amount held: 5
        0.0000	Mouse_3	mousefunc (77):  Acquiring 2
        0.0000	Mouse_3	mousefunc (82):  Success, new amount held: 2
        0.0000	Mouse_5	mousefunc (77):  Acquiring 1
        0.0000	Mouse_2	mousefunc (77):  Acquiring 3
       0.23852	Mouse_1	mousefunc (99):  Hold returned normally
       0.23852	Mouse_1	mousefunc (115):  Holds 5, releasing 5
       0.23852	Mouse_1	mousefunc (122):  Holding, amount held: 0
       0.23852	Mouse_5	mousefunc (82):  Success, new amount held: 1
       0.23852	Mouse_2	mousefunc (82):  Success, new amount held: 3
       0.30029	Cat_1	catfunc (221):  Awake, looking for rodents
       0.46399	Mouse_2	mousefunc (99):  Hold returned normally
       0.46399	Mouse_2	mousefunc (115):  Holds 3, releasing 1
       0.46399	Mouse_2	mousefunc (122):  Holding, amount held: 2
       0.56088	Mouse_1	mousefunc (77):  Acquiring 1
       0.56088	Mouse_1	mousefunc (82):  Success, new amount held: 1
       0.58910	Mouse_4	mousefunc (99):  Hold returned normally
       0.58910	Mouse_4	mousefunc (122):  Holding, amount held: 1
       0.73649	Mouse_5	mousefunc (99):  Hold returned normally
       0.73649	Mouse_5	mousefunc (122):  Holding, amount held: 1
       0.74171	Mouse_3	mousefunc (99):  Hold returned normally
       0.74171	Mouse_3	mousefunc (115):  Holds 2, releasing 2
       0.74171	Mouse_3	mousefunc (122):  Holding, amount held: 0
       0.83936	Mouse_3	mousefunc (77):  Acquiring 4
       0.89350	Mouse_5	mousefunc (77):  Acquiring 5
        1.3408	Rat_2	ratfunc (173):  Hold returned normally
        1.3408	Rat_2	ratfunc (189):  Holds 4, releasing 1
        1.3408	Rat_2	ratfunc (196):  Holding, amount held: 3
        1.3408	Mouse_3	mousefunc (82):  Success, new amount held: 4
        1.4394	Mouse_4	mousefunc (77):  Acquiring 5
        1.8889	Mouse_2	mousefunc (77):  Acquiring 1
        1.8992	Mouse_3	mousefunc (99):  Hold returned normally
        1.8992	Mouse_3	mousefunc (115):  Holds 4, releasing 4
        1.8992	Mouse_3	mousefunc (122):  Holding, amount held: 0
        1.9260	Mouse_1	mousefunc (99):  Hold returned normally
        1.9260	Mouse_1	mousefunc (122):  Holding, amount held: 1
        2.5697	Mouse_3	mousefunc (77):  Acquiring 3
        3.1025	Mouse_1	mousefunc (77):  Acquiring 4
        3.7215	Rat_2	ratfunc (151):  Preempting 6
        3.7215	Mouse_4	mousefunc (86):  Preempted during acquire, all my Cheese is gone
        3.7215	Mouse_1	mousefunc (86):  Preempted during acquire, all my Cheese is gone
        4.2186	Mouse_1	mousefunc (99):  Hold returned normally
        4.2186	Mouse_1	mousefunc (122):  Holding, amount held: 0
        4.7152	Mouse_1	mousefunc (77):  Acquiring 5
        4.8393	Cat_1	catfunc (224):  Chasing Mouse_1
        4.8393	Cat_1	catfunc (221):  Awake, looking for rodents
        4.8393	Mouse_1	mousefunc (92):  Interrupted by signal -2
        5.3060	Cat_1	catfunc (224):  Chasing Mouse_4
        5.3060	Cat_1	catfunc (221):  Awake, looking for rodents
        5.3060	Mouse_4	mousefunc (109):  Interrupted by signal 20
        5.3060	Mouse_4	mousefunc (122):  Holding, amount held: 0
        5.8149	Mouse_1	mousefunc (99):  Hold returned normally
        5.8149	Mouse_1	mousefunc (122):  Holding, amount held: 0
        6.0788	Mouse_1	mousefunc (77):  Acquiring 4
        6.1803	Rat_1	ratfunc (173):  Hold returned normally
        6.1803	Rat_1	ratfunc (189):  Holds 8, releasing 3
        6.1803	Rat_1	ratfunc (196):  Holding, amount held: 5


...and so on. The interactions can get rather intricate, but hopefully intuitive:
A ``cmb_resourepool_preempt()`` call will start from the lowest priority victim
process and take *all* of its resource, but only if the victim has strictly lower
priority than the caller. If the requested amount is not satisfied from the first
victim, it will continue to the next lowest priority victim. If some amount is
left over after taking everything the victim held, the resource guard is signalled
to evaluate what process gets the remainder. If no potential victims with strictly
lower priority than the caller process exists, the caller will join the priority
queue and wait in line for some amount to become available.

Cimba does not try to be "fair" or "optimal" in its resource allocation, just
efficient and predictable. If the application needs different allocation rules,
it can either adjust process priorities dynamically or create a derived class
with a custom demand function.

Real world uses
^^^^^^^^^^^^^^^

The example above was originally written as part of the Cimba unit test suite,
to ensure that the library tracking of how many units each process holds from
the resource pool always matches the expected values calculated here. Hence all
the ``cmb_assert_debug(amount_held == cmb_resourcepool_held_by_process(sp, me));``
statements. We wanted to make very sure that this is correct in all possible
sequences of events, hence this frantic stress test with preemptions and
interruptions galore.

The preempt and interrupt mechanisms will be important in a range of real-world
modeling applications, ranging from hospital emergency room triage operations to
manufacturing job shops and machine breakdown processes. Together with hold,
acquire/release, put/get, the condition variables, and the ability for processes
to wait for specific events and for other processes to finish, Cimba provides a
comprehensive set of process interactions.

Building, validating, and parallelizing the simulation will follow the same
pattern as in our two first tutorials, so we will not repeat that here.

This completes our third tutorial, demonstrating how to use direct process
interactions like ``cmb_process_interrupt()`` and ``cmb_resourcepool_preempt()``.
We have mentioned, but not demonstrated ``cmb_process_wait_process()``
and ``cmb_process_wait_event()``. We encourage you to look up these in the
API reference documentation next. Any remaining question may best be answered by
reading the relevant source code.

We hope you will find Cimba useful for your own modeling needs.
