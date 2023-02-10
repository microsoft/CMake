project ("Debugger")

add_executable (TestDebuggerNamedPipe "TestDebuggerNamedPipe.cxx" )
target_link_libraries(TestDebuggerNamedPipe CMakeLib CTestLib)


if(DEFINED CMAKE_BUILD_TYPE)
  set(SourceDir "${CMAKE_CURRENT_SOURCE_DIR}/SampleProject")
  set(BinDir "${CMAKE_CURRENT_BINARY_DIR}/SampleProject")
  set(NamePipe "\\\\\\\\.\\\\pipe\\\\LOCAL\\\\CMakeDebuggerPipe1")

  # It seems that ${CMAKE_CMAKE_COMMAND}, when passing as an argument to a test,
  # is not properly expanded with multi-config generators for some reason. As a
  # result, ${CMAKE_CMAKE_COMMAND} isn't pointing to a valid location, i.e. missing
  # the build-config in the path.
  # As a workaroudn, we will add this test for single-configuration generators only.
  add_test(TestDebuggerNamedPipe
    ${CMAKE_CURRENT_BINARY_DIR}/TestDebuggerNamedPipe.exe ${NamePipe}
    ${CMAKE_CMAKE_COMMAND} ${SourceDir} ${BinDir} 5000
    )
  set_tests_properties(TestDebuggerNamedPipe PROPERTIES TIMEOUT 5)
endif()
