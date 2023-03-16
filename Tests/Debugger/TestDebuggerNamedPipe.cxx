/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#ifdef _WIN32
#  include <Windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include <cm3p/cppdap/io.h>

#include "cmsys/Encoding.hxx"

#include "cmSystemTools.h"
namespace cmDebugger {
extern std::shared_ptr<dap::ReaderWriter> CreateDebuggerNamedPipe(HANDLE pipe);
}

static void sendCommands(std::shared_ptr<dap::ReaderWriter> const& debugger,
                         int delayMs,
                         std::vector<std::string> const& initCommands)
{
  for (auto& command : initCommands) {
    std::string contentLength = "Content-Length:";
    contentLength += std::to_string(command.size()) + "\r\n\r\n";
    debugger->write(contentLength.c_str(), contentLength.size());
    if (!debugger->write(command.c_str(), command.size())) {
      std::cout << "debugger write error: " << GetLastError() << std::endl;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
  }
}

/** \brief Test CMake debugger named pipe.
 *
 * Test CMake debugger named pipe by
 * 1. Create a named pipe for DAP traffic between the client and the debugger.
 * 2. Create a client thread to wait for the debugger connection.
 *    - Once the debugger is connected, send the minimum required commands to
 *      get debugger going.
 *    - Wait for the CMake to complete the cache generation
 *    - Send the disconnect command.
 *    - Read and store the debugger's responses for validation.
 * 3. Run the CMake command with debugger on and wait for it to complete.
 * 4. Validate the response to ensure we are getting the expected responses.
 *
 */
int main(int argc, char* argv[])
{
  if (argc < 5) {
    std::cout
      << "Usage: TestDebuggerNamedPipe <NamePipe> <CMakePath> <SourceFolder> "
         "<OutputFolder> <TimeoutMs>"
      << std::endl;
    return 1;
  }

  std::string namedPipe = argv[1];
  std::string cmakeCommand = std::string(argv[2]) +
    " --debugger --debugger-pipe " + namedPipe + " " + argv[3];

  int timeout = std::stoi(argv[5]);
  if (!(CreateDirectory(argv[4], NULL) ||
        ERROR_ALREADY_EXISTS == GetLastError())) {
    std::cout << "Error creating output folder " << GetLastError()
              << std::endl;
    return -1;
  }

  HANDLE pipe = CreateNamedPipeW(
    cmsys::Encoding::ToWide(namedPipe.c_str()).c_str(), // pipe name
    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,          // pipe mode
    PIPE_TYPE_BYTE | PIPE_WAIT,                         // blocking mode
    1,         // max number of instances
    1024 * 16, // output buffer size
    1024 * 16, // input buffer size
    0,         // default timeout
    NULL       // default security attributes
  );

  if (pipe == INVALID_HANDLE_VALUE) {
    std::cout << "Error creating named pipe" << std::endl;
    return -1;
  }

  // Capture debugger response stream.
  std::stringstream debuggerResponseStream;

  // Start the debugger client process.
  std::thread clientThread([&]() {
    std::cout << "Waiting for debugger connection" << std::endl;
    if (ConnectNamedPipe(pipe, NULL) == FALSE) {
      std::cout << "Error connecting to named pipe: " << GetLastError()
                << std::endl;
      return -1;
    }

    std::shared_ptr<dap::ReaderWriter> debugger =
      cmDebugger::CreateDebuggerNamedPipe(pipe);

    // Send init commands to get debugger going.
    sendCommands(
      debugger, 400,
      { "{\"arguments\":{\"adapterID\":\"\"},\"command\":\"initialize\","
        "\"seq\":"
        "1,\"type\":\"request\"}",
        "{\"arguments\":{},\"command\":\"launch\",\"seq\":2,\"type\":"
        "\"request\"}",
        "{\"arguments\":{},\"command\":\"configurationDone\",\"seq\":3,"
        "\"type\":"
        "\"request\"}" });

    // Wait until CMake generation complete.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Send disconnect command.
    sendCommands(
      debugger, 200,
      { "{\"arguments\":{},\"command\":\"disconnect\",\"seq\":4,\"type\":"
        "\"request\"}" });

    // Read response from the debugger.
    while (true) {
      char buffer[1];
      if (debugger->read(buffer, 1) != 1) {
        std::cout << "debugger read error: " << GetLastError() << std::endl;
        break;
      }
      debuggerResponseStream << buffer[0];
    }

    debugger->close();
    CloseHandle(pipe);

    return 0;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  std::cout << "Running command: " << cmakeCommand << std::endl;

  // Start the CMake process.
  if (!CreateProcessW(
        NULL, // No module name (use command line)
        cmsys::Encoding::ToWide(cmakeCommand.c_str()).data(), // Command line
        NULL,  // Process handle not inheritable
        NULL,  // Thread handle not inheritable
        FALSE, // Set handle inheritance to FALSE
        0,     // No creation flags
        NULL,  // Use parent's environment block
        cmsys::Encoding::ToWide(argv[4])
          .data(), // Use parent's starting directory
        &si,       // Pointer to STARTUPINFO structure
        &pi)       // Pointer to PROCESS_INFORMATION structure
  ) {
    std::cout << "Error running command " << GetLastError() << std::endl;
    return -1;
  }

  // Wait until CMake process exits.
  WaitForSingleObject(pi.hProcess, timeout);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  clientThread.join();

  auto debuggerResponse = debuggerResponseStream.str();

  std::vector<std::string> expectedResponses = {
    "\"event\":\"initialized\".*\"type\":\"event\"",
    "\"command\":\"launch\".*\"success\":true.*\"type\":\"response\"",
    "\"command\":\"configurationDone\".*\"success\":true.*\"type\":"
    "\"response\"",
    "\"reason\":\"started\".*\"threadId\":1.*\"event\":\"thread\".*"
    "\"type\":\"event\"",
    "\"reason\":\"exited\".*\"threadId\":1.*\"event\":\"thread\".*"
    "\"type\":\"event\"",
    "\"exitCode\":0.*\"event\":\"exited\".*\"type\":\"event\"",
    "\"command\":\"disconnect\".*\"success\":true.*\"type\":\"response\""
  };

  for (auto& regexString : expectedResponses) {
    std::regex regex(regexString);
    if (!std::regex_search(debuggerResponse, regex)) {
      std::cout << "Expected response not found: " << regexString << std::endl;
      std::cout << debuggerResponse << std::endl;
      return -1;
    }
  }

  return 0;
}
