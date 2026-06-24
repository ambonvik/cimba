![large logo](images/logo_large.jpg)

## A multithreaded discrete event simulation library in C

### What is it?
A fast discrete event simulation library written in C and assembly with POSIX pthreads 
for running multiple trials in parallel on multi-core CPUs. Simulated processes are 
implemented as stackful coroutines ("fibers") inside the pthreads. Compute-intensive 
model physics can be implemented as massively parallel CUDA (or other GPGPU) functions 
inside the coroutines. 
As far as we know, there is no other open source library that can provide these features.

Implementation status:
* x86-64: Stable for Linux and Windows 10 + 11.
* Apple Silicon: Planned
* ARM: Planned

Cimba models run 40-50 times faster than SimPy equivalents. The chart below shows the 
number of simulated events processed per second of wall clock time on a simple M/M/1 
queue implemented in SimPy and Cimba. _Cimba runs twice as fast (32M events/sec) on a 
single CPU core as SimPy does when using all 64 cores (16M events/sec)._

![Speed_test_AMD_3970x.png](images/Speed_test_AMD_3970x.png)

### Why should I use it?
It is fast, powerful, reliable, and free.

* *Fast*: The speed from multithreaded parallel execution translates to high
  resolution in your simulation modeling. You can run hundreds of replications
  and parameter variations in just a few seconds, generating tight confidence
  intervals in your experiments and a high density of data points along parameter
  variations.

  In the benchmark shown above, Cimba reduces the run time by 98 % compared to the 
  same model in SimPy using all CPU cores. This translates into doing your simulation 
  experiments in seconds instead of minutes, or in minutes instead of hours.

  If you need even more speed, CUDA kernels can be used for massively parallel 
  computation inside each simulated process, e.g., for AI-enabled agents or for 
  intricate physics calculations. In 
  [one of our tutorials](https://cimba.readthedocs.io/en/latest/tutorial.html#adding-cuda-gpu-power-for-simulation-physics), 
  we demonstrate how to combine multithreaded trials with CUDA functions running on 
  multiple GPUs.

* *Powerful*: Cimba provides a comprehensive toolkit for discrete event simulation:

  * Processes implemented as asymmetric stackful coroutines. A simulated process can
    yield and resume control from any level of a function call stack, allowing 
    well-structured coding of arbitrarily large simulation models. 
    A simulated process can run in an infinite loop 
    or act as a one-shot customer passing through the system, being both an active 
    agent and a passive object as needed.

  * Pre-packaged process interaction mechanisms like resources, resource pools, buffers, 
    object queues, priority queues, and timeouts. Cimba also provides condition variables 
    where your simulated processes can wait for arbitrarily complex conditions to become 
    true – anything you can express as a function returning a binary true or false result.

  * A wide range of fast, high-quality random number generators, both
    of academically important and more empirically oriented types. Important 
    distributions like normal and exponential are implemented by state-of-the-art 
    ziggurat rejection sampling for speed and accuracy.
  
  * Integrated logging and data collection features that make it easy
    to get a model running and understand what is happening inside it, including 
    custom asserts to pinpoint sources of errors.
  
  * An entire experiment design can be expressed as an array of trials with various 
    parameters, all trials executed in parallel, and statistics calculated, all in the 
    same program.

  * As a C library, Cimba allows easy integration with other libraries and programs. You 
    could call CUDA routines to calculate model physics or to enhance your simulation 
    models with GPU-powered agentic behavior.
    You could even call the Cimba simulation engine from other programming languages, 
    since the C calling convention is standard and well-documented. 

* *Reliable*: Cimba is well-engineered open source. There is no mystery to the results you get.

  * The code is written with liberal use of assertions 
    to enforce preconditions, invariants, and postconditions in each function. The 
    assertions act as self-enforcing documentation on expected inputs to and outputs from 
    the Cimba functions. About 13 % of all code lines in the Cimba library are 
    assertions, a very high density.
  
  * There are unit tests for each module. Running the unit test battery in debug mode (all
    assertions active) verifies the correct operation in great detail. You can do that 
    by the one-liner ``meson test -C build`` from the terminal command line. 
  
  * Cimba is compatible with sanitizers for undefined behavior (UBSan), memory address safety 
    (ASan), and thread safety (TSan). These sanitizers are executed automatically as 
    GitHub runners on every push to the repository as public verification of our reliability 
    claim, right here: https://github.com/ambonvik/cimba/actions

  * The code is routinely reviewed by the latest and greatest AI tools as they become 
    available, most recently Anthropic Claude Opus 4.8 (May 2026) and Claude Fable 5 
    (June 2026). Any bugs identified by these reviews are fixed and a follow-up review 
    done. The latest reviews can be found here: https://github.com/ambonvik/cimba/tree/main/code_reviews

* *Free*: Cimba should fit well into the budget of most research groups.

### What can I use Cimba for?
It is a general-purpose discrete event simulation library, in the spirit of a
21st century Simula67 descendant. You can use it to model, e.g.
* computer networks,
* transportation networks, 
* operating system task scheduling,
* manufacturing systems and job shops,
* military command and control systems,
* hospital and emergency room patient flows,
* queuing systems like bank tellers and store checkouts,
* urban systems like public transport and garbage collection,
* and quite a few more application domains of similar kinds, where overall system 
  complexity arises from interactions between relatively simple components.

If you look under the hood, you will also find additional reusable internal components.
Cimba contains stackful coroutines doing their own thing on thread-safe cactus stacks. 
There are fast memory pool allocators for generic small objects and hash-heaps combining 
a binary heap and an open addressing hash map using Fibonacci hashing. Although not part 
of the public Cimba API, these components can also be used in your model if needed.

### What does the code look like?
It is C code. As an illustration, this is the entire code for [our multithreaded M/M/1 
benchmark](https://github.com/ambonvik/cimba/tree/main/benchmark) mentioned above:

```
    #include <inttypes.h>
    #include <stdio.h>
    #include <stdint.h>
    
    #include <cimba.h>
    
    #define NUM_OBJECTS 1000000u
    #define ARRIVAL_RATE 0.9
    #define SERVICE_RATE 1.0
    #define NUM_TRIALS 100
    
    CMB_THREAD_LOCAL struct cmi_mempool objectpool = CMI_MEMPOOL_STATIC_INIT(8u, 512u);
    
    struct simulation {
        struct cmb_process *arrival;
        struct cmb_process *service;
        struct cmb_objectqueue *queue;
    };
    
    struct trial {
        double arr_mean;
        double srv_mean;
        uint64_t obj_cnt;
        double sum_wait;
        double avg_wait;
    };
    
    struct context {
        struct simulation *sim;
        struct trial *trl;
    };
    
    void *arrivalfunc(struct cmb_process *me, void *vctx)
    {
        cmb_unused(me);
        const struct context *ctx = vctx;
        struct cmb_objectqueue *qp = ctx->sim->queue;
        const double mean_hld = ctx->trl->arr_mean;
        for (uint64_t ui = 0; ui < NUM_OBJECTS; ui++) {
            const double t_hld = cmb_random_exponential(mean_hld);
            cmb_process_hold(t_hld);
            void *object = cmi_mempool_alloc(&objectpool);
            double *dblp = object;
            *dblp = cmb_time();
            cmb_objectqueue_put(qp, object);
        }
    
        return NULL;
    }
    
    void *servicefunc(struct cmb_process *me, void *vctx)
    {
        cmb_unused(me);
        const struct context *ctx = vctx;
        struct cmb_objectqueue *qp = ctx->sim->queue;
        const double mean_srv = ctx->trl->srv_mean;
        uint64_t *cnt = &(ctx->trl->obj_cnt);
        double *sum = &(ctx->trl->sum_wait);
        while (true) {
            void *object = NULL;
            cmb_objectqueue_get(qp, &object);
            const double *dblp = object;
            const double t_srv = cmb_random_exponential(mean_srv);
            cmb_process_hold(t_srv);
            const double t_sys = cmb_time() - *dblp;
            *sum += t_sys;
            *cnt += 1u;
            cmi_mempool_free(&objectpool, object);
        }
    }
    
    void run_trial(void *vtrl)
    {
        struct trial *trl = vtrl;
    
        cmb_logger_flags_off(CMB_LOGGER_INFO);
        cmb_random_initialize(cmb_random_hwseed());
        cmb_event_queue_initialize(0.0);
        struct context *ctx = malloc(sizeof(*ctx));
        ctx->trl = trl;
        struct simulation *sim = malloc(sizeof(*sim));
        ctx->sim = sim;
    
        sim->queue = cmb_objectqueue_create();
        cmb_objectqueue_initialize(sim->queue, "Queue", CMB_UNLIMITED);
    
        sim->arrival = cmb_process_create();
        cmb_process_initialize(sim->arrival, "Arrival", arrivalfunc, ctx, 0);
        cmb_process_start(sim->arrival);
        sim->service = cmb_process_create();
        cmb_process_initialize(sim->service, "Service", servicefunc, ctx, 0);
        cmb_process_start(sim->service);
    
        cmb_event_queue_execute();
    
        cmb_process_stop(sim->service, NULL);
        cmb_process_terminate(sim->arrival);
        cmb_process_terminate(sim->service);
        cmb_process_destroy(sim->arrival);
        cmb_process_destroy(sim->service);
    
        cmb_objectqueue_destroy(sim->queue);
        cmb_event_queue_terminate();
        free(sim);
        free(ctx);
    }
    
    int main(void)
    {
        struct trial *experiment = calloc(NUM_TRIALS, sizeof(*experiment));
        for (unsigned ui = 0; ui < NUM_TRIALS; ui++) {
            struct trial *trl = &experiment[ui];
            trl->arr_mean = 1.0 / ARRIVAL_RATE;
            trl->srv_mean = 1.0 / SERVICE_RATE;
            trl->obj_cnt = 0u;
            trl->sum_wait = 0.0;
        }
    
        cimba_run_experiment(experiment,
                             NUM_TRIALS,
                             sizeof(*experiment),
                             run_trial);
    
        struct cmb_datasummary summary;
        cmb_datasummary_initialize(&summary);
        for (unsigned ui = 0; ui < NUM_TRIALS; ui++) {
            const double avg_tsys = experiment[ui].sum_wait / (double)(experiment[ui].obj_cnt);
            cmb_datasummary_add(&summary, avg_tsys);
        }
    
        const unsigned un = cmb_datasummary_count(&summary);
        if (un > 1) {
            const double mean_tsys = cmb_datasummary_mean(&summary);
            const double sdev_tsys = cmb_datasummary_stddev(&summary);
            const double serr_tsys = sdev_tsys / sqrt((double)un);
            const double ci_w = 1.96 * serr_tsys;
            const double ci_l = mean_tsys - ci_w;
            const double ci_u = mean_tsys + ci_w;
    
            printf("Average system time %f (n %u, conf.int. %f - %f, expected %f)\n",
                   mean_tsys, un, ci_l, ci_u, 1.0 / (SERVICE_RATE - ARRIVAL_RATE));
    
            return 0;
        }
    }

```
See [our tutorial](https://cimba.readthedocs.io/en/latest/tutorial.html) at ReadTheDocs for more usage examples.


### So, what can I use all that speed for?
As shown above, it is some 45 times faster than SimPy in a relevant benchmark. It means 
getting your results almost immediately rather than after a "go brew a pot of coffee" 
delay breaking your line of thought.

If you can run, say, 10 replications with SimPy within a certain budget for time and 
computing resources, you can run 450 with Cimba. That will tighten the confidence 
intervals in your results by a factor of about 8.

For another illustration of how to benefit from the sheer speed, the experiment in 
[test_cimba.c](test/test_cimba.c)
simulates an M/G/1 queue at four different levels of 
service process variability. For each variability level, it tries 
five system utilization levels. There are ten replications for each parameter 
combination, in total 4 * 5 * 10 = 200 trials. Each trial lasts for one million 
time units, where the average service time always is 1.0 time units. 

This entire simulation runs in *about 1.5 seconds* on an AMD Threadripper 3970X with 
Arch Linux and produces the chart below.

![M/G/1 queue](images/MG1%20example.png)

Or, for a more "real" example, see 
[our tutorial 5](https://cimba.readthedocs.io/en/latest/tutorial.html#adding-cuda-gpu-power-for-simulation-physics).
Using the same 64-core Threadripper and dual RTX 3090 GPUs, it runs 300 trials of an AWACS
scenario with detailed three-dimensional
physics in 78 seconds. Each trial is a six-hour simulation of a thousand target
processes (coroutines) and one sensor process (coroutine) on a 1000 x 1000 nm
synthetic terrain with one arcsecond resolution. The sensor process models a scanning S-band 
surveillance radar including 
line-of-sight geometry, terrain masking, constant-gamma clutter with CA-CFAR detection, 
and specular multipath. The model uses radar dwell itervals (time steps) of 0.04 seconds.

The screen shot below shows one frame from a simulation of this simulation. The size of
each target is its current radar cross-section, the color is the current detection status.
The vectors on the sphere representing the AWACS indicate the current direction of the
platform and the current direction of the radar lobe. The visualization is done in 
ParaView.

![AWACS racetrack](images/tut_5_1c.png)

Cimba is able to harness all the computing power available in modern computer 
architectures for your simulation purposes, whatever they are.

### What do you mean by "well engineered"?
Discrete event simulation fits well with an object-oriented paradigm. That is
why object-oriented programming was invented in the first place for Simula67.
Since OOP is not directly enforced in plain C, we provide the object-oriented
characteristics (such as encapsulation, inheritance, polymorphism, and abstraction) 
in the Cimba software design instead. (See 
the [ReadTheDocs explanation](https://cimba.readthedocs.io/en/latest/background.html#object-oriented-programming-in-c-and-assembly)
for more details.)

The simulated processes are stackful coroutines on their own call stacks, allowing the 
processes to store their state at arbitrary points and resume execution from there 
later with minimal overhead. The context-switching code is hand-coded in assembly for 
each platform. (You 
can find [more details here](https://cimba.readthedocs.io/en/latest/background.html#coroutines-revisited).)

![Stackful coroutines](images/stack_1.png)

The stackful coroutines for simulated processes are combined with a higher
level of concurrency in the Posix pthreads managing the trials and replications in an
experiment design, and with a lower level of massive parallelism in GPGPU-based
physics calculations. These three layers of concurrency have clearly separated
semantics in the model code.

The C code is liberally sprinkled with `assert` statements testing for preconditions,
invariants, and postconditions wherever possible, applying 
[Design by Contract](https://en.wikipedia.org/wiki/Design_by_contract) 
principles for high reliability. The Cimba library contains 958 asserts in 7132 lines of 
C code, for a very high assert density of 13.4 %. These are custom-written 
assert macros that will report 
what trial, what process, the simulated time, the function and line number, and even the 
random number seed used, if anything should go wrong. All time-consuming invariants and 
postconditions are debug asserts, while the release asserts mostly check preconditions 
like function argument validity. Turning off the debug asserts doubles the speed of your
model when you are ready for it, while turning off the release asserts as well gives 
a small incremental improvement. (Again, 
[more explanation here](https://cimba.readthedocs.io/en/latest/background.html#error-handling-the-loud-crashing-noise).)

Extensive unit testing of each module ensures that
all lower-level functionality works as expected before moving on to higher levels. 
You will find the test files corresponding to each code module in the [test](./test) 
directory.

Moreover, Cimba supports sanitizer tools like Address Sanitizer, Undefined Behavior
Sanitizer, Thread Sanitizer, and the CUDA Compute Sanitizer. ASan, UBSan, and TSan are
run automatically on each push to the GitHub repo. The details can be found in the 
GitHub repo, including the results of successive test runs.

But do read the [LICENSE](LICENSE). We are not giving any warranties here.

### Object-oriented? In C and assembly? Why not just use C++?
Long story made short: C++ exception handling is not very friendly to the stackful 
coroutines we need in Cimba. The stackless coroutines in C++ are not the coroutines 
that we are looking for.

C++ has also become a large and feature-rich language, where it will be
hard to ensure compatibility with every possible combination of features.

Hence (like the Linux kernel), we chose the simpler platform for speed, clarity,
and reliability. If you need to call Cimba from some other language, the C calling
convention is well-known and well-documented.

### Version 3.0.0, you say. Why haven't I heard about Cimba before?
Because it was not made public before. What retrospectively can be called Cimba 1.0
was implemented in K&R C at MIT in the early 1990's, followed by a parallelized
version 2.0 in ANSI C and Perl around 1995–96. The present version written in 
C17 with POSIX pthreads is the third major rebuild, and the first public version.

### You had me at "free." How do I get my hands on Cimba?
It is right here. You clone the repository, build, and install it. You
will need a C compiler and the Meson build manager. On Linux, you can use GCC 
or Clang, while the recommended approach on Windows is MinGW with its GCC 
compiler. For convenience, we recommend the CLion integrated development environment 
with GCC, Meson, and Ninja built-in support on both Linux and Windows.

You will find the installation guide here: https://cimba.readthedocs.io/en/latest/installation.html
