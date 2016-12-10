#!/bin/bash


CMAKE_ARGS="$1"
CLEAN_FIRST=""

threads=$((`cat /proc/cpuinfo | grep processor | wc -l` + 1)); 

cmake "$CMAKE_ARGS" .

cmake --build . --clean-first -- -j$threads 
