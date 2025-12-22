.. _installation:

Installation
===============

You will need a C compiler like gcc or clang, the NASM assembler, and a development
toolchain of git, meson, and ninja. For some of the examples in the tutorial, you
will need the free plotting program gnuplot, but this is not strictly necessary for
using Cimba.

We also recommend using a modern integrated development environment (IDE) like CLion,
which has the advantage of being available both on Linux and Windows, integrated
with a gcc toolchain (called MinGW under Windows), and free for open source
work. Microsoft Visual Studio with MVSC should also work, but we have not tested
it yet.

Once the build chain is installed, you need to obtain Cimba itself.
Cimba is distributed as free open source code through the Github repository at
https://github.com/ambonvik/cimba. You download, build, and install Cimba with
terminal commands. On Linux, it is straightforward:

.. code-block:: bash

    git clone https://github.com/ambonvik/cimba
    cd cimba
    meson setup build
    meson compile -C build
    sudo meson install -C build

You need elevated privileges for the last step, since it installs
the library and header files in system locations  `/usr/local/include/cimba`
and `/usr/local/bin/cimba`.

As always, things are more complicated in Windows. We have encapsulated most
of the complexity in a batch script called `setup_MinGW.bat`. You need to run
it from a command shell (`cmd.exe`) as administrator:

.. code-block:: batch

   git clone https://github.com/ambonvik/cimba
   cd cimba
   setup_MinGW.bat
   meson compile -C build
   meson install -C build

If you have Windows Security ransomware protection enabled (and you should),
you may have to allow access for the build chain applications git, meson, ninja,
gcc, nasm, and ld.

After installation, we can write a C program like `tutorial/hello.c`:

.. code-block:: c

    #include <cimba.h>
    #include <stdio.h>

    int main(void) {
        printf("Hello world, I am Cimba %s.\n", cimba_version());
    }

We compile and run it as any other C program, linking to the Cimba library:

.. code-block:: bash

    gcc hello.c -lcimba -o hello
    ./hello

If all goes well, this should produce output similar to::

    Hello world, I am Cimba 3.0.0 beta.

You now have a working Cimba installation.

