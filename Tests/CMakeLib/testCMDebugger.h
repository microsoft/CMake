/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "testCommon.h"

#define ASSERT_VARIABLE(x, expectedName, expectedValue, expectedType)         \
  do {                                                                        \
    ASSERT_TRUE(x.name == expectedName);                                      \
    ASSERT_TRUE(x.value == expectedValue);                                    \
    ASSERT_TRUE(x.type.value() == expectedType);                              \
    ASSERT_TRUE(x.evaluateName.has_value() == false);                         \
    if (std::string(expectedType) == "collection") {                          \
      ASSERT_TRUE(x.variablesReference != 0);                                 \
    }                                                                         \
  } while (false)

#define ASSERT_VARIABLE_REFERENCE(x, expectedName, expectedValue,             \
                                  expectedType, expectedReference)            \
  do {                                                                        \
    ASSERT_VARIABLE(x, expectedName, expectedValue, expectedType);            \
    ASSERT_TRUE(x.variablesReference == (expectedReference));                 \
  } while (false)

#define ASSERT_VARIABLE_REFERENCE_NOT_ZERO(x, expectedName, expectedValue,    \
                                           expectedType)                      \
  do {                                                                        \
    ASSERT_VARIABLE(x, expectedName, expectedValue, expectedType);            \
    ASSERT_TRUE(x.variablesReference != 0);                                   \
  } while (false)
