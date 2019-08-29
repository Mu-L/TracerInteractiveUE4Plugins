# This is the CMakeCache file.

########################
# EXTERNAL cache entries
########################

set (CMAKE_INSTALL_FRAMEWORK_PREFIX "Library/Frameworks" CACHE STRING "Frameworks installation directory" FORCE)

set (HDF_PACKAGE_EXT "" CACHE STRING "Name of HDF package extension" FORCE)

set (HDF5_BUILD_FORTRAN ON CACHE BOOL "Build FORTRAN support" FORCE)

set (HDF5_BUILD_GENERATORS OFF CACHE BOOL "Build Test Generators" FORCE)

set (HDF5_ENABLE_Z_LIB_SUPPORT ON CACHE BOOL "Enable Zlib Filters" FORCE)

set (HDF5_ENABLE_SZIP_SUPPORT ON CACHE BOOL "Use SZip Filter" FORCE)

set (HDF5_ENABLE_SZIP_ENCODING ON CACHE BOOL "Use SZip Encoding" FORCE)

set (HDF5_ENABLE_HSIZET ON CACHE BOOL "Enable datasets larger than memory" FORCE)

set (ALLOW_UNSUPPORTED OFF CACHE BOOL "Enable unsupported combinations of configuration options" FORCE)

set (HDF5_ENABLE_DEPRECATED_SYMBOLS ON CACHE BOOL "Enable deprecated public API symbols" FORCE)

set (HDF5_ENABLE_DIRECT_VFD OFF CACHE BOOL "Build the Direct I/O Virtual File Driver" FORCE)

set (HDF5_ENABLE_PARALLEL OFF CACHE BOOL "Enable parallel build (requires MPI)" FORCE)

set (MPIEXEC_MAX_NUMPROCS "3" CACHE STRING "Minimum number of processes for HDF parallel tests" FORCE)

set (HDF5_BUILD_PARALLEL_ALL OFF CACHE BOOL "Build Parallel Programs" FORCE)

set (HDF5_ENABLE_COVERAGE OFF CACHE BOOL "Enable code coverage for Libraries and Programs" FORCE)

set (HDF5_ENABLE_USING_MEMCHECKER OFF CACHE BOOL "Indicate that a memory checker is used" FORCE)

set (HDF5_DISABLE_COMPILER_WARNINGS OFF CACHE BOOL "Disable compiler warnings" FORCE)

set (HDF5_USE_FOLDERS ON CACHE BOOL "Enable folder grouping of projects in IDEs." FORCE)

set (HDF5_USE_16_API_DEFAULT OFF CACHE BOOL "Use the HDF5 1.6.x API by default" FORCE)

set (HDF5_ENABLE_THREADSAFE OFF CACHE BOOL "(WINDOWS)Enable Threadsafety" FORCE)

set (HDF_TEST_EXPRESS "2" CACHE STRING "Control testing framework (0-3)" FORCE)

set (HDF5_PACKAGE_EXTLIBS OFF CACHE BOOL "(WINDOWS)CPACK - include external libraries" FORCE)

set (HDF5_NO_PACKAGES OFF CACHE BOOL "CPACK - Disable packaging" FORCE)

set (HDF5_ALLOW_EXTERNAL_SUPPORT "NO" CACHE STRING "Allow External Library Building (NO GIT SVN TGZ)" FORCE)
set_property (CACHE HDF5_ALLOW_EXTERNAL_SUPPORT PROPERTY STRINGS NO GIT SVN TGZ)

set (ZLIB_TGZ_NAME "ZLib.tar.gz" CACHE STRING "Use ZLib from compressed file" FORCE)

set (SZIP_TGZ_NAME "SZip.tar.gz" CACHE STRING "Use SZip from compressed file" FORCE)

set (ZLIB_PACKAGE_NAME "zlib" CACHE STRING "Name of ZLIB package" FORCE)

set (SZIP_PACKAGE_NAME "szip" CACHE STRING "Name of SZIP package" FORCE)
