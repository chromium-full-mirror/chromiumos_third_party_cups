// Copyright 2021 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <gtest/gtest-param-test.h>

#include "ppd-cache-private.h"

namespace {

struct Conversion {
  const char* pwg_value;
  const char* ppd_value;
};

class PpdCacheUnppdizeTest : public testing::TestWithParam<Conversion> {};

TEST_P(PpdCacheUnppdizeTest, Normal) {
  char actual[25];
  pwg_unppdize_name(GetParam().ppd_value, actual, 25, /*dashchars=*/"",
                    /*exempt_x_dot=*/0);
  EXPECT_THAT(actual, testing::StrEq(GetParam().pwg_value));
}

const Conversion kNormal[] = {
    {"tray-1", "Tray1"},
    {"roll-10", "Roll10"},
    {"multifunction-tray", "MultifunctionTray"},
    {"upper", "Upper"},
    {"already-pwg", "already-pwg"},
    {"tray-15", "Tray-15"},
    // Verify non-ASCII.
    {"フォルダーパ", "フォルダーパ"},
    // Remove duplicate and trailing dashes.
    {"duplicate-dashes", "Duplicate--dashes"},
    {"duplicate-dashes", "duplicate---dashes"},
    {"trailing-dash", "trailing-dash-"},
    {"trailing-dashes", "trailing-dashes--"},
    {"leading-dashes", "-leading-dashes"},
    // Verify x and dot handling.
    {"a.b.c", "A.B.C"},
    {"media-sizex-25", "MediaSizex25"},
    // Verify string pruning.
    {"123456789012345678901234",
     "1234567890123456789012345_TheseCharactersShouldBePruned"},
    {"abcdefghijklmnopqrstuvwx",
     "abcdefghijklmnopqrstuvwx_thesecharactersshouldbepruned"}};
INSTANTIATE_TEST_SUITE_P(Normal,
                         PpdCacheUnppdizeTest,
                         testing::ValuesIn(kNormal));

class PpdCacheUnppdizeXDotExemptTest
    : public testing::TestWithParam<Conversion> {};

TEST_P(PpdCacheUnppdizeXDotExemptTest, XDotExempt) {
  char actual[255];
  pwg_unppdize_name(GetParam().ppd_value, actual, 255, /*dashchars=*/"",
                    /*exempt_x_dot=*/1);
  EXPECT_THAT(actual, testing::StrEq(GetParam().pwg_value));
}

const Conversion kDotExempt[] = {{"na_letter_8.5x11in", "na_letter_8.5x11in"},
                                 {"5x7", "5x7"},
                                 {"envelope-10x11", "Envelope10x11"},
                                 {"media-size-5", "MediaSize5"}};
INSTANTIATE_TEST_SUITE_P(XDotExempt,
                         PpdCacheUnppdizeXDotExemptTest,
                         testing::ValuesIn(kDotExempt));

TEST(PpdCacheUnppdizeTest, ZeroLengthNameUpperCase) {
  char actual = 'X';
  pwg_unppdize_name("DoesNotMatter", &actual, 0, "", 0);
  // Zero length causes no changes.
  EXPECT_EQ(actual, 'X');
}

TEST(PpdCacheUnppdizeTest, ZeroLengthNameLowerCase) {
  char actual = 'X';
  pwg_unppdize_name("lowercase", &actual, 0, "", 0);
  // Zero length causes no changes.
  EXPECT_EQ(actual, 'X');
}

TEST(PpdCacheUnppdizetest, DashcharReplacement) {
  const char dashchars[] = "$%^";
  char actual[25];

  pwg_unppdize_name("Convert$Dashed^chars%4", actual, 25, dashchars,
                    /*exempt_x_dot=*/0);
  EXPECT_THAT(actual, testing::StrEq("convert-dashed-chars-4"));
}

}  // namespace
