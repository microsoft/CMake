/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <map>
#include <memory>
#include <optional>

#include <cm3p/cppdap/protocol.h>

#include "cmValue.h"

namespace cmDebugger {

class cmDebuggerVariablesManager
{
  std::unordered_map<
    int64_t,
    std::function<dap::array<dap::Variable>(dap::VariablesRequest const&)>>
    VariablesHandlers;
  void RegisterHandler(
    int64_t id,
    std::function<dap::array<dap::Variable>(dap::VariablesRequest const&)>
      handler);
  void UnregisterHandler(int64_t id);
  friend class cmDebuggerVariables;

public:
  cmDebuggerVariablesManager();
  dap::array<dap::Variable> HandleVariablesRequest(
    dap::VariablesRequest const& request);
};

} // namespace cmDebugger
