cmake_minimum_required (VERSION 3.5)

include_directories(..)
add_library(ccan compiler/compiler.h opt/helpers.c opt/opt.c opt/opt.h opt/parse.c opt/private.h opt/usage.c typesafe_cb/typesafe_cb.h)
#target_include_directories(ccan PUBLIC $CMAKE_CURRENT_SOURCE_DIR)
