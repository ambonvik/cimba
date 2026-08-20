## Cimba change log

Note: Version numbers are MAJOR.MINOR.PATCH, but we do *not* promise to follow Semantic 
Versioning in the strictest sense. There may be minor breaking changes in some obscure 
corner without this warranting a new major release. We will summarize new features, 
changes, and bug fixes below. For complete details, see the git commit history.

### 2026-xx-xx: 3.0.0 Release Candidate 1
* Possibly breaking change: Tightened enforcement of the Create - Initialize -
  Terminate - Destroy object lifecycle, necessary for proper memory recovery in error
  handling for multithreaded trials.
* Automatic deallocation of Cimba objects following an abandoned trial in a 
  multithreaded experiment.
* Deprecated long forms `cmb_resource_held_by_process()` (now just `cmb_resource_held()`)
  and `cmb_resourcepool_held_by_process()` (now `cmb_resourcepool_held()`), since 
  nothing but processes can hold a resource anyway.

### Running changes in beta version:
* Breaking change: `cmb_buffer_get_name()` renamed to `cmb_buffer_name()` for
  consistency with other similar functions, and to avoid any confusion with the
  `cmb_buffer_get()` function.
* Breaking change: `cmb_random_loaded_dice(int64_t a, int64_t b, double *pa[b - a + 1])` 
  now takes three arguments, the end points `a` and `b` in addition to the probability 
  array `pa`. The new function `cmb_random_discrete_nonuniform(uint64_t n, double *pa[n])` 
  has the earlier semantics, returning a value on `[0, n-1]`. This gives better symmetry with
  `cmb_random_dice(int64_t a, int64_t b)` and `cmb_random_discrete_uniform(uint64_t n)`.
* Bug fixes and approx 50 % speed improvement, mainly from improvements to the hash map 
  part of the hash-heap data structures, improved register handling in context switches,
  and improved algorithms for some random number distributions.
* Added functions to set the stack size of a `cmb_process`, either for a single process 
  when initialized by `cmb_process_initialize_wssz()`, or globally for all future 
  processes by `cmb_process_default_stacksize_set()`.
* Added `cmb_priorityqueue`, `cmb_process_timer`, `cmb_process_yield()` and
  `cmb_process_resume()`.
* Added `cimba_thread_hooks_set()` and `cimba_thread_context()` for managing CUDA 
  streams and a worked tutorial example of how to use CUDA parallelism to accelerate 
  model physics.
* Added the number of failed trials as return value from `cimba_run()` (renamed from 
  `cimba_run_experiment`, the old name deprecated).
* Added `setjmp`/`longjmp` error recovery in multithreaded trials after call to 
  `cmb_logger_error()` (which abandons the current trial), with memory recovery of any 
  abandoned Cimba objects.
* Adapted for use with ASan, UBSan, TSan, and LeakSan. Running these automatically on 
  GitHub CI runners after each git push.
* Coroutines adapted for Windows 11 and modern CPUs with improved stack security measures.
* Added `cmb_random_fmix64()` to bootstrap deterministic thread seeds from a master seed.
* Added tutorial 5 demonstrating CUDA integration.

### 2025-12-27: 3.0.0 beta
* Initial public version
* Documentation on Read the Docs
* Support for Linux and Windows 10 on x86-64
* Should be considered an active build with ongoing changes
