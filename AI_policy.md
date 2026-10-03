## Cimba discrete event simulation library

Copyright (c) Asbjørn M. Bonvik 2026.
Licensed under the Apache License, Version 2.0 (see LICENSE).

### Preliminary project policy on use of generative AI

As of mid-2026, LLM-driven tools have become very useful for coding tasks. However, the state of the art is still not at a point where an LLM model can be trusted to independently write critical code. Moreover, the copyright implications of LLM-generated code are unclear. For that reason, the de facto AI policy in the Cimba project can be summarized as follows:

#### Definitions:

"Human-authored" means that a named person wrote the code, or reviewed and substantively revised any AI-proposed code, understands every line of the code, and is accountable for it.

"Agentic" means that code was generated, written into the source code file, possibly also committed to the repository by an AI tool acting independently across multiple steps, also if a human approves those steps.

#### Library and test battery

* All code in the library and any code in the test battery that runs as automated verification (i.e., anything that will execute from the command `meson test`) must be human-authored. It is acceptable to ask an AI model to propose draft code for some specific task, but any such proposal is reviewed, rewritten as needed, and integrated by hand into the codebase. 
* Adversarial AI-driven code reviews are encouraged. The results from such reviews are stored in the repository, directory `cimba/code_reviews/` with follow-up reviews to verify that any defects have been addressed. Any fixes still need to be human-authored, as described above.

#### Tutorials and examples

* These are built for illustrative purposes, not strictly part of the library itself or the verification of it. This may be partly AI-assisted, but still needs to be reviewed and adapted by a human author. Use of AI needs to be stated in the file header, describing both what tool was used and what parts of the code were AI-assisted. For examples, see, e.g., `tutorial/tut_5_3.c` and `tutorial/tut_5_3.cu`.

#### Contributions

* The above applies to contributed code. Agentic code contributed to the library or test battery will not be accepted. AI-supported examples may be acceptable. Any AI assistance must be disclosed in the pull request. The maintainer may choose to reimplement a contribution instead of merging it if deemed more appropriate.
* Any bug reports or issues are welcome, also if they were found by AI tooling. Any edge cases in a discrete event simulation may be quite obscure, and as of mid-2026, the current generation of frontier models are already very good at spotting any inconsistencies. For examples, see `cimba\code_reviews`.
* Independent projects built on Cimba are governed by their own policies.