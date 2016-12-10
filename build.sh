#!/bin/bash

threads=$((`cat /proc/cpuinfo | grep processor | wc -l` + 1)); 
cmake -G "Unix Makefiles" . && time cmake --build . --clean-first -- -j$threads 
