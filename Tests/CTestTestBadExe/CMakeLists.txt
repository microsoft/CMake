cmake_minimum_required(VERSION 3.10)
project(CTestTestBadExe)
include(CTest)

# Use a file without an extension to avoid Windows file associations
# (e.g. .txt -> Notepad) which can cause the test to hang instead of
# reporting BAD_COMMAND.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/notAnExe" "${CMAKE_CURRENT_BINARY_DIR}/notAnExe" COPYONLY)

add_test (TestBadExe "${CMAKE_CURRENT_BINARY_DIR}/notAnExe")
