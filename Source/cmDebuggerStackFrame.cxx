/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebuggerStackFrame.h"

namespace cmDebugger {

cmDebuggerStackFrame::cmDebuggerStackFrame(std::string fileName)
  : cmDebuggerStackFrame(fileName, "", 0)
{
}

cmDebuggerStackFrame::cmDebuggerStackFrame(std::string fileName,
    std::string functionName,
    int64_t line)
  : FileName(fileName)
  , FunctionName(functionName)
  , Line(line)
  , Id(NextId.fetch_add(1))
{
}

std::atomic<int64_t> cmDebuggerStackFrame::NextId = 1;

} // namespace cmDebugger
