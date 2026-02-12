configure_file(NoSourcePermissions.sh NoSourcePermissions.sh.out
               NO_SOURCE_PERMISSIONS)

if (CMAKE_HOST_UNIX AND NOT MSYS)
  find_program(STAT_EXECUTABLE NAMES stat)
  if(NOT STAT_EXECUTABLE)
    return()
  endif()

  # NO_SOURCE_PERMISSIONS should produce 0644 (owner rw, group r, world r).
  # Check file permissions directly using stat (consistent with
  # SourcePermissions.cmake and UseSourcePermissions.cmake) rather than
  # attempting to execute the file, which can give false results when CI
  # tooling (e.g. CodeQL) injects LD_PRELOAD libraries that alter process
  # execution.
  if (CMAKE_HOST_SYSTEM_NAME MATCHES "FreeBSD")
    execute_process(COMMAND "${STAT_EXECUTABLE}" -f %Lp "${CMAKE_CURRENT_BINARY_DIR}/NoSourcePermissions.sh.out"
      OUTPUT_VARIABLE output OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
    execute_process(COMMAND "${STAT_EXECUTABLE}" -f %A "${CMAKE_CURRENT_BINARY_DIR}/NoSourcePermissions.sh.out"
      OUTPUT_VARIABLE output OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  else()
    execute_process(COMMAND "${STAT_EXECUTABLE}" -c %a "${CMAKE_CURRENT_BINARY_DIR}/NoSourcePermissions.sh.out"
      OUTPUT_VARIABLE output OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()

  # Verify no executable bits are set (expected: 644)
  if (NOT output EQUAL "644")
    message(FATAL_ERROR "Copied file has unexpected permissions: ${output}, expected 644")
  endif()
endif()
