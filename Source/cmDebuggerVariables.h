/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <atomic>
#include <memory>

#include <cm3p/cppdap/protocol.h>

#include "cmValue.h"

namespace cmDebugger {

class cmDebuggerVariables
{
  static std::atomic<int64_t> NextId;
  int64_t Id;

protected:
  bool SupportsVariableType;

public:
  cmDebuggerVariables(bool supportsVariableType);
  int64_t GetId() const noexcept { return this->Id; }
  virtual dap::array<dap::Variable> GetVariables(
    dap::VariablesRequest const& request) = 0;
  virtual ~cmDebuggerVariables() = default;
};

class cmDebuggerVariablesLocal : public cmDebuggerVariables
{
  int64_t CacheVariableReference;
  std::function<int64_t()> GetLine;

public:
  cmDebuggerVariablesLocal(bool supportsVariableType,
                           std::function<int64_t()> const& getLine,
                           int64_t cacheVariableReference);
  dap::array<dap::Variable> GetVariables(
    dap::VariablesRequest const& request);
};

class cmDebuggerVariablesCache : public cmDebuggerVariables
{
  std::function<std::vector<std::string>()> GetKeys;
  std::function<cmValue(std::string const&)> GetDefinition;

public:
  cmDebuggerVariablesCache(
    bool supportsVariableType,
    std::function<std::vector<std::string>()> getKeys,
    std::function<cmValue(std::string const&)> getDefinition);
  dap::array<dap::Variable> GetVariables(
    dap::VariablesRequest const& request);
};

} // namespace cmDebugger
