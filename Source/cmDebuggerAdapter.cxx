/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */

#include "cmConfigure.h" // IWYU pragma: keep

#include <cstring>
#include <iostream>
#include <sstream>
#include <climits>

#include "cmake.h"
#include "cmDebuggerAdapter.h"
#include "cmDebuggerProtocol.h"
#include "cmDebuggerThread.h"
#include "cmDebuggerThreadManager.h"
#include "cmVersionConfig.h"

namespace cmDebugger {

// More refactoring work needed for variable stuff.
namespace {
dap::Variable CreateVariable(
  std::string const& name, std::string const& value, std::string const& type,
  bool supportsVariableType, std::optional<std::string> evaluateName = {},
  std::optional<dap::integer> variablesReference = {},
  std::optional<dap::VariablePresentationHint> presentationHint = {})
{
  dap::Variable variable;
  variable.name = name;
  variable.value = value;
  if (supportsVariableType) {
    variable.type = type;
  }
  if (evaluateName.has_value()) {
    variable.evaluateName = evaluateName.value();
  }
  if (variablesReference.has_value()) {
    variable.variablesReference = variablesReference.value();
  }
  if (presentationHint.has_value()) {
    variable.presentationHint = presentationHint.value();
  }
  return variable;
}

static const dap::VariablePresentationHint PublicPropertyHint = { {},
                                                                  "property",
                                                                  {},
                                                                  "public" };
static const int64_t CMakeCacheVariableReference = 123;
static const dap::integer VariablesReferenceId = 300;
}

cmDebuggerAdapter::cmDebuggerAdapter(
  std::shared_ptr<dap::Reader> reader, std::shared_ptr<dap::Writer> writer,
  std::function<cmStateSnapshot()> getCurrentSnapshot,
  std::string dapLogPath)
  : GetCurrentSnapshot(getCurrentSnapshot)
  , ThreadManager(std::make_unique<cmDebuggerThreadManager>())
  , SessionActive(true)
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
    ContinueSem.Notify();
    DisconnectEvent.Fire();
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
  Session->registerHandler(
    [this](const dap::StackTraceRequest& request)
      -> dap::ResponseOrError<dap::StackTraceResponse> {
      std::unique_lock<std::mutex> lock(Mutex);

      cm::optional<dap::StackTraceResponse> respone =
        ThreadManager->GetThreadStackTraceResponse(request.threadId);
      if (respone.has_value()) {
        return respone.value();
      }

      return dap::Error("Unknown threadId '%d'", int(request.threadId));
    });

  // The Scopes request reports all the scopes of the given stack frame.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Scopes
  Session->registerHandler(
    [this](const dap::ScopesRequest& request)
      -> dap::ResponseOrError<dap::ScopesResponse> {
      std::unique_lock<std::mutex> lock(Mutex);
      dap::Scope scope;
      scope.name = "Locals";
      scope.presentationHint = "locals";
      scope.variablesReference = VariablesReferenceId;

      dap::Source source;
      source.name = DefaultThread->GetTopStackFrame()->FileName;
      source.path = source.name;
      scope.source = source;

      dap::ScopesResponse response;
      response.scopes.push_back(scope);
      return response;
    });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Variables
  // More refactoring work needed for variable stuff.
  Session->registerHandler(
    [this](const dap::VariablesRequest& request)
      -> dap::ResponseOrError<dap::VariablesResponse> {
      std::unique_lock<std::mutex> lock(Mutex);
      dap::VariablesResponse response;
      if (request.variablesReference == VariablesReferenceId) {
        response.variables.push_back(CreateVariable(
          "CurrentLine",
          std::to_string(DefaultThread->GetTopStackFrame()->Line), "int",
          SupportsVariableType));

        response.variables.push_back(CreateVariable(
          "CMAKE CACHE VARIABLES", "", "collection", SupportsVariableType, {},
          std::make_optional(CMakeCacheVariableReference),
          PublicPropertyHint));
      }

      if (request.variablesReference == CMakeCacheVariableReference) {
        std::vector<std::string> closureKeys =
          GetCurrentSnapshot().ClosureKeys();
        std::sort(closureKeys.begin(), closureKeys.end());

        for (auto& varStr : closureKeys) {
          auto val = GetCurrentSnapshot().GetDefinition(varStr);
          if (val) {
            response.variables.push_back(CreateVariable(
              varStr, *val, "string", SupportsVariableType,
              std::make_optional(varStr)));
          }
        }
      }

      return response;
    });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Pause
  Session->registerHandler([this](const dap::PauseRequest& req) {
    PauseRequest.store(true);
    return dap::PauseResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Continue
  Session->registerHandler([this](const dap::ContinueRequest& req) {
    ContinueSem.Notify();
    return dap::ContinueResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Next
  Session->registerHandler([this](const dap::NextRequest& req) {
    NextStepFrom.store(DefaultThread->GetStackFrameSize());
    ContinueSem.Notify();
    return dap::NextResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepIn
  Session->registerHandler([this](const dap::StepInRequest& req) {
    // This would stop after stepped in, single line stepped or stepped out.
    StepInRequest.store(true);
    ContinueSem.Notify();
    return dap::StepInResponse();
  });

  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepOut
  Session->registerHandler([this](const dap::StepOutRequest& req) {
    StepOutDepth.store(DefaultThread->GetStackFrameSize() - 1);
    ContinueSem.Notify();
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
    ContinueSem.Notify();
    DisconnectEvent.Fire();
    SessionActive.store(false);
    return dap::DisconnectResponse();
  });

  Session->registerHandler([this](const dap::EvaluateRequest& request) {
    dap::EvaluateResponse response;
    auto var = GetCurrentSnapshot().GetDefinition(request.expression);
    if (var) {
      response.type = "string";
      response.result = var->c_str();
    }
    return response;
  });

  // The ConfigurationDone request is made by the client once all configuration
  // requests have been made.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ConfigurationDone
  Session->registerHandler(
    [this](const dap::ConfigurationDoneRequest& req) {
      ConfigurationDoneEvent.Fire();
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

  ConfigurationDoneEvent.Wait();

  DefaultThread = ThreadManager->StartThread("CMake script");
  dap::ThreadEvent threadEvent;
  threadEvent.reason = "started";
  threadEvent.threadId = DefaultThread->GetId();
  Session->send(threadEvent);
}

cmDebuggerAdapter::~cmDebuggerAdapter() {
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
  DisconnectEvent.Wait();
}

void cmDebuggerAdapter::SourceFileLoaded(std::string const& sourcePath,
                                         cmListFile const& listFile)
{
  BreakpointManager->SourceFileLoaded(sourcePath, listFile);
}

void cmDebuggerAdapter::BeginFunction(std::string const& sourcePath,
                                      std::string const& functionName,
                                      int64_t line)
{
  std::unique_lock<std::mutex> lock(Mutex);
  cmDebuggerStackFrame frame(sourcePath, functionName, line);
  DefaultThread->PushStackFrame(frame);

  if (line == 0) {
    // File just loaded, continue to first valid function call.
    return;
  }

  auto hits = BreakpointManager->GetBreakpoints(sourcePath, line);
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
    ContinueSem.Wait();
  }
}

void cmDebuggerAdapter::EndFunction()
{
  DefaultThread->PopStackFrame();
}

void cmDebuggerAdapter::CheckException(MessageType t, std::string const& text)
{
  std::optional<dap::StoppedEvent> stoppedEvent =
    ExceptionManager->RaiseExceptionIfAny(t, text);
  if (stoppedEvent.has_value()) {
    stoppedEvent->threadId = DefaultThread->GetId();
    Session->send(*stoppedEvent);
    ContinueSem.Wait();
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
