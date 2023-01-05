# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
Findcppdap
---------

Find cppdap includes and library.

Imported Targets
^^^^^^^^^^^^^^^^

An :ref:`imported target <Imported targets>` named
``cppdap::cppdap`` is provided if cppdap has been found.

Result Variables
^^^^^^^^^^^^^^^^

This module defines the following variables:

``cppdap_FOUND``
  True if cppdap was found, false otherwise.
``cppdap_INCLUDE_DIRS``
  Include directories needed to include cppdap headers.
``cppdap_LIBRARIES``
  Libraries needed to link to cppdap.
``cppdap_VERSION``
  The version of cppdap found.
``cppdap_VERSION_MAJOR``
  The major version of cppdap.
``cppdap_VERSION_MINOR``
  The minor version of cppdap.
``cppdap_VERSION_PATCH``
  The patch version of cppdap.

Cache Variables
^^^^^^^^^^^^^^^

This module uses the following cache variables:

``cppdap_LIBRARY``
  The location of the cppdap library file.
``cppdap_INCLUDE_DIR``
  The location of the cppdap include directory containing ``dap.h``.

The cache variables should not be used by project code.
They may be set by end users to point at cppdap components.
#]=======================================================================]

#-----------------------------------------------------------------------------
find_library(cppdap_LIBRARY
  NAMES cppdap
  )
mark_as_advanced(cppdap_LIBRARY)

find_path(cppdap_INCLUDE_DIR
  NAMES dap/dap.h
  PATH_SUFFIXES cppdap
  )
mark_as_advanced(cppdap_INCLUDE_DIR)

#-----------------------------------------------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/../../Modules/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(cppdap
  FOUND_VAR cppdap_FOUND
  REQUIRED_VARS cppdap_LIBRARY cppdap_INCLUDE_DIR
  VERSION_VAR cppdap_VERSION
  )
set(cppdap_FOUND ${cppdap_FOUND})

#-----------------------------------------------------------------------------
# Provide documented result variables and targets.
if(cppdap_FOUND)
  set(cppdap_INCLUDE_DIRS ${cppdap_INCLUDE_DIR})
  set(cppdap_LIBRARIES ${cppdap_LIBRARY})
  if(NOT TARGET cppdap::cppdap)
    add_library(cppdap::cppdap UNKNOWN IMPORTED)
    set_target_properties(cppdap::cppdap PROPERTIES
      IMPORTED_LOCATION "${cppdap_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${cppdap_INCLUDE_DIRS}"
      )
  endif()
endif()
