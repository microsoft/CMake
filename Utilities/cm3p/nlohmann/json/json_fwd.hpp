/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

/* Use the nlohmann_json library configured for CMake.  */
#include "cmThirdParty.h"
#ifdef CMAKE_USE_SYSTEM_NLOHMANNJSON
#  include <nlohmann/json_fwd.hpp> // IWYU pragma: export
#else
#  include <cmnlohmann/include/nlohmann/json_fwd.hpp> // IWYU pragma: export
#endif
