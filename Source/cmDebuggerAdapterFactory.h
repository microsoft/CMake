/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <memory>
#include <string>

namespace cmDebugger {

class cmDebuggerAdapter;

class cmDebuggerAdapterFactory
{
public:
  static std::shared_ptr<cmDebuggerAdapter> CreateAdapter(
    std::string namedPipeIfAny, std::string dapLogFileIfAny);
};

} // namespace cmDebugger
