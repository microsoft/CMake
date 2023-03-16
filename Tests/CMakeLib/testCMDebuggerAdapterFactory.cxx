/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

#include <Windows.h>

#include <stddef.h>

#include "cmsys/Encoding.hxx"

#include "cmDebuggerAdapter.h"
#include "cmDebuggerAdapterFactory.h"
#include "cmDebuggerProtocol.h"
#include "cmStateSnapshot.h"
#include "cmVersionConfig.h"

#include "testCommon.h"

namespace cmDebugger {
extern std::shared_ptr<dap::ReaderWriter> CreateDebuggerNamedPipe(HANDLE pipe);
}

bool testCMDebuggerAdapterFactoryWindows()
{
  std::promise<bool> debuggerAdapterInitializedPromise;
  std::future<bool> debuggerAdapterInitializedFuture =
    debuggerAdapterInitializedPromise.get_future();
  std::atomic<bool> initializedEventReceived = false;
  std::atomic<bool> exitedEventReceived = false;
  std::atomic<bool> terminatedEventReceived = false;
  std::atomic<bool> threadStarted = false;
  std::atomic<bool> threadExited = false;
  std::string namedPipe = "\\\\.\\pipe\\LOCAL\\CMakeDebuggerPipe2";

  std::unique_ptr<dap::Session> client = dap::Session::create();
  client->registerHandler([&](const dap::InitializedEvent& e) {
    initializedEventReceived.store(true);
  });
  client->registerHandler(
    [&](const dap::ExitedEvent& e) { exitedEventReceived.store(true); });
  client->registerHandler([&](const dap::TerminatedEvent& e) {
    terminatedEventReceived.store(true);
  });
  client->registerHandler([&](const dap::ThreadEvent& e) {
    if (e.reason == "started") {
      threadStarted.store(true);
    } else if (e.reason == "exited") {
      threadExited.store(true);
    }
  });

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
  ASSERT_TRUE(pipe != INVALID_HANDLE_VALUE);
  std::shared_ptr<dap::ReaderWriter> client2Debugger =
    cmDebugger::CreateDebuggerNamedPipe(pipe);

  std::thread debuggerThread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::shared_ptr<cmDebugger::cmDebuggerAdapter> debuggerAdapter =
      cmDebugger::cmDebuggerAdapterFactory::CreateAdapter(namedPipe, {});

    debuggerAdapterInitializedPromise.set_value(true);
    debuggerAdapter->ReportExitCode(0);

    // Give disconnectResponse some time to be received before
    // destructing debuggerAdapter.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    return 0;
  });

  OVERLAPPED connectOverlap;
  ZeroMemory(&connectOverlap, sizeof(OVERLAPPED));
  connectOverlap.hEvent = CreateEvent(NULL,  // default security attribute
                                      TRUE,  // manual-reset event
                                      FALSE, // initial state
                                      NULL); // unnamed event object
  BOOL success = ConnectNamedPipe(pipe, &connectOverlap);
  if (!success && GetLastError() != ERROR_IO_PENDING) {
    std::cout << "Error connecting to named pipe: " << GetLastError()
              << std::endl;
    return 1;
  }

  DWORD unused;
  success = GetOverlappedResult(pipe, &connectOverlap, &unused, TRUE);
  if (!success) {
    std::cout << "Error connecting to named pipe: " << GetLastError()
              << std::endl;
  }

  client->bind(client2Debugger, client2Debugger);

  dap::CMakeInitializeRequest initializeRequest;
  auto response = client->send(initializeRequest);
  auto initializeResponse = response.get();
  ASSERT_TRUE(!initializeResponse.error);
  ASSERT_TRUE(initializeResponse.response.cmakeVersion.full == CMake_VERSION);
  ASSERT_TRUE(initializeResponse.response.cmakeVersion.major ==
              CMake_VERSION_MAJOR);
  ASSERT_TRUE(initializeResponse.response.cmakeVersion.minor ==
              CMake_VERSION_MINOR);
  ASSERT_TRUE(initializeResponse.response.cmakeVersion.patch ==
              CMake_VERSION_PATCH);
  ASSERT_TRUE(initializeResponse.response.supportsExceptionInfoRequest);
  ASSERT_TRUE(
    initializeResponse.response.exceptionBreakpointFilters.has_value());
  dap::LaunchRequest launchRequest;
  auto launchResponse = client->send(launchRequest).get();
  ASSERT_TRUE(!launchResponse.error);

  dap::ConfigurationDoneRequest configurationDoneRequest;
  auto configurationDoneResponse =
    client->send(configurationDoneRequest).get();
  ASSERT_TRUE(!configurationDoneResponse.error);

  debuggerAdapterInitializedFuture.get();

  ASSERT_TRUE(initializedEventReceived.load());

  dap::DisconnectRequest disconnectRequest;
  auto disconnectResponse = client->send(disconnectRequest).get();
  ASSERT_TRUE(!disconnectResponse.error);

  ASSERT_TRUE(threadStarted.load());
  ASSERT_TRUE(threadExited.load());
  ASSERT_TRUE(exitedEventReceived.load());
  ASSERT_TRUE(terminatedEventReceived.load());

  debuggerThread.join();
  return true;
}

int testCMDebuggerAdapterFactory(int, char*[])
{
  return runTests(std::vector<std::function<bool()>>{
    testCMDebuggerAdapterFactoryWindows,
  });
}
