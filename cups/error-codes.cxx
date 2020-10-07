// Copyright 2020 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "error-codes.h"

#include <thread>
#include <vector>

namespace {

// Id of the main thread.
const std::thread::id thread_id = std::this_thread::get_id();

struct EcFunctionCall {
  // An unique pointer representing a scope (function).
  const char* function_name;
  // The last error code reported in the scope (or EC_NONE).
  error_code_t last_error;
  // Constructor.
  EcFunctionCall(const char* func_name, error_code_t error)
      : function_name(func_name), last_error(error) {}
};

// The current stack of functions calls with the EC_FUNC macro.
// It always has at least one element and the first element
// always has |function_name| == nullptr.
std::vector<EcFunctionCall>* calls_stack = nullptr;

// The only goal of this variable is to create |calls_stack|.
const struct Initializer {
  Initializer() {
    calls_stack = new std::vector<EcFunctionCall>();
    calls_stack->reserve(32);
    calls_stack->emplace_back(nullptr, EC_NONE);
  }
} initializer;

}  // namespace

extern "C" void _ec_begin(const char* const func) {
  if (thread_id != std::this_thread::get_id() || func == nullptr)
    return;
  calls_stack->emplace_back(func, EC_NONE);
}

extern "C" void _ec_end(const char* const func, const error_code_t error) {
  if (thread_id != std::this_thread::get_id() || func == nullptr)
    return;
  // We should remove the last element only. However, it may occurred that
  // there are some extra elements. It may happen only if there are
  // missing RETURN_* macros in the code (not all EC_FUNC were matched by
  // corresponding RETURN_macro).
  const char* last_func = nullptr;
  do {
    last_func = calls_stack->back().function_name;
    if (last_func == nullptr)  // Safety guard - it should never happen.
      return;
    calls_stack->pop_back();
  } while (last_func != func);
  // We overwrite the last error code <=> an error was reported.
  if (error != EC_NONE)
    calls_stack->back().last_error = error;
}

extern "C" void _ec_end_auto(const char* const func) {
  if (thread_id != std::this_thread::get_id() || func == nullptr)
    return;
  // We should remove the last element only. However, it may occurred that
  // there are some extra elements. It may happen only if there are
  // missing RETURN_* macros in the code (not all EC_FUNC were matched by
  // corresponding RETURN_macro).
  const char* last_func = nullptr;
  error_code_e last_error = EC_UNKNOWN;
  do {
    last_func = calls_stack->back().function_name;
    if (last_func == nullptr)  // Safety guard - it should never happen.
      return;
    if (calls_stack->back().last_error != EC_NONE)
      last_error = calls_stack->back().last_error;
    calls_stack->pop_back();
  } while (last_func != func);
  // We overwrite the last error code <=> an error was reported.
  if (last_error != EC_NONE)
    calls_stack->back().last_error = last_error;
}

extern "C" error_code_t ec_last_error() {
  if (thread_id != std::this_thread::get_id())
    return EC_NONE;
  return calls_stack->back().last_error;
}
