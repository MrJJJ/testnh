# CMake script for TestNH package
# Authors:
#   Julien Dutheil
#   Francois Gindraud (2017)
# Created: 07/04/2010

cmake_minimum_required (VERSION 2.8.11)
project (testnh CXX)

# Compile options
set (CMAKE_CXX_FLAGS "-std=c++11 -Wall -Weffc++ -Wshadow -Wconversion")

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
ENDIF(NOT CMAKE_BUILD_TYPE)

IF(NOT NO_DEP_CHECK)
  SET(NO_DEP_CHECK FALSE CACHE BOOL
      "Disable dependencies check for building distribution only."
      FORCE)
ENDIF(NOT NO_DEP_CHECK)

IF(NO_DEP_CHECK)
  MESSAGE(STATUS "Dependencies checking disabled. Only distribution can be built.")
ELSE(NO_DEP_CHECK)

#static linkage?
IF(NOT BUILD_STATIC)
  SET(BUILD_STATIC FALSE CACHE BOOL
      "Enable static linkage."
      FORCE)
ENDIF()
IF(BUILD_STATIC)
  MESSAGE(STATUS "Static linkage requested.")
  set (CMAKE_CXX_FLAGS "-static -static-libgcc ${CMAKE_CXX_FLAGS}")
ENDIF()

# Check compression program
# COMPRESS_PROGRAM controls the choice of program
# COMPRESS_EXT can be used to override the file extension
if (NOT COMPRESS_PROGRAM)
  set (COMPRESS_PROGRAM gzip CACHE STRING "Set program for compressing documentation" FORCE)
endif ()
find_program (COMPRESS_BIN NAMES ${COMPRESS_PROGRAM} DOC "${COMPRESS_PROGRAM} compression program")
if (NOT COMPRESS_BIN)
  message (STATUS "${COMPRESS_PROGRAM} program not found, text doc will not be compressed")
else ()
  # Deduce COMPRESS_EXT for known compression programs if not set
  if (NOT COMPRESS_EXT)
    if (${COMPRESS_PROGRAM} STREQUAL "gzip")
      set (COMPRESS_EXT "gz")
    elseif (${COMPRESS_PROGRAM} STREQUAL "bzip2")
      set (COMPRESS_EXT "bz2")
    else ()
      set (COMPRESS_EXT "${COMPRESS_PROGRAM}") # Default: program name (works for xz/lzma)
    endif ()
  endif ()
  # Generate command line args (always add -c to output compressed file to stdout)
  if (${COMPRESS_PROGRAM} STREQUAL "gzip")
    # -n for no timestamp in files (reproducible builds)
    set (COMPRESS_ARGS -c -n)
  else ()
    set (COMPRESS_ARGS -c)
  endif ()
  message (STATUS "Found ${COMPRESS_BIN} compression program, using file extension .${COMPRESS_EXT}")
endif ()

# Find dependencies (add install directory to search)
if (CMAKE_INSTALL_PREFIX)
  set (CMAKE_PREFIX_PATH "${CMAKE_INSTALL_PREFIX}" ${CMAKE_PREFIX_PATH})
endif (CMAKE_INSTALL_PREFIX)

include (GNUInstallDirs)
find_package (bpp-phyl 12.0.0 REQUIRED)

# Subdirectories
add_subdirectory (TestNH)
add_subdirectory (doc)
add_subdirectory (man)

ENDIF(NO_DEP_CHECK)

# Packager
SET(CPACK_PACKAGE_NAME "testnh")
SET(CPACK_PACKAGE_VENDOR "Bastien Boussau and Julien Dutheil")
SET(CPACK_PACKAGE_VERSION "1.2.0")
SET(CPACK_PACKAGE_VERSION_MAJOR "1")
SET(CPACK_PACKAGE_VERSION_MINOR "2")
SET(CPACK_PACKAGE_VERSION_PATCH "0")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Non-homogeneous model testing")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
SET(CPACK_RESOURCE_FILE_AUTHORS "${CMAKE_SOURCE_DIR}/AUTHORS")
SET(CPACK_RESOURCE_FILE_INSTALL "${CMAKE_SOURCE_DIR}/INSTALL")
SET(CPACK_SOURCE_GENERATOR "TGZ")
# /!\ This assumes that an external build is used
SET(CPACK_SOURCE_IGNORE_FILES 
       "/build/" 
       "/\\\\.git/" 
       "/\\\\.gitignore" 
       ${CPACK_SOURCE_IGNORE_FILES}
       )

SET(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
SET(CPACK_DEBSOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}_${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}.orig")
INCLUDE(CPack)

#This adds the 'dist' target
ADD_CUSTOM_TARGET(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)

IF(NOT NO_DEP_CHECK)
IF (UNIX)
#This creates deb packages:
ADD_CUSTOM_TARGET(origdist COMMAND cp ${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz ../${CPACK_DEBSOURCE_PACKAGE_FILE_NAME}.tar.gz)
ADD_DEPENDENCIES(origdist dist)
ADD_CUSTOM_TARGET(deb dpkg-buildpackage -uc -us -i${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz)
ADD_DEPENDENCIES(deb origdist)
ADD_DEPENDENCIES(deb info man)

#This creates rpm packages:
ADD_CUSTOM_TARGET(rpm rpmbuild -ta ${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz)
ADD_DEPENDENCIES(rpm dist info man)

ENDIF(UNIX)

ENDIF(NOT NO_DEP_CHECK)
