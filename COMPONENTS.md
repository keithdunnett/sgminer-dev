
## ADL 
Removed. To be replaced with an AMDGPU sysfs interface, or some sort of abstraction layer for
a similar 

## API
Moved into its own subdirectory. Rudimentary bash client script included, uses netcat and jq.

## CMAKE
Working on it. 

## AUTOTOOLS


## GNULIB
Culled. This build targets Linux and stripping down over portability - which could be added
back later or not, as the case may be. 






## OpenCL
Using Khronos OpenCL headers, sticking at version 1.2 to suit amdgpu-pro. 

## UTHASH
Now imported as a submodule, compiled as libut.a and function definitions from libut.h


