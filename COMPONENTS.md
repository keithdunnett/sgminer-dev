
## ADL 
Removed. To be replaced with the AMDGPU sysfs interface, or some sort of abstraction layer perhaps.

## API
Moved into its own subdirectory. Rudimentary bash client script included, uses netcat and jq.

## CMAKE
Working on it. 

## AUTOTOOLS


## GNULIB
Largely left alone so far, headers have been tidied into the include/ directory. This build mainly
targets the GNU/Linux platform anyway, so whether and how far we need GNUlib modules is worth thinking
about. So, what does it give us?

### sigprocmask

https://www.gnu.org/software/gnulib/manual/html_node/sigprocmask.html#sigprocmask

### sigaction

https://www.gnu.org/software/gnulib/manual/html_node/sigaction.html#sigaction

### memmem

https://www.gnu.org/software/gnulib/manual/html_node/memmem.html#memmem

### memchr

https://www.gnu.org/software/gnulib/manual/html_node/memchr.html#memchr







## OpenCL
Using Khronos OpenCL headers, sticking at version 1.2 to suit amdgpu
All versions are under include and can be changed at config time.

## UTHASH
Now imported as a submodule, compiled as libut.a and function definitions from libut.h


