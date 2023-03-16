/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmDebuggerAdapterFactory.h"

#include <iostream>

#ifdef _WIN32
#  include <fcntl.h> // _O_BINARY
#  include <io.h>    // _setmode
#endif

#include "cmDebuggerAdapter.h"

namespace cmDebugger {
extern std::shared_ptr<dap::ReaderWriter> CreateDebuggerNamedPipe(
  std::string const& name);

std::shared_ptr<cmDebuggerAdapter> cmDebuggerAdapterFactory::CreateAdapter(
  std::string namedPipeIfAny, std::string dapLogFileIfAny)
{
  std::shared_ptr<dap::Reader> input;
  std::shared_ptr<dap::Writer> output;

  if (namedPipeIfAny.empty()) {
#if defined(_WIN32)
    // Change from text mode to binary mode for all Windows OS.
    // This ensures sequences of \r\n are not changed to \n.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    input = dap::file(stdin, false);
    output = dap::file(stdout, false);
  } else {
#if defined(_WIN32)
    std::shared_ptr<dap::ReaderWriter> pipe =
      CreateDebuggerNamedPipe(namedPipeIfAny);

    if (pipe->isOpen() == false) {
      std::cerr << "Error: Failed to create named pipe " << namedPipeIfAny
                << std::endl;
      return nullptr;
    }

    input = pipe;
    output = pipe;
#else
    std::cerr << "Error: Named pipe is not currently supported!" << std::endl;
    return nullptr;
#endif
  }

  return std::make_shared<cmDebugger::cmDebuggerAdapter>(input, output,
                                                         dapLogFileIfAny);
}

} // namespace cmDebugger
