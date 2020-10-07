// Copyright 2020 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <vector>

#include "error-codes.h"
#include "gtest/gtest.h"

namespace {

// The function returns |result|. If |result| == true, the function reports
// no errors. Otherwise, it reports IO error.
bool SimpleFuncReturnOkOrIoErr(bool result) {
  EC_FUNC;
  if (result)
    RETURN_OK(true);
  RETURN_FAIL_IO(false);
}

// The function returns |result|. If |result| == true, the function reports
// no errors. Otherwise, it reports MEMORY error.
bool SimpleFuncReturnOkOrMemoryErr(bool result) {
  EC_FUNC;
  if (!result)
    RETURN_FAIL_MEMORY(false);
  RETURN_OK(true);
}

// The function returns |result|. If |result| == true, the function reports
// no errors. Otherwise, it reports the error reported by
// SimpleFuncReturnOkOrMemoryErr(false).
bool ComplexFuncReturnOkOrMemoryErr(bool result) {
  EC_FUNC;
  SimpleFuncReturnOkOrMemoryErr(true);
  SimpleFuncReturnOkOrIoErr(false);
  if (!SimpleFuncReturnOkOrMemoryErr(result))
    RETURN_FAIL(false);
  RETURN_OK(true);
}

// This function always return true and reports no errors.
bool ComplexFuncReturnOk() {
  EC_FUNC;
  SimpleFuncReturnOkOrIoErr(false);
  RETURN_OK(true);
}

}  // namespace

// Calling a function that succeeds (reports no errors).
TEST(ErrorCodes, ReturnOk) {
  EXPECT_TRUE(SimpleFuncReturnOkOrIoErr(true));
  EXPECT_EQ(ec_last_error(), EC_NONE);
  EXPECT_TRUE(SimpleFuncReturnOkOrMemoryErr(true));
  EXPECT_EQ(ec_last_error(), EC_NONE);
}

// Calling a function that succeeds after its subfunction reports an error.
TEST(ErrorCodes, ReturnOkAfterError) {
  EXPECT_TRUE(ComplexFuncReturnOkOrMemoryErr(true));
  EXPECT_EQ(ec_last_error(), EC_NONE);
  EXPECT_TRUE(ComplexFuncReturnOk());
  EXPECT_EQ(ec_last_error(), EC_NONE);
}

// Calling a function that reports an error.
TEST(ErrorCodes, ReturnError) {
  EXPECT_FALSE(SimpleFuncReturnOkOrIoErr(false));
  EXPECT_EQ(ec_last_error(), EC_IO);
  EXPECT_FALSE(SimpleFuncReturnOkOrMemoryErr(false));
  EXPECT_EQ(ec_last_error(), EC_MEMORY);
}

// Calling a function that reports the last error reported by its subfunctions.
TEST(ErrorCodes, ReturnLastError) {
  EXPECT_FALSE(ComplexFuncReturnOkOrMemoryErr(false));
  EXPECT_EQ(ec_last_error(), EC_MEMORY);
}
