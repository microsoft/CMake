/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef cmDebugger_h
#define cmDebugger_h

#include "cmConfigure.h" // IWYU pragma: keep

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <map>
#include <unordered_set>
#include "cmake.h"

// IWYU does not see that 'std::unordered_map<std::string, cmTarget>'
// will not compile without the complete type.
#include "cmTarget.h" // IWYU pragma: keep

#if !defined(CMAKE_BOOTSTRAP)
#  include "cmSourceGroup.h"
#endif

// Uncomment the line below and change <path-to-log-file> to a file path to
// write all DAP communications to the given path.
//
//#define LOG_TO_FILE "c:\\git\\logs\\dep.log"

// Debugger holds the dummy debugger state and fires events to the EventHandler
// passed to the constructor.
class Debugger
{
public:
  enum class Event
  {
    BreakpointHit,
    Stepped,
    SteppedIn,
    SteppedOut,
    SteppedOut2,
    Paused,
    Disconnect,
    Terminate
  };
  using EventHandler = std::function<void(Event, int dbgSrcIdx, std::string dbgSrc)>;

  Debugger(const EventHandler&);

  void initialize(std::string);

  // run() instructs the debugger to continue execution.
  void run();

  // pause() instructs the debugger to pause execution.
  void pause();

  // currentLine() returns the currently executing line number.
  int64_t currentLine();

  int64_t getNextNonEmptyLineIfEmpty();

  // stepForward() instructs the debugger to step forward one line.
  void stepForward();

  void stepIn();

  void stepOut();

  void sendStepInEvent(std::string dbgSrc, int line);

  // clearBreakpoints() clears all set breakpoints.
  void clearBreakpoints();

  // addBreakpoint() sets a new breakpoint on the given line.
  void addBreakpoint(int64_t line);

  // Number of source lines in the CMakeLists.txt
  int64_t numSourceLines[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  std::map<int, std::string> sourceLines[10]; //  = new std::map<int, std::string, >();
  cmake* m_cmake;
  
private:
  EventHandler onEvent;
  std::mutex mutex;
  int64_t line = 1;
  std::unordered_set<int64_t> breakpoints;
};


// Event provides a basic wait and signal synchronization primitive.
class Event
{
public:
  // wait() blocks until the event is fired.
  void wait();

  // fire() sets signals the event, and unblocks any calls to wait().
  void fire();

private:
  std::mutex mutex;
  std::condition_variable cv;
  bool fired = false;
};

#endif
