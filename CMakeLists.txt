cmake_minimum_required (VERSION 3.5)

# ==================
# Define Our Project
# ==================

set (My_Project_Title "sgminer")
set (My_Project_Description "Ripping sgminer apart and putting it together again")
set (My_Project_Author "the many sgminer developers over the years")
set (My_Project_Revised_Date "2016-11-30")
set (My_Project_Version_Major 5)
set (My_Project_Version_Minor 5)
set (My_Project_Version_Patch 2)
set (My_Project_Version_Revision "magick")
set (My_Project_Version "${My_Project_Version_Major}.${My_Project_Version_Minor}.${My_Project_Version_Patch}")
string(TOUPPER ${My_Project_Title} My_Project_Title_Caps  )
string(REGEX REPLACE " " "_" My_Project_Title_Caps ${My_Project_Title_Caps} )
project (${My_Project_Title})
set(BINARY_NAME "sgminer-dev")

# Global CMAKE settings

set(CMAKE_VERBOSE_MAKEFILES "0")
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/Modules")
set(CMAKE_PREFIX_PATH "/usr /usr/lib/x86_64-linux-gnu /opt/amdgpu-pro/lib/x86_64-linux-gnu")
set(CMAKE_GENERATOR "Unix Makefiles")
#set(CMAKE_C_COMPILER_ID "Clang")
#set(CMAKE_C_COMPILER_ID "GNU")




set(OpenCL_LIBRARY "/opt/amdgpu-pro/lib/x86_64-linux-gnu/libOpenCL.so")
set(OPENCL_LIBRARIES ${OpenCL_LIBRARY})
set(OPENCL_INCLUDE_PATH "/opt/AMDAPPSDK-3.0/include")
set(OPENCL_INCLUDE_DIRS ${OPENCL_INCLUDE_PATH})


# Were we asked to clean first? Do the same for projects that we build.

#set(BUILDSWITCHES "")
if (${CLEAN_FIRST})
	set(BUILDSWITCHES "--clean-first")
endif()

OPTION(DEFINE_DEBUG
  "Build the project using debugging code"
  OFF)
IF(DEFINE_DEBUG)
  MESSAGE("Adding Debug flag...")
  ADD_DEFINITIONS(-DDEBUG)
  SET(CMAKE_BUILD_TYPE Debug)
  MESSAGE("Build type is " ${CMAKE_BUILD_TYPE})
  set(BINARY_NAME "sgminer-debug")
ENDIF(DEFINE_DEBUG)

OPTION(DEFINE_MINSIZE
  "Build the project for smallest binary size"
  OFF)

IF(DEFINE_MINSIZE)
  MESSAGE("Adding MinSizeRel flag...")
  ADD_DEFINITIONS(-DMINSIZEREL)
  SET(CMAKE_BUILD_TYPE MinSizeRel)
  MESSAGE("Build type is " ${CMAKE_BUILD_TYPE})
  set(BINARY_NAME "sgminer-minsizerel")
ENDIF(DEFINE_MINSIZE)



#OPTION(DEFINE_RELEASE
#  "Build the project with optimisation level -O3"
#  OFF)#######

#IF(DEFINE_RELEASE)
#  MESSAGE("Adding Release flag...")
#  ADD_DEFINITIONS(-DRELEASE)
#  SET(CMAKE_BUILD_TYPE Release)
#  MESSAGE("Build type is " ${CMAKE_BUILD_TYPE})
#  set(BINARY_NAME "sgminer-release")
#ENDIF(DEFINE_RELEASE)


#
# Define the CMake modules we want to use
#

INCLUDE(ExternalProject)
INCLUDE(FeatureSummary)
INCLUDE(FindPackageHandleStandardArgs)
INCLUDE(FindPackageMessage)
INCLUDE(FindOpenCL)
INCLUDE(FindCurses)
INCLUDE(FindJansson)
INCLUDE(FindCURL)



option(USE_CURSES "Compile with the curses-based terminal interface (recommended)" ON)
add_feature_info(USE_CURSES ${USE_CURSES} "Compile with the curses-based terminal interface (recommended)")

option(USE_LIBSPH "Compile and link the RNRT SAPHIR crypto library (currently needed)" ON)
add_feature_info(USE_LIBSPH ${USE_LIBSPH} "Compile and link the RNRT SAPHIR crypto library (currently required)")

option(USE_LIBCURL "Build with libcurl for getwork and GBT" OFF)
add_feature_info(USE_LIBCURL ${USE_LIBCURL} "Dependency on libcurl for gbt/getwork (not needed for stratum)")




feature_summary(WHAT ENABLED_FEATURES
                INCLUDE_QUIET_PACKAGES
                DESCRIPTION "Enabled Features:"
                VAR enabledFeaturesText)
feature_summary(WHAT DISABLED_FEATURES
                INCLUDE_QUIET_PACKAGES
                DESCRIPTION "Disabled Features:"
                VAR disabledFeaturesText)

message(STATUS "${enabledFeaturesText}")
message(STATUS "${disabledFeaturesText}")

#
# Fetch and make use of external project sources
#

# Handle Jansson 2.9 as an external project.
# Considered doing away with the Jansson compile and instead depending on libjansson-dev in the
# operating system, but my Ubuntu install is on 2.7 and some of the bug fixes and new features
# in 2.9 could be worth having. We'll compile our own. 

set(JANSSON_GIT "false")

if (${JANSSON_GIT})

ExternalProject_Add(jansson-git
  PREFIX jansson
  GIT_REPOSITORY https://github.com/akheron/jansson.git
  GIT_TAG master
  TIMEOUT 60
  BUILD_DIR=${SGMINER_SOURCE_DIR}
  BINARY_DIR=${SGMINER_SOURCE_DIR}
  INSTALL_DIR=${SGMINER_SOURCE_DIR}
  CONFIGURE_COMMAND cmake -G "Ninja" -DJANSSON_BUILD_DOCS=OFF -DCMAKE_INSTALL_PREFIX=../../../ ../jansson-git
  BUILD_COMMAND cmake --build . ${BUILDSWITCHES}
  INSTALL_COMMAND cmake --build . --target install
)
ExternalProject_Get_Property(jansson-git install_dir)
add_library(jansson STATIC IMPORTED)
set_property(TARGET jansson PROPERTY IMPORTED_LOCATION lib/libjansson.a)
add_dependencies(jansson jansson-git)


endif()

# =========================
# Build Subdirectories
# =========================


add_subdirectory(ccan)
add_subdirectory(sph)
add_subdirectory(include)



# =================
# Configure Project
# =================

# Search for included files here
include_directories( ${JANSSON_INCLUDE_DIR} )
#include_directories( ${PROJECT_SOURCE_DIR}/include )
include_directories( ${PROJECT_SOURCE_DIR}/sph )
include_directories( ${PROJECT_SOURCE_DIR}/lib )
include_directories( ${PROJECT_SOURCE_DIR}/ccan )
include_directories( ${PROJECT_BINARY_DIR} )
include_directories( ${OPENCL_INCLUDE_PATH} )
include_directories( ${CURSES_INCLUDE_PATH} )
#include_directories( ${CURL_INCLUDE_DIR} )


#file(READ ${PROJECT_SOURCE_DIR}/COPYING My_Project_License)

#string(REGEX REPLACE "\n" "\n\t" My_Project_License_Indent ${My_Project_License})

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

# Primary header files
set(header_files
./algorithm.h
./algorithm/animecoin.h
./algorithm/bitblock.h
./algorithm/blake256.h
./algorithm/blakecoin.h
./algorithm/cn-aes-tbls.h
./algorithm/credits.h
./algorithm/cryptonight.h
./algorithm/darkcoin.h
./algorithm/decred.h
./algorithm/ethash.h
./algorithm/eth-sha3.h
./algorithm/fresh.h
./algorithm/fuguecoin.h
./algorithm/groestlcoin.h
./algorithm/inkcoin.h
./algorithm/lbry.h
./algorithm/marucoin.h
./algorithm/maxcoin.h
./algorithm/myriadcoin-groestl.h
./algorithm/neoscrypt.h
./algorithm/pluck.h
./algorithm/quarkcoin.h
./algorithm/qubitcoin.h
./algorithm/scrypt.h
./algorithm/sia.h
./algorithm/sibcoin.h
./algorithm/sifcoin.h
./algorithm/sysendian.h
./algorithm/talkcoin.h
./algorithm/twecoin.h
./algorithm/whirlcoin.h
./algorithm/whirlpoolx.h
./algorithm/x14.h
./api/api.h
./ccan/compiler/compiler.h
./ccan/opt/opt.h
./ccan/opt/private.h
./ccan/opt/test/utils.h
./ccan/typesafe_cb/typesafe_cb.h
./config.h
./functions/config/config_parser.h
./driver-opencl.h
./events.h
./findnonce.h
./include/arg-nonnull.h
./include/bench_block.h
./include/c++defs.h
./include/compat.h
./include/elist.h
./include/uthash/src/libut.h
./include/uthash/src/ringbuf.h
./include/uthash/src/utarray.h
./include/uthash/src/uthash.h
./include/uthash/src/utlist.h
./include/uthash/src/utmm.h
./include/uthash/src/utringbuffer.h
./include/uthash/src/utstring.h
./include/uthash/src/utvector.h
./include/uthash/tests/lru_cache/cache.h
./include/warn-on-use.h
./logging.h
./miner.h
./ocl/binary_kernel.h
./ocl/build_kernel.h
./ocl.h
./pool.h
./sph/sha256_Y.h
./sph/sph_blake.h
./sph/sph_bmw.h
./sph/sph_cubehash.h
./sph/sph_echo.h
./sph/sph_fugue.h
./sph/sph_gost.h
./sph/sph_groestl.h
./sph/sph_hamsi.h
./sph/sph_jh.h
./sph/sph_keccak.h
./sph/sph_luffa.h
./sph/sph_panama.h
./sph/sph_ripemd.h
./sph/sph_sha2.h
./sph/sph_shabal.h
./sph/sph_shavite.h
./sph/sph_simd.h
./sph/sph_skein.h
./sph/sph_types.h
./sph/sph_whirlpool.h
./util.h
)

# Public headers, will be installed in 'include'
# Do not manually add files here, use the ADD_PUBLIC_HEADER() macro
#set(public_header_files
#)

# Utility source files will not be included in doxygen
#set(src_utility_files
#	src/GLibFacade.c
#)

#set(header_utility_files
#	src/GLibFacade.h
#	${PROJECT_BINARY_DIR}/version.h
#)

# Generate doxygen configuration file
#string(REPLACE ";" " " doxygen_src_files "${src_files}" )
#string(REPLACE ";" " " doxygen_header_files "${header_files}" )

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
	
#	set (CMAKE_FIND_LIBRARY_SUFFIXES ".a")
#	set (BUILD_SHARED_LIBRARIES OFF)
#	set (CMAKE_EXE_LINKER_FLAGS "-static")

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

#Create a command-line app?


 if (NOT DEFINED TEST)
add_executable(${BINARY_NAME}
#algorithm/animecoin.c
#algorithm/bitblock.c
#algorithm/blake256.c
#algorithm/blakecoin.c
#algorithm/credits.c
#algorithm/cryptonight.c
#algorithm/darkcoin.c
#algorithm/decred.c
algorithm/ethash.c
algorithm/ethgencache.c
algorithm/eth-sha3.c
#algorithm/fresh.c
#algorithm/fuguecoin.c
#algorithm/groestlcoin.c
#algorithm/inkcoin.c
#algorithm/lbry.c
#algorithm/marucoin.c
#algorithm/maxcoin.c
#algorithm/myriadcoin-groestl.c
#algorithm/neoscrypt.c
#algorithm/pluck.c
#algorithm/quarkcoin.c
#algorithm/qubitcoin.c
#algorithm/scrypt.c
#algorithm/sia.c
#algorithm/sibcoin.c
#algorithm/sifcoin.c
#algorithm/talkcoin.c
#algorithm/twecoin.c
#algorithm/whirlcoin.c
#algorithm/whirlpoolx.c
#algorithm/x14.c
algorithm.c
api/api.c
driver-opencl.c
events.c
findnonce.c
logging.c
ocl/binary_kernel.c
ocl/build_kernel.c
ocl.c
pool.c
util.c
functions/config/config_parser.c
functions/config/config_writer.c
functions/libcurl/util.c
functions/libcurl/remote-config.c
functions/libcurl/exsgminer.c
sgminer.c
${header_files}
 	)

if (${JANSSON_GIT})
	add_dependencies(${BINARY_NAME} jansson)
endif()
	add_dependencies(${BINARY_NAME} ccan)
	add_dependencies(${BINARY_NAME} sph)
	add_dependencies(${BINARY_NAME} ut)

#	Link the library to the app?

	target_link_libraries(${BINARY_NAME} dl)
	target_link_libraries(${BINARY_NAME} rt)
	target_link_libraries(${BINARY_NAME} m)
	target_link_libraries(${BINARY_NAME} pthread)
	target_link_libraries(${BINARY_NAME} ccan)
	target_link_libraries(${BINARY_NAME} sph)
	target_link_libraries(${BINARY_NAME} ${OPENCL_LIBRARIES})
	target_link_libraries(${BINARY_NAME} ${CURSES_FORM_LIBRARY})
	target_link_libraries(${BINARY_NAME} ${CURSES_NCURSES_LIBRARY})
	target_link_libraries(${BINARY_NAME} ${CURL_LIBRARIES})
	target_link_libraries(${BINARY_NAME} ${JANSSON_LIBRARIES})
endif()
