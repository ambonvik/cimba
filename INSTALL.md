## Installation guide

You will need a C compiler like ``gcc`` or ``clang``, the ``nasm`` assembler, and a 
development toolchain of ``git``, ``meson``, and ``ninja``. 

For some of the examples in the tutorial, you will need the free plotting programs 
``gnuplot`` and ``paraview``, and the NVidia GPU programming environment CUDA, but 
this is not strictly necessary for building and using Cimba.

We recommend using a modern integrated development environment (IDE) like CLion,
which has the advantage of being available both on Linux and Windows, integrated
with a gcc toolchain (called MinGW under Windows), and free for open source work.

Once the build chain is installed, you need to obtain Cimba itself.
Cimba is distributed as free open source code through the GitHub repository at
https://github.com/ambonvik/cimba. You download, build, and install Cimba with
terminal commands.

### Linux

On Linux, open a terminal window, change directory to where you want to have the source
code tree, and enter these commands:

```
    git clone https://github.com/ambonvik/cimba
    cd cimba
    meson setup build
    meson compile -C build
    sudo meson install -C build
```

You need elevated privileges (``sudo``) for the last step, since it installs
the library and header files in system locations  ``/usr/local/lib``
and ``/usr/local/include``. These should already be in the respective paths for
compilation and runtime linking.

Alternatively, instad of cloning the entire repository, download the latest release in
either ``zip`` or ``tar.gz`` format from https://github.com/ambonvik/cimba/releases

Unpack it into your target directory, ``cd`` there, build and install it as above.

Cimba should build without any warnings or errors. If you encounter errors, such as the
include file not found by the compiler or the Cimba library not found at runtime, please
check that these paths are correctly configured on your system. The details of how to fix
the issue may be distro-specific, so we will not try to give a recipe here.

### Windows

As always, getting something to work on Windows is a bit more involved.
See https://cimba.readthedocs.io/en/latest/installation.html for the details.

## Verifying your installation

After installation, we can write a C program like
[tutorial/hello.c](https://github.com/ambonvik/cimba/blob/main/tutorial/hello.c):

```    
    #include <cimba.h>
    #include <stdio.h>

    int main(void) {
        printf("Hello world, I am Cimba %s.\n", cimba_version());
    }
``` 

Compile it as any other C program, linking to the Cimba library:

``` 
    gcc hello.c -lcimba -o hello
``` 

If all goes well, this program should produce output similar to::

``` 
    Hello world, I am Cimba 3.0.3
``` 

You now have a working Cimba installation.

For a more comprehensive unit test, type:

``` 
    meson test -C build
``` 

This will run the full suite of module unit tests from the ``test`` directory.
