cmake_minimum_required (VERSION 3.5)

INCLUDE(ExternalProject)

# ==================
# Define Our Project
# ==================

set (My_Project_Title "sgminer")
set (My_Project_Description "Ripping sgminer apart and putting it together again")
set (My_Project_Author "magick777")
set (My_Project_Revised_Date "2016-11-30")
set (My_Project_Version_Major 5)
set (My_Project_Version_Minor 5)
set (My_Project_Version_Patch 2)
set (My_Project_Version_Revision "magick")

set (My_Project_Version "${My_Project_Version_Major}.${My_Project_Version_Minor}.${My_Project_Version_Patch}")

set (My_Project_Copyright_Date "2016")
set (My_Project_Copyright "${My_Project_Copyright_Date} ${My_Project_Author}.")

string(TOUPPER ${My_Project_Title} My_Project_Title_Caps  )
string(REGEX REPLACE " " "_" My_Project_Title_Caps ${My_Project_Title_Caps} )

project (${My_Project_Title})

#
# Fetch and make use of external project sources
#

# Handle Jansson 2.9 as an external project.
# Considered doing away with the Jansson compile and instead depending on libjansson-dev in the
# operating system, but my Ubuntu install is on 2.7 and some of the bug fixes and new features
# in 2.9 could be worth having. We'll compile our own. 

ExternalProject_Add(jansson-master
  PREFIX jansson
  GIT_REPOSITORY https://github.com/akheron/jansson.git
  GIT_TAG master
  TIMEOUT 60
  BUILD_DIR=${SGMINER_SOURCE_DIR}
  BINARY_DIR=${SGMINER_SOURCE_DIR}
  INSTALL_DIR=${SGMINER_SOURCE_DIR}
  CONFIGURE_COMMAND CC="clang-4.0" cmake -G Ninja -DJANSSON_BUILD_DOCS=OFF -DCMAKE_INSTALL_PREFIX=../../../ ../jansson-master
  BUILD_COMMAND cmake --build . --clean-first
  INSTALL_COMMAND cmake --build . --target install
)
#ExternalProject_Get_Property(jansson-master install_dir)
#add_library(jansson STATIC IMPORTED)
#set_property(TARGET jansson PROPERTY IMPORTED_LOCATION lib/libjansson.a)
#add_dependencies(jansson jansson-master)

#add_subdirectory(lib)
#add_subdirectory(submodules)

# =========================
# Build Subdirectories
# =========================


add_subdirectory(ccan)
add_subdirectory(sph)
add_subdirectory(include)

# Handle GNUlib source dir as an external project, for now. We likely don't need it in this build.
#ExternalProject_Add(gnulib
#  SOURCE_DIR lib
#  BUILD_IN_SOURCE 1
#  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/lib
#  CONFIGURE_COMMAND ""
#  BUILD_COMMAND make
#  INSTALL_COMMAND make install PREFIX=${CMAKE_CURRENT_BINARY_DIR}/lib
#)
#ExternalProject_Get_Property(gnulib install_dir)
#add_library(gnu STATIC IMPORTED)
#set_property(TARGET gnu PROPERTY IMPORTED_LOCATION ${install_dir}/lib/libgnu.a)
#add_dependencies(gnulib gnu)
#include_directories(${install_dir}/lib)
#target_link_libraries(gnulib gnu)

#add_subdirectory(lib)
#add_subdirectory(submodules)


# =================
# Configure Project
# =================

# Search for included files here
include_directories( ${PROJECT_SOURCE_DIR}/include )
include_directories( ${PROJECT_SOURCE_DIR}/sph )
include_directories( ${PROJECT_SOURCE_DIR}/lib )
include_directories( ${PROJECT_SOURCE_DIR}/ccan )
include_directories(${PROJECT_BINARY_DIR})


# =================
# Macro Definitions
# =================

MACRO(ADD_PUBLIC_HEADER target filename)
	# Add filename to public_header_files list, flag it as 
	# public header for libraries and OS X Frameworks

	# This will work for creating one library/framework with public headers
	# per project. If you need more than one, you will need to customize
	# the workflow as appropriate, since there is only one 
	# public_header_files list.

	SET_TARGET_PROPERTIES(${target} PROPERTIES PUBLIC_HEADER ${filename})

	LIST(APPEND public_header_files ${filename})

	SET_SOURCE_FILES_PROPERTIES(
		${filename}
		PROPERTIES
		MACOSX_PACKAGE_LOCATION
		PUBLIC_HEADER
	)

	# Set Xcode project to configure public header location to allow
	# use when this project is used in another workspace.
	# NOTE: You must manually add a "Headers" build phase and add
	# the desired public headers to that list for Xcode to use them.
	#
	# TODO: If anyone knows how to automate that in Cmake, I would be very
	# greatful!!

	SET_TARGET_PROPERTIES(${target} PROPERTIES
		XCODE_ATTRIBUTE_PUBLIC_HEADERS_FOLDER_PATH
		"include/$(TARGET_NAME)"
	)

	SET_TARGET_PROPERTIES(${target} PROPERTIES
		XCODE_ATTRIBUTE_PRIVATE_HEADERS_FOLDER_PATH
		"$(PUBLIC_HEADERS_FOLDER_PATH)/Private"
	)
	
	# Set Xcode target to include settings for OS X and iOS
	
	SET_TARGET_PROPERTIES(${target} PROPERTIES
		XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS
		"macos iphonesimulator iphoneos"
	)

	SET_TARGET_PROPERTIES(${target} PROPERTIES
		XCODE_ATTRIBUTE_VALID_ARCHITECTURES
		"x86_64 i386 armv6 armv7 armv7s arm64"
	)

ENDMACRO(ADD_PUBLIC_HEADER)


# The target should be an OS X Bundle with a PList

MACRO(MAKE_TARGET_BUNDLE targetname)

	set_target_properties(
		${targetname}
		PROPERTIES
		MACOSX_BUNDLE_INFO_PLIST
		${PROJECT_SOURCE_DIR}/templates/plist.in
	)

ENDMACRO(MAKE_TARGET_BUNDLE)


MACRO(ADD_LINKED_FRAMEWORK frame)

	SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGES} -framework ${frame}")

ENDMACRO(ADD_LINKED_FRAMEWORK)


# ======================
# Process Template Files
# ======================

file(READ ${PROJECT_SOURCE_DIR}/COPYING My_Project_License)

string(REGEX REPLACE "\n" "\n\t" My_Project_License_Indent ${My_Project_License})

#configure_file (
#	"${PROJECT_SOURCE_DIR}/templates/template.c.in"
#	"${PROJECT_BINARY_DIR}/template.c"
#)
#
#configure_file (
#	"${PROJECT_SOURCE_DIR}/templates/template.h.in"
#	"${PROJECT_BINARY_DIR}/template.h"
#)

# Update the project README, to pull in new version #, etc.
#configure_file (
#	"${PROJECT_SOURCE_DIR}/templates/README.md.in"
#	"${CMAKE_CURRENT_LIST_DIR}/README.md"
#)

#configure_file (
#	"${PROJECT_SOURCE_DIR}/templates/version.h.in"
#	"${PROJECT_BINARY_DIR}/version.h"
#)


# ============
# Source Files
# ============

# src_files are the primary files, and will be included in doxygen documentation
set(src_files
lib/dummy.c
ccan/opt/helpers.c
ccan/opt/opt.c
ccan/opt/parse.c
ccan/opt/usage.c
sph/bmw.c
sph/echo.c
sph/jh.c
sph/luffa.c
sph/gost.c
sph/simd.c
sph/blake.c
sph/cubehash.c
sph/groestl.c
sph/keccak.c
sph/shavite.c
sph/skein.c
sph/sha2.c
sph/sha2big.c
sph/fugue.c
sph/hamsi.c
sph/panama.c
sph/shabal.c
sph/whirlpool.c
sph/sha256_Y.c
sph/ripemd.c
events.c
include/uthash/libut/src/libut.c
include/uthash/libut/src/utvector.c
include/uthash/libut/src/utmm.c
include/uthash/libut/src/ringbuf.c
algorithm/ethgencache.c
algorithm/eth-sha3.c
sgminer.c
api/api.c
util.c
logging.c
driver-opencl.c
ocl.c
findnonce.c
pool.c
algorithm.c
config_parser.c
ocl/build_kernel.c
ocl/binary_kernel.c
algorithm/scrypt.c
algorithm/darkcoin.c
algorithm/qubitcoin.c
algorithm/quarkcoin.c
algorithm/myriadcoin-groestl.c
algorithm/fuguecoin.c
algorithm/inkcoin.c
algorithm/animecoin.c
algorithm/groestlcoin.c
algorithm/sibcoin.c
algorithm/sifcoin.c
algorithm/twecoin.c
algorithm/marucoin.c
algorithm/maxcoin.c
algorithm/talkcoin.c
algorithm/bitblock.c
algorithm/x14.c
algorithm/fresh.c
algorithm/whirlcoin.c
algorithm/neoscrypt.c
algorithm/whirlpoolx.c
algorithm/pluck.c
algorithm/sia.c
algorithm/credits.c
algorithm/blake256.c
algorithm/blakecoin.c
algorithm/decred.c
algorithm/lbry.c
algorithm/ethash.c
algorithm/cryptonight.c
)

# Primary header files, also for doxygen documentation
set(header_files
)

# Public headers, will be installed in 'include'
# Do not manually add files here, use the ADD_PUBLIC_HEADER() macro
set(public_header_files
)

# Utility source files will not be included in doxygen
set(src_utility_files
#	src/GLibFacade.c
)

set(header_utility_files
#	src/GLibFacade.h
	${PROJECT_BINARY_DIR}/version.h
)

# Generate doxygen configuration file
string(REPLACE ";" " " doxygen_src_files "${src_files}" )
string(REPLACE ";" " " doxygen_header_files "${header_files}" )

#configure_file (
#	"${PROJECT_SOURCE_DIR}/templates/doxygen.conf.in"
#	"${PROJECT_BINARY_DIR}/doxygen.conf"
#)


# ===========================================
# Build Test Suite with CuTest (unit testing)
# ===========================================

set(test_files
	test/CuTest.c
	test/CuTest.h
	${PROJECT_BINARY_DIR}/AllTests.c
)

if (DEFINED TEST)
	add_definitions(-DTEST)

	add_executable(run_tests
		${test_files}
		${src_files}
		${header_files}
		${src_utility_files}
		${header_utility_files}
	)

	# Process source files to look for tests to run
	add_custom_command (
		OUTPUT ${PROJECT_BINARY_DIR}/AllTests.c
		COMMAND sh ${PROJECT_SOURCE_DIR}/test/make-tests.sh ${PROJECT_SOURCE_DIR}/src/*.c > ${PROJECT_BINARY_DIR}/AllTests.c
	)

	enable_testing()

	add_test( test ${PROJECT_BINARY_DIR}/run_tests)

	# valgrind memory testing
	find_program (MEMORYCHECK_COMMAND valgrind)
	SET (MEMORYCHECK_COMMAND_OPTIONS --leak-check=full --error-exitcode=1)

	add_test( memory_test ${MEMORYCHECK_COMMAND} ${MEMORYCHECK_COMMAND_OPTIONS} ${PROJECT_BINARY_DIR}/run_tests)

endif()


# =======================
# Configure for Target OS
# =======================

# OS X Builds
if (APPLE)

	# Configure backwards-compatible support (if your project allows it)
	SET(CMAKE_OSX_DEPLOYMENT_TARGET "10.4" CACHE STRING "Deployment target for OSX" FORCE)

	# Compile for x86_64 and i386.  ppc no longer supported
	if(CMAKE_BUILD_TYPE MATCHES "Release")
		SET (CMAKE_OSX_ARCHITECTURES x86_64;i386)
	endif(CMAKE_BUILD_TYPE MATCHES "Release")

	# Use PackageMaker for installers?
	if (DEFINED ZIP)
		set (CPACK_GENERATOR ZIP)
	else (DEFINED ZIP)
		set (CPACK_GENERATOR PackageMaker)
	endif (DEFINED ZIP)

endif (APPLE)

# Windows Builds
if (WIN32)

	# Use NSIS to generate installers?
	if (DEFINED ZIP)
		set (CPACK_GENERATOR ZIP)
	else (DEFINED ZIP)
		set (CPACK_GENERATOR NSIS)
	endif (DEFINED ZIP)

# Linux Builds (not cross-compiling for Windows)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")

	# Create zip archive
	set (CPACK_GENERATOR ZIP)

	# Statically link libraries -- might make the binary slightly more
	# compatible across Linux distributions, for example
	#
	# You may wish to disable this.
	#
	
	set (CMAKE_FIND_LIBRARY_SUFFIXES ".a")
	set (BUILD_SHARED_LIBRARIES OFF)
	set (CMAKE_EXE_LINKER_FLAGS "-static")

	# Some libraries need to be linked on some Linux builds
	if (DEFINED TEST)
		# target_link_libraries(run_tests m)
	endif (DEFINED TEST)

endif (WIN32)


# ==============
# Define targets
# ==============

# Create a library?
# add_library(libFOO STATIC
#	${src_files}
#	${src_utility_files}
#	${header_files}
#	${header_utility_files}
# )

# remove the extra "lib" from "liblibFOO"
# SET_TARGET_PROPERTIES(libFOO PROPERTIES PREFIX "")

# Create a command-line app?
# if (NOT DEFINED TEST)
# 	add_executable(main
# 		src/main.c
# 		src/GLibFacade.c
# 		src/GLibFacade.h
# 		${header_files}
# 	)
# 
#	Link the library to the app?
#	target_link_libraries(main libFOO)
# endif()

# Xcode settings for fat binaries
# set_target_properties(libFOO PROPERTIES XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")
# set_target_properties(main PROPERTIES XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")


# ==========================
# Build Installer with CPack
# ==========================

# You're largely on your own here

# install (FILES ${CMAKE_CURRENT_LIST_DIR}/README.md ${PROJECT_SOURCE_DIR}/LICENSE.txt
# 	DESTINATION .
# )

# Use something like this to install public header files (after adding them
# with the ADD_PUBLIC_HEADER() macro)

# install (FILES ${public_header_files} DESTINATION local/include/libFoo) 

set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "${My_Project_Description}")
set (CPACK_PACKAGE_VENDOR "${My_Project_Author}")
set (CPACK_PACKAGE_VERSION "${My_Project_Version_Major}.${My_Project_Version_Minor}.${My_Project_Version_Patch}")
set (CPACK_PACKAGE_VERSION_MAJOR "${My_Project_Version_Major}")
set (CPACK_PACKAGE_VERSION_MINOR "${My_Project_Version_Minor}")
set (CPACK_PACKAGE_VERSION_PATCH "My_Project_Version_Patch")

set (CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/COPYING")

if (APPLE)
	set (CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-Mac-${CPACK_PACKAGE_VERSION}")
else (APPLE)
	set (CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${CMAKE_SYSTEM_NAME}-${CPACK_PACKAGE_VERSION}")
endif (APPLE)

# Use some default install locations (if appropriate)
if (WIN32)
	set (CPACK_INSTALL_PREFIX "")
	set (CPACK_NSIS_MODIFY_PATH ON)
else (WIN32)
	set (CPACK_INSTALL_PREFIX /usr/local)
endif (WIN32)

set (CPACK_PACKAGE_INSTALL_DIRECTORY ${PROJECT})

include (CPack)
