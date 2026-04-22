## Cimba change log

Note: Version numbers are MAJOR.MINOR.PATCH, but we do *not* promise to follow Semantic 
Versioning in the strictest sense. There may be minor breaking changes in some obscure 
corner without this warranting a new major release. We will summarize new features, 
changes, and bug fixes below. For complete details, see the git commit history.

### Incremental changes on the way to the official 3.0.0 release
* Bug fixes and approx 50 % speed improvement
* Adds cmb_priorityqueue, cmb_process_timer, cmb_process_yield/resume
* Adds cimba_set_thread_hooks() and cimba_thread_context() for managing CUDA streams
* Coroutines adapted for Windows 11 with its improved stack security measures

### 2025-12-27: 3.0.0 beta
* Initial public version
* Documentation on Read the Docs
* Support for Linux and Windows 10 on x86-64
* Should be considered an active build with ongoing changes
