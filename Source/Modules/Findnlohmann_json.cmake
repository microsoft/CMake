# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindNLohmannJson
----------------

Find nlohmann_json includes and library.

Imported Targets
^^^^^^^^^^^^^^^^

An :ref:`imported target <Imported targets>` named
``nlohmann_json::nlohmann_json`` is provided if nlohmann_json has been found.

Result Variables
^^^^^^^^^^^^^^^^

This module defines the following variables:

``nlohmann_json_FOUND``
  True if nlohmann_json was found, false otherwise.
``nlohmann_json_INCLUDE_DIRS``
  Include directories needed to include nlohmann_json headers.
``nlohmann_json_LIBRARIES``
  Libraries needed to link to nlohmann_json.
``nlohmann_json_VERSION``
  The version of nlohmann_json found.
``nlohmann_json_VERSION_MAJOR``
  The major version of nlohmann_json.
``nlohmann_json_VERSION_MINOR``
  The minor version of nlohmann_json.
``nlohmann_json_VERSION_PATCH``
  The patch version of nlohmann_json.

Cache Variables
^^^^^^^^^^^^^^^

This module uses the following cache variables:

``nlohmann_json_LIBRARY``
  The location of the nlohmann_json library file.
``nlohmann_json_INCLUDE_DIR``
  The location of the nlohmann_json include directory containing ``nlohmann/json.hpp``.

The cache variables should not be used by project code.
They may be set by end users to point at nlohmann_json components.
#]=======================================================================]

#-----------------------------------------------------------------------------
find_library(nlohmann_json_LIBRARY
  NAMES nlohmann_json
  )
mark_as_advanced(nlohmann_json_LIBRARY)

find_path(nlohmann_json_INCLUDE_DIR
  NAMES nlohmann/json.hpp
  )
mark_as_advanced(nlohmann_json_INCLUDE_DIR)

#-----------------------------------------------------------------------------
# Extract version number if possible.
set(_nlohmann_json_H_REGEX "^// |  |  |__   |  |  | | | |  version[ \t]+\"(([0-9]+)\\.([0-9]+)\\.([0-9]+)[^\"]*)\".*$")
if(nlohmann_json_INCLUDE_DIR AND EXISTS "${nlohmann_json_INCLUDE_DIR}/nlohmann/json.hpp")
  file(STRINGS "${nlohmann_json_INCLUDE_DIR}/nlohmann/json.hpp" _nlohmann_json_H REGEX "${_nlohmann_json_H_REGEX}")
else()
  set(_nlohmann_json_H "")
endif()
if(_nlohmann_json_H MATCHES "${_nlohmann_json_H_REGEX}")
  set(nlohmann_json_VERSION_STRING "${CMAKE_MATCH_1}")
  set(nlohmann_json_VERSION_MAJOR "${CMAKE_MATCH_2}")
  set(nlohmann_json_VERSION_MINOR "${CMAKE_MATCH_3}")
  set(nlohmann_json_VERSION_PATCH "${CMAKE_MATCH_4}")
else()
  set(nlohmann_json_VERSION_STRING "")
  set(nlohmann_json_VERSION_MAJOR "")
  set(nlohmann_json_VERSION_MINOR "")
  set(nlohmann_json_VERSION_PATCH "")
endif()
unset(_nlohmann_json_H_REGEX)
unset(_nlohmann_json_H)

#-----------------------------------------------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/../../Modules/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(nlohmann_json
  FOUND_VAR nlohmann_json_FOUND
  REQUIRED_VARS nlohmann_json_LIBRARY nlohmann_json_INCLUDE_DIR
  VERSION_VAR nlohmann_json_VERSION_STRING
  )
set(nlohmann_json_FOUND ${nlohmann_json_FOUND})

#-----------------------------------------------------------------------------
# Provide documented result variables and targets.
if(nlohmann_json_FOUND)
  set(nlohmann_json_INCLUDE_DIRS ${nlohmann_json_INCLUDE_DIR})
  set(nlohmann_json_LIBRARIES ${nlohmann_json_LIBRARY})
  if(NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json UNKNOWN IMPORTED)
    set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
      IMPORTED_LOCATION "${nlohmann_json_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${nlohmann_json_INCLUDE_DIRS}"
      )
  endif()
endif()
