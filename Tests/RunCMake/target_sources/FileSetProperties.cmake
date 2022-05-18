enable_language(C)

function(assert_prop_undef tgt prop)
  unset(actual_value)
  get_property(actual_value TARGET ${tgt} PROPERTY ${prop})
  if(DEFINED actual_value)
    message(SEND_ERROR "${prop} should be undefined, actual value:\n  ${actual_value}")
  endif()
endfunction()

function(assert_prop_eq tgt prop value)
  unset(actual_value)
  get_property(actual_value TARGET ${tgt} PROPERTY ${prop})
  if(NOT actual_value STREQUAL value)
    message(SEND_ERROR "Expected value of ${prop}:\n  ${value}\nActual value:\n  ${actual_value}")
  endif()
endfunction()

add_library(lib1 STATIC empty.c)
assert_prop_eq(lib1 HEADER_SETS "")
assert_prop_eq(lib1 INTERFACE_HEADER_SETS "")
assert_prop_undef(lib1 INCLUDE_DIRECTORIES)
assert_prop_undef(lib1 INTERFACE_INCLUDE_DIRECTORIES)

target_sources(lib1 PUBLIC FILE_SET a TYPE HEADERS BASE_DIRS "." FILES h1.h h2.h)
assert_prop_eq(lib1 HEADER_SETS "a")
assert_prop_eq(lib1 INTERFACE_HEADER_SETS "a")
assert_prop_eq(lib1 HEADER_DIRS_a "${CMAKE_CURRENT_SOURCE_DIR}/.")
assert_prop_eq(lib1 HEADER_SET_a "${CMAKE_CURRENT_SOURCE_DIR}/h1.h;${CMAKE_CURRENT_SOURCE_DIR}/h2.h")
assert_prop_eq(lib1 INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>")
assert_prop_eq(lib1 INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>")

target_sources(lib1 PUBLIC FILE_SET a FILES h3.h)
assert_prop_eq(lib1 HEADER_SETS "a")
assert_prop_eq(lib1 INTERFACE_HEADER_SETS "a")
assert_prop_eq(lib1 HEADER_DIRS_a "${CMAKE_CURRENT_SOURCE_DIR}/.")
assert_prop_eq(lib1 HEADER_SET_a "${CMAKE_CURRENT_SOURCE_DIR}/h1.h;${CMAKE_CURRENT_SOURCE_DIR}/h2.h;${CMAKE_CURRENT_SOURCE_DIR}/h3.h")
assert_prop_eq(lib1 INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>")
assert_prop_eq(lib1 INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>")

target_sources(lib1 PRIVATE FILE_SET b TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/dir" FILES dir/dir.h)
assert_prop_eq(lib1 HEADER_SETS "a;b")
assert_prop_eq(lib1 INTERFACE_HEADER_SETS "a")
assert_prop_eq(lib1 HEADER_DIRS_b "${CMAKE_CURRENT_SOURCE_DIR}/dir")
assert_prop_eq(lib1 HEADER_SET_b "${CMAKE_CURRENT_SOURCE_DIR}/dir/dir.h")
assert_prop_eq(lib1 INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dir>")
assert_prop_eq(lib1 INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>")

target_sources(lib1 INTERFACE FILE_SET c TYPE HEADERS FILE_SET d TYPE HEADERS)
assert_prop_eq(lib1 HEADER_SETS "a;b")
assert_prop_eq(lib1 INTERFACE_HEADER_SETS "a;c;d")
assert_prop_eq(lib1 HEADER_DIRS_c "${CMAKE_CURRENT_SOURCE_DIR}")
assert_prop_eq(lib1 HEADER_SET_c "")
assert_prop_eq(lib1 HEADER_DIRS_d "${CMAKE_CURRENT_SOURCE_DIR}")
assert_prop_eq(lib1 HEADER_SET_d "")
assert_prop_eq(lib1 INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dir>")
assert_prop_eq(lib1 INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

target_sources(lib1 PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES h1.h)
assert_prop_eq(lib1 INTERFACE_HEADER_SETS "HEADERS;a;c;d")
assert_prop_eq(lib1 HEADER_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
assert_prop_eq(lib1 HEADER_SET "${CMAKE_CURRENT_SOURCE_DIR}/h1.h")
assert_prop_eq(lib1 INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dir>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
assert_prop_eq(lib1 INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

target_sources(lib1 PUBLIC FILE_SET HEADERS FILES h2.h)
assert_prop_eq(lib1 INTERFACE_HEADER_SETS "HEADERS;a;c;d")
assert_prop_eq(lib1 HEADER_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
assert_prop_eq(lib1 HEADER_SET "${CMAKE_CURRENT_SOURCE_DIR}/h1.h;${CMAKE_CURRENT_SOURCE_DIR}/h2.h")
assert_prop_eq(lib1 INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dir>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
assert_prop_eq(lib1 INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/.>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>;$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
