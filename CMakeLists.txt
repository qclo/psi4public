project(psi4)
cmake_minimum_required(VERSION 2.8)

enable_language(Fortran)

# cmake modules are relative to this file
get_filename_component(ROOT_SRC_DIR ${CMAKE_CURRENT_LIST_FILE} PATH)
set(CMAKE_MODULE_PATH "${ROOT_SRC_DIR}/cmake/")

#
# Command line options
#
set(ALL_USEROPTS "")
set(EMPTY "\"\"")

include(cmake/GetValue.cmake)
#OPTION CXXFLAGS        Extra C++ compiler flags to be used in compilation (string)
get_value(useroptCXX_FLAGS CXXFLAGS   ${EMPTY})
#OPTION F77FLAGS        Extra Fortran compiler flags to be used in compilation (string)
get_value(useroptF77_FLAGS F77FLAGS   ${EMPTY})
#OPTION F77SYMBOL       The Fortran compiler name mangling convention, used for linking external Fortran libraries, such as BLAS. Values are lcu (lower case with traling underscore), lc (lower case), ucu (upper case with trailing underscore), uc (upper case). If omitted CMake will detect it automatically if a Fortran compiler is present, if not it will use lcu. (string)
get_value(useroptF77SYMBOL F77SYMBOL  "detect")
#OPTION LAPACK           Linker flags to use for Lapack (and BLAS), instead of detecting system library (string)
get_value(useroptLAPACK  LAPACK   ${EMPTY})
#OPTION LD_FLAGS         Extra flags to be used in linking (string)
get_value(useroptLDFLAGS  LDFLAGS   ${EMPTY})
#OPTION PREFIX           The installation directory for psi4 (string)
get_value(CMAKE_INSTALL_PREFIX  PREFIX  "/usr/local/psi4")
#OPTION PYTHON           Full path to python interpreter to use. If omitted Python will be detected. There should be a corresponding python-config script in this location for Psi to work, i.e. Python development version. (string)
get_value(useroptPYTHON  PYTHON   ${EMPTY})
#OPTION USE_SYSTEM_BOOST Whether to use system boost, if detected. False forces Psi to build its own Boost. (boolean)
get_value(useroptUSE_SYSTEM_BOOST  USE_SYSTEM_BOOST   TRUE)
#OPTION MAX_AM_ERI    The maximum angular momentum to allow for integrals (1=p,2=d,3=f, etc.).  First and second derivatives are supported up to MAX_AM_ERI-1 and MAX_AM_ERI-2, respectively. (int)
get_value(useroptLIBINT_MAX_AM  MAX_AM_ERI  5)

set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${useroptLDFLAGS}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${useroptCXX_FLAGS}")
set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} ${useroptF77_FLAGS}")
# This will be echoed at the end of configure, and also output to a file so the build can be replicated
set(CONFIGURE_CMD "${CMAKE_COMMAND} ${PROJECT_SOURCE_DIR}")
foreach(opt ${ALL_USEROPTS})
    set(CONFIGURE_CMD "${CONFIGURE_CMD} -D${opt}")
endforeach()

#
# Psi4 configuration
#
set(PSI_VERSION \"4.0\")
set(psi4datadir \"${CMAKE_INSTALL_PREFIX}\")
set(top_srcdir \"${PROJECT_SOURCE_DIR}\")
set(top_objdir \"${PROJECT_BINARY_DIR}\")

#set(PSI_VERSION "4.0")
set(PACKAGE_VERSION "4.0git")
set(PACKAGE_NAME "Psi")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "psicode@users.sourceforge.net")

if( CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND NOT MSVC_IDE )
  message(FATAL_ERROR "In-source builds are not allowed.
Please create a directory and run cmake from there, passing the path
to this source directory as the last argument.
This process created the file `CMakeCache.txt' and the directory `CMakeFiles'.
Please delete them.")
endif()

#
# OpenMP
#
find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

include(CheckIncludeFiles)
check_include_files(dlfcn.h HAVE_DLFCN)

include(CheckFunctionExists)
check_function_exists(MKL_Free_Buffers HAVE_MKL) # NEEDS TO BE TESTED
check_function_exists(erf HAVE_FUNC_ERF)

include(CheckSymbolExists)
check_symbol_exists(MPI_Finalize mpi.h HAVE_MPI) # NEEDS TO BE TESTED

include(cmake/Restrict.cmake)
test_restrict(restrict)

if(CMAKE_Fortran_COMPILER)
    set(HAVE_ERD 1)
endif()
configure_file(include/psiconfig.h.cmake.in include/psiconfig.h)

# Is this needed for the script?
# TODO: Fix gitversion.py to work without setting top_srcdir...maybe use PROJECT_SOURCE_DIR
set(top_srcdir ${PROJECT_SOURCE_DIR}) # The quotes are added in the source, in this case
configure_file(src/bin/psi4/gitversion.py.in src/bin/psi4/gitversion.py)

#
# Libint configuration
# Perhaps move to libint's cmakelists.txt file

set(LIBINT_OPT_AM ${useroptLIBINT_MAX_AM})
math(EXPR LIBDERIV_OPT_AM1 ${LIBINT_OPT_AM}-1) # A.M. level for 1st derivative ERIs
math(EXPR LIBDERIV_OPT_AM2 ${LIBINT_OPT_AM}-2) # A.M. level for 2nd derivative ERIs
math(EXPR LIBINT_NEW_AM ${LIBINT_OPT_AM}*2)
math(EXPR LIBDERIV_NEW_AM1 ${LIBDERIV_OPT_AM1}*2)
math(EXPR LIBDERIV_NEW_AM2 ${LIBDERIV_OPT_AM2}*2)
math(EXPR LIBDERIV_NEW_AM12 ${LIBDERIV_OPT_AM2}*2)
configure_file(src/lib/libint/libint_config.h.in src/lib/libint/libint_config.h)
configure_file(src/lib/libderiv/libderiv_config.h.in src/lib/libderiv/libderiv_config.h)

# Psi4-specific includes and libraries
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(
    ${PROJECT_BINARY_DIR}/include 
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/lib
    ${PROJECT_BINARY_DIR}/src/lib
)

#
# BLAS and LAPACK
#
#if(useroptBLAS)
#    set(BLAS_Libraries ${useroptBLAS})
#    message(WARNING "Skipping BLAS detection and using these flags: ${useroptBLAS}")
#else()
#    find_package(BLAS REQUIRED)
#endif()
if(NOT useroptLAPACK STREQUAL EMPTY)
    set(LAPACK_LIBRARIES ${useroptLAPACK})
    message(WARNING "Skipping LAPACK detection and using these flags: ${useroptLAPACK}")
else()
    find_package(LAPACK REQUIRED)
endif()
#if(NOT MKL_Found)
#    find_package(LAPACK REQUIRED)   # "Finding LAPACK implies BLAS was found too"
#endif()

#
# Fortran (yuk!) for external linking
#
if(useroptF77SYMBOL STREQUAL "detect")
    if(CMAKE_Fortran_COMPILER)
        include(cmake/FortranCInterface.cmake)
        get_fc_symbol(FC_SYMBOL)
    else()
        set(FC_SYMBOL 2)
    endif()
else()
    # The user told us what to do
    if(useroptF77SYMBOL STREQUAL "ucu")
        set(FC_SYMBOL 4)
    elseif(useroptF77SYMBOL STREQUAL "lcu")
        set(FC_SYMBOL 2)
    elseif(useroptF77SYMBOL STREQUAL "uc")
        set(FC_SYMBOL 3)
    elseif(useroptF77SYMBOL STREQUAL "uc")
        set(FC_SYMBOL 1)
    else()
        message(FATAL_ERROR "Invalid value ${useroptF77SYMBOL} passed in for the F77SYMBOL variable. Use lc, uc, ucu, or lcu.")
    endif()
endif()
add_definitions(-DFC_SYMBOL=${FC_SYMBOL})

#
# Python Detection 
#
if(NOT useroptPYTHON STREQUAL EMPTY)
    set(PYTHON_EXECUTABLE ${useroptPYTHON})
    message(WARNING "Skipping Python detection and using this version: ${useroptPYTHON}")
else()
    find_package(PythonInterp REQUIRED)
endif()
set(PYCONFIG ${PYTHON_EXECUTABLE}-config)
execute_process(COMMAND ${PYCONFIG} --ldflags  OUTPUT_VARIABLE PYTHON_LIBRARIES OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${PYCONFIG} --include  OUTPUT_VARIABLE PYTHON_INCLUDE_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE)
# Turn the flags into proper lists
string(REPLACE "-I" "" PYTHON_INCLUDE_DIRS ${PYTHON_INCLUDE_DIRS})
string(REPLACE " " ";" PYTHON_INCLUDE_DIRS ${PYTHON_INCLUDE_DIRS})

##if(PYLIB STREQUAL "" OR PYINC STREQUAL "")
##   message(FATAL_ERROR "Python development libraries were not detected. Make sure that the development version "
##                       "is installed and that python-config exists in the same directory as the python executable.")
##endif()
#
#find_package(PythonLibs REQUIRED)
include_directories(${PYTHON_INCLUDE_DIRS})

#
# Boost Detection 
#
# N.B. Update boost/CMakeLists.txt if this list of components changes
set(Boost_USE_STATIC_LIBS ON)
set(Boost_COMPONENTS filesystem python regex serialization system thread )
if(NOT useroptUSE_SYSTEM_BOOST)
    message(WARNING "Skipping Boost detection, and forcing Psi to build the bundled version.")
else()
    find_package(Boost 1.47.0 COMPONENTS ${Boost_COMPONENTS})
endif()
if(NOT Boost_FOUND)
    # Add the Boost version bundled with Psi4 as a dependency
    message(WARNING "Boost not found. The pre-packaged version will be built.")
    # The version bundled with Psi4
    set(BOOSTVER boost_1_54_0)
    # Link and include information
    set(BOOSTLIBDIR ${PROJECT_BINARY_DIR}/boost/${BOOSTVER}/stage/lib)
    set(Boost_INCLUDE_DIRS ${PROJECT_BINARY_DIR}/boost/${BOOSTVER})
    foreach(component ${Boost_COMPONENTS})
        set(Boost_LIBRARIES ${Boost_LIBRARIES} "${BOOSTLIBDIR}/libboost_${component}.a")
    endforeach()
    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        set(Boost_LIBRARIES ${Boost_LIBRARIES} -lrt)
    endif()
    add_subdirectory(boost)
endif()
include_directories(${Boost_INCLUDE_DIRS})

#
# PThreads
#
find_package(Threads)
#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()

#
# The location of compiled libraries and executables
#
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

set(PSILIB mints_wrapper dfmp2 scf ccenergy ccsort psimrcc transqt2 cctriples scf_solver fock dcft lmp2 mcscf sapt dftsapt sapt_solver cchbar cclambda ccdensity transqt ccresponse detci occ mrcc fnocc cceom adc thermo functional disp thce 3index deriv_wrapper optking findif mints trans dpd chkpt iwl psio qt ciomr options moinfo util stable deriv scfgrad int util diis plugin parallel)
if(CMAKE_Fortran_COMPILER)
    set(PSILIB ${PSILIB} erd)
endif()

#
# Recursively add source directories
#
add_subdirectory(src)

#
# Echo the directory information back
#
set(RECONFIGURE_PSI "${PROJECT_BINARY_DIR}/reconfigure.sh")
message("The following command can be used to exactly reproduce this build. It can also be found in ${RECONFIGURE_PSI}:\n")
message("${CONFIGURE_CMD}\n")
configure_file(cmake/reconfigure.sh.in reconfigure.sh)

#
# Output the build settings, for plugins to read later
#
#set(buildsettingsfile ${PROJECT_BINARY_DIR}/buildsettings.cmake)
#export(TARGETS psi4 FILE ${buildsettingsfile})

#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
