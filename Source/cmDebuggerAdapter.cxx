/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include "cmConfigure.h" // IWYU pragma: keep

#include "cmDebuggerAdapter.h"

#include <climits>
#include <cstring>
#include <iostream>
#include <sstream>

#include "cmDebuggerBreakpointManager.h"
#include "cmDebuggerExceptionManager.h"
#include "cmDebuggerProtocol.h"
#include "cmDebuggerStackFrame.h"
#include "cmDebuggerThread.h"
#include "cmDebuggerThreadManager.h"
#include "cmListFileCache.h"
#include "cmMakefile.h"
#include "cmVersionConfig.h"
#include "cmake.h"

namespace cmDebugger {

// Event provides a basic wait and signal synchronization primitive.
class SyncEvent
{
public:
  // Wait() blocks until the event is fired.
  void Wait()
  {
    std::unique_lock<std::mutex> lock(Mutex);
    Cv.wait(lock, [&] { return Fired; });
  }

  // Fire() sets signals the event, and unblocks any calls to Wait().
  void Fire()
  {
    std::unique_lock<std::mutex> lock(Mutex);
    Fired = true;
    Cv.notify_all();
  }

private:
  std::mutex Mutex;
  std::condition_variable Cv;
  bool Fired = false;
};

class Semaphore
{
public:
  Semaphore(int count_ = 0)
    : Count(count_)
  {
  }

  inline void Notify()
  {
    std::unique_lock<std::mutex> lock(Mutex);
    Count++;
    // notify the waiting thread
    Cv.notify_one();
  }

  inline void Wait()
  {
    std::unique_lock<std::mutex> lock(Mutex);
    while (Count == 0) {
      // wait on the mutex until notify is called
      Cv.wait(lock);
    }
    Count--;
  }

private:
  std::mutex Mutex;
  std::condition_variable Cv;
  int Count;
};

cmDebuggerAdapter::cmDebuggerAdapter(
  std::shared_ptr<dap::Reader> const& reader,
  std::shared_ptr<dap::Writer> const& writer, std::string const& dapLogPath)
  : ThreadManager(std::make_unique<cmDebuggerThreadManager>())
  , SessionActive(true)
  , DisconnectEvent(std::make_unique<SyncEvent>())
  , ConfigurationDoneEvent(std::make_unique<SyncEvent>())
  , ContinueSem(std::make_unique<Semaphore>())
{
  if (!dapLogPath.empty()) {
    SessionLog = dap::file(dapLogPath.c_str());
  }
  ClearStepRequests();

  Session = dap::Session::create();
  BreakpointManager =
    std::make_unique<cmDebuggerBreakpointManager>(Session.get());
  ExceptionManager =
    std::make_unique<cmDebuggerExceptionManager>(Session.get());

  // Handle errors reported by the Session. These errors include protocol
  // parsing errors and receiving messages with no handler.
  Session->onError([this](const char* msg) {
    if (SessionLog) {
      dap::writef(SessionLog, "dap::Session error: %s\n", msg);
    }

    std::cout << "[CMake Debugger] DAP session error: " << msg << std::endl;

    BreakpointManager->ClearAll();
    ExceptionManager->ClearAll();
    ClearStepRequests();
    ContinueSem->Notify();
    DisconnectEvent->Fire();
    SessionActive.store(false);
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Initialize
  Session->registerHandler([this](const dap::CMakeInitializeRequest& req) {
    SupportsVariableType = req.supportsVariableType.value(false);
    dap::CMakeInitializeResponse response;
    response.supportsConfigurationDoneRequest = true;
    response.cmakeVersion.major = CMake_VERSION_MAJOR;
    response.cmakeVersion.minor = CMake_VERSION_MINOR;
    response.cmakeVersion.patch = CMake_VERSION_PATCH;
    response.cmakeVersion.full = CMake_VERSION;
    ExceptionManager->HandleInitializeRequest(response);
    return response;
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Events_Initialized
  Session->registerSentHandler(
    [&](const dap::ResponseOrError<dap::CMakeInitializeResponse>&) {
      Session->send(dap::InitializedEvent());
    });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Threads
  Session->registerHandler([this](const dap::ThreadsRequest& req) {
    std::unique_lock<std::mutex> lock(Mutex);
    dap::ThreadsResponse response;
    dap::Thread thread;
    thread.id = DefaultThread->GetId();
    thread.name = DefaultThread->GetName();
    response.threads.push_back(thread);
    return response;
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StackTrace
  Session->registerHandler([this](const dap::StackTraceRequest& request)
                             -> dap::ResponseOrError<dap::StackTraceResponse> {
    std::unique_lock<std::mutex> lock(Mutex);

    cm::optional<dap::StackTraceResponse> response =
      ThreadManager->GetThreadStackTraceResponse(request.threadId);
    if (response.has_value()) {
      return response.value();
    }

    return dap::Error("Unknown threadId '%d'", int(request.threadId));
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Scopes
  Session->registerHandler([this](const dap::ScopesRequest& request)
                             -> dap::ResponseOrError<dap::ScopesResponse> {
    std::unique_lock<std::mutex> lock(Mutex);
    return DefaultThread->GetScopesResponse(request.frameId,
                                            SupportsVariableType);
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Variables
  Session->registerHandler([this](const dap::VariablesRequest& request)
                             -> dap::ResponseOrError<dap::VariablesResponse> {
    return DefaultThread->GetVariablesResponse(request);
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Pause
  Session->registerHandler([this](const dap::PauseRequest& req) {
    PauseRequest.store(true);
    return dap::PauseResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Continue
  Session->registerHandler([this](const dap::ContinueRequest& req) {
    ContinueSem->Notify();
    return dap::ContinueResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Next
  Session->registerHandler([this](const dap::NextRequest& req) {
    NextStepFrom.store(DefaultThread->GetStackFrameSize());
    ContinueSem->Notify();
    return dap::NextResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepIn
  Session->registerHandler([this](const dap::StepInRequest& req) {
    // This would stop after stepped in, single line stepped or stepped out.
    StepInRequest.store(true);
    ContinueSem->Notify();
    return dap::StepInResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepOut
  Session->registerHandler([this](const dap::StepOutRequest& req) {
    StepOutDepth.store(DefaultThread->GetStackFrameSize() - 1);
    ContinueSem->Notify();
    return dap::StepOutResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Launch
  Session->registerHandler(
    [](const dap::LaunchRequest& req) { return dap::LaunchResponse(); });

  // Handler for disconnect requests
  Session->registerHandler([this](const dap::DisconnectRequest& request) {
    BreakpointManager->ClearAll();
    ExceptionManager->ClearAll();
    ClearStepRequests();
    ContinueSem->Notify();
    DisconnectEvent->Fire();
    SessionActive.store(false);
    return dap::DisconnectResponse();
  });

  Session->registerHandler([this](const dap::EvaluateRequest& request) {
    dap::EvaluateResponse response;
    if (request.frameId.has_value()) {
      std::shared_ptr<cmDebuggerStackFrame> frame =
        DefaultThread->GetStackFrame(request.frameId.value());

      auto var = frame->GetMakefile()->GetDefinition(request.expression);
      if (var) {
        response.type = "string";
        response.result = var->c_str();
        return response;
      }
    }

    return response;
  });

  // The ConfigurationDone request is made by the client once all configuration
  // requests have been made.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ConfigurationDone
  Session->registerHandler([this](const dap::ConfigurationDoneRequest& req) {
    ConfigurationDoneEvent->Fire();
    return dap::ConfigurationDoneResponse();
  });

  // Connect to the client.
  if (SessionLog) {
    Session->connect(spy(reader, SessionLog), spy(writer, SessionLog));
  } else {
    Session->connect(reader, writer);
  }

  // Start the processing thread.
  SessionThread = std::thread([this] {
    while (SessionActive.load()) {
      if (auto payload = Session->getPayload()) {
        payload();
      }
    }
  });

  ConfigurationDoneEvent->Wait();

  DefaultThread = ThreadManager->StartThread("CMake script");
  dap::ThreadEvent threadEvent;
  threadEvent.reason = "started";
  threadEvent.threadId = DefaultThread->GetId();
  Session->send(threadEvent);
}

cmDebuggerAdapter::~cmDebuggerAdapter()
{
  if (SessionThread.joinable()) {
    SessionThread.join();
  }

  Session.reset(nullptr);

  if (SessionLog) {
    SessionLog->close();
  }
}

void cmDebuggerAdapter::ReportExitCode(int exitCode)
{
  ThreadManager->EndThread(DefaultThread);
  dap::ThreadEvent threadEvent;
  threadEvent.reason = "exited";
  threadEvent.threadId = DefaultThread->GetId();
  DefaultThread.reset();

  dap::ExitedEvent exitEvent;
  exitEvent.exitCode = exitCode;

  dap::TerminatedEvent terminatedEvent;

  if (SessionActive.load()) {
    Session->send(threadEvent);
    Session->send(exitEvent);
    Session->send(terminatedEvent);
  }

  // Wait until disconnected or error.
  DisconnectEvent->Wait();
}

void cmDebuggerAdapter::SourceFileLoaded(
  std::string const& sourcePath,
  std::vector<cmListFileFunction> const& functions)
{
  BreakpointManager->SourceFileLoaded(sourcePath, functions);
}

void cmDebuggerAdapter::BeginFunction(cmMakefile* mf,
                                      std::string const& sourcePath,
                                      cmListFileFunction const& lff)
{
  std::unique_lock<std::mutex> lock(Mutex);
  DefaultThread->PushStackFrame(mf, sourcePath, lff);

  if (lff.Line() == 0) {
    // File just loaded, continue to first valid function call.
    return;
  }

  auto hits = BreakpointManager->GetBreakpoints(sourcePath, lff.Line());
  lock.unlock();

  bool waitSem = false;
  dap::StoppedEvent stoppedEvent;
  stoppedEvent.allThreadsStopped = true;
  stoppedEvent.threadId = DefaultThread->GetId();
  if (hits.size() > 0) {
    ClearStepRequests();
    waitSem = true;

    dap::array<dap::integer> hitBreakpoints;
    hitBreakpoints.resize(hits.size());
    std::transform(hits.begin(), hits.end(), hitBreakpoints.begin(),
                   [&](const auto& id) { return dap::integer(id); });
    stoppedEvent.reason = "breakpoint";
    stoppedEvent.hitBreakpointIds = hitBreakpoints;
  }

  if (long(DefaultThread->GetStackFrameSize()) <= NextStepFrom.load() ||
      StepInRequest.load() ||
      long(DefaultThread->GetStackFrameSize()) <= StepOutDepth.load()) {
    ClearStepRequests();
    waitSem = true;

    stoppedEvent.reason = "step";
  }

  if (PauseRequest.load()) {
    ClearStepRequests();
    waitSem = true;

    stoppedEvent.reason = "pause";
  }

  if (waitSem) {
    Session->send(stoppedEvent);
    ContinueSem->Wait();
  }
}

void cmDebuggerAdapter::EndFunction()
{
  DefaultThread->PopStackFrame();
}

static std::shared_ptr<cmListFileFunction> listFileFunction;

void cmDebuggerAdapter::BeginFileParse(cmMakefile* mf,
                                       std::string const& sourcePath)
{
  std::unique_lock<std::mutex> lock(Mutex);

  listFileFunction = std::make_shared<cmListFileFunction>(
    sourcePath, 0, 0, std::vector<cmListFileArgument>());
  DefaultThread->PushStackFrame(mf, sourcePath, *listFileFunction);
}

void cmDebuggerAdapter::EndFileParse()
{
  DefaultThread->PopStackFrame();
  listFileFunction = nullptr;
}

void cmDebuggerAdapter::CheckException(MessageType t, std::string const& text)
{
  std::optional<dap::StoppedEvent> stoppedEvent =
    ExceptionManager->RaiseExceptionIfAny(t, text);
  if (stoppedEvent.has_value()) {
    stoppedEvent->threadId = DefaultThread->GetId();
    Session->send(*stoppedEvent);
    ContinueSem->Wait();
  }
}

void cmDebuggerAdapter::ClearStepRequests()
{
  NextStepFrom.store(INT_MIN);
  StepInRequest.store(false);
  StepOutDepth.store(INT_MIN);
  PauseRequest.store(false);
}

} // namespace cmDebugger
