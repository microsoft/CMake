#include "cmConfigure.h" // IWYU pragma: keep

#include "cmDebugger.h"

#include "dap/io.h"
#include "dap/protocol.h"
#include "dap/session.h"

#include <cstring>
#include <iostream>
#include <sstream>


#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <unordered_set>

#ifdef _MSC_VER
#  define OS_WINDOWS 1
#endif

#ifdef OS_WINDOWS
#  include <fcntl.h> // _O_BINARY
#  include <io.h>    // _setmode
#endif               // OS_WINDOWS

// class Debugger
Debugger::Debugger(const EventHandler& onEvent)
  : onEvent(onEvent)
{
}

void Debugger::initialize(std::string dbgSrc)
{
  //std::ifstream fileCMT(dbgSrc);
  //if (fileCMT.is_open()) {
  //  std::string ll;
  //  std::stringstream buffer;
  //  int count = 0;
  //  while (std::getline(fileCMT, ll)) {
  //    count++;
  //    buffer << line << "\n";
  //    sourceLines[count] = line;
  //  }
  //  numSourceLines = count;
  //  fileCMT.close();
  //}
}

void Debugger::run()
{
  std::unique_lock<std::mutex> lock(mutex);
  while (m_cmake->dbgTypeIndex >= 0) {
    for (int64_t i = m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgLine /*line*/ + 1;
         i <= numSourceLines[m_cmake->dbgTypeIndex]; i++) {
      line = i;
      if (breakpoints.count(i)) {
        // the line should not be empty because we don't add unverified
        // breakpoints to the debugger object
        lock.unlock();

        // while (line - 1 > this->m_cmake->cmakeLine) {
        //   int x = 0;
        //   x++;
        //   x--;
        // }
        m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgLine = line;

        onEvent(Event::BreakpointHit, 0, "");
        onEvent(Event::Paused, 0, "");
        return;
      }
    }

    onEvent(Event::SteppedOut2, m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgSrcIndex, m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgSrc);
    m_cmake->dbgTypeIndex--;
  }


  //while (line - 1 > this->m_cmake->cmakeLine) {
  //  int x = 0;
  //  x++;
  //  x--;
  //}
  
  //m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgLine = line;

  onEvent(Event::Terminate, 0, "");
}

void Debugger::pause()
{
  onEvent(Event::Paused, 0, "");
}

int64_t Debugger::currentLine()
{
  std::unique_lock<std::mutex> lock(mutex);
  line = m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgLine;
  return line;
}

int64_t Debugger::getNextNonEmptyLineIfEmpty()
{
  if (numSourceLines[m_cmake->dbgTypeIndex] == 0) {
    line = 1; // figure out how not to do this
  } else if (!sourceLines[m_cmake->dbgTypeIndex][line+1].empty() &&
             !sourceLines[m_cmake->dbgTypeIndex][line+1]._Starts_with("#")) {
    return ++line;
  } else {
    int l = line + 1;
    while (l <= numSourceLines[m_cmake->dbgTypeIndex] &&
           (sourceLines[m_cmake->dbgTypeIndex][l].empty() ||
            sourceLines[m_cmake->dbgTypeIndex][l]._Starts_with("#"))) {
      l++;
    }
    
    line = l;
  }

  return line;
}

// make sure to hit any breakpoint that happens inside
void Debugger::stepForward()
{
  std::unique_lock<std::mutex> lock(mutex);
  //line++;
  line = getNextNonEmptyLineIfEmpty(); //  = (line % numSourceLines) + 1;
  lock.unlock();

  //while (line - 1 > this->m_cmake->cmakeLine) {
  //  int x = 0;
  //  x++;
  //  x--;
  //}
  m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgLine = line;

  if (line <= numSourceLines[m_cmake->dbgTypeIndex]) {
    onEvent(Event::Stepped, 0, "");
  } else {
    if (m_cmake->dbgTypeIndex == 0) {
      onEvent(Event::Terminate, 0, "");
    } else {
      onEvent(Event::SteppedOut2, m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgSrcIndex, "");
      m_cmake->dbgTypeIndex--;
      line = m_cmake[m_cmake->dbgTypeIndex].curDbg->dbgLine;
    }
  }
}

void Debugger::stepIn()
{
  std::unique_lock<std::mutex> lock(mutex);
  m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgLine = getNextNonEmptyLineIfEmpty(); // line + 1; // where to return
  m_cmake->dbgTypeIndex++;
  m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgLine = 0; // or 1?
  m_cmake->curDbg[m_cmake->dbgTypeIndex].cmakeLine = 0; // or 1?
  m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgSrcIndex = m_cmake->curDbg[m_cmake->dbgTypeIndex - 1].dbgSrcIndex + 1;
  m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgSrc =
    "???"; // "C:/ Work / Projects / QuickStart / build / CMakeFiles
           // / 3.18.20200727 -g30a0cb2 - dirty / CMakeSystem.cmake "; //"???";
  //onEvent(Event::Stepped);
}

// todo: make sure to stop at any breakpoint before we reach the destination of stepout
void Debugger::stepOut()
{
  std::unique_lock<std::mutex> lock(mutex);
  onEvent(Event::SteppedOut2, m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgSrcIndex, m_cmake->curDbg[m_cmake->dbgTypeIndex].dbgSrc);
  
  if (m_cmake->dbgTypeIndex > 0) {
    m_cmake->dbgTypeIndex--;
    line = m_cmake[m_cmake->dbgTypeIndex].curDbg->dbgLine;
    onEvent(Event::Stepped, 0, "");
  } else {
    onEvent(Event::Terminate, 0, "");
  }
}

void Debugger::sendStepInEvent(std::string dbgSrc, int line)
{
  onEvent(Event::SteppedIn, 0, "");
}

void Debugger::clearBreakpoints()
{
  std::unique_lock<std::mutex> lock(mutex);
  this->breakpoints.clear();
}

void Debugger::addBreakpoint(int64_t l)
{
  std::unique_lock<std::mutex> lock(mutex);
  this->breakpoints.emplace(l);
}


// class Event
void Event::wait()
{
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return fired; });
}

void Event::fire()
{
  std::unique_lock<std::mutex> lock(mutex);
  fired = true;
  cv.notify_all();
}

