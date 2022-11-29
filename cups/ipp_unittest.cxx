// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cups/ipp.h>
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <memory>

#include "cups.h"
#include "ipp.h"

namespace {

using ScopedIppPtr = std::unique_ptr<ipp_t, void (*)(ipp_t*)>;

ScopedIppPtr WrapIpp(ipp_t* ipp) {
  return ScopedIppPtr(ipp, &ippDelete);
}

struct ClientInfo {
  std::string name;
  int type;
  std::string string_version;
  std::string patches;
  std::string version;
};

// EXPECT that all `client_info` sub-attributes match `expected`
void CheckClientInfoMatch(ipp_t* client_info, const ClientInfo& expected) {
  ipp_attribute_t* name_attr =
      ippFindAttribute(client_info, "client-name", IPP_TAG_NAME);
  ipp_attribute_t* type_attr =
      ippFindAttribute(client_info, "client-type", IPP_TAG_ENUM);
  ipp_attribute_t* string_version_attr =
      ippFindAttribute(client_info, "client-string-version", IPP_TAG_TEXT);
  ipp_attribute_t* patches_attr =
      ippFindAttribute(client_info, "client-patches", IPP_TAG_TEXT);
  ipp_attribute_t* version_attr =
      ippFindAttribute(client_info, "client-version", IPP_TAG_STRING);

  EXPECT_NE(name_attr, nullptr);
  EXPECT_NE(type_attr, nullptr);
  EXPECT_NE(string_version_attr, nullptr);
  EXPECT_NE(patches_attr, nullptr);
  EXPECT_NE(version_attr, nullptr);

  std::string name = ippGetString(name_attr, 0, nullptr);
  int type = ippGetInteger(type_attr, 0);
  std::string string_version = ippGetString(string_version_attr, 0, nullptr);
  std::string patches = ippGetString(patches_attr, 0, nullptr);
  int version_len;
  char* version =
      static_cast<char*>(ippGetOctetString(version_attr, 0, &version_len));

  EXPECT_EQ(name, expected.name);
  EXPECT_EQ(type, expected.type);
  EXPECT_EQ(string_version, expected.string_version);
  EXPECT_EQ(patches, expected.patches);
  EXPECT_EQ(std::string(version, version_len), expected.version);
}

TEST(IppClientInfoTests, ClientTypeStringToInt) {
  EXPECT_EQ(ippEnumValue("client-type", "application"), 3);
  EXPECT_EQ(ippEnumValue("client-type", "operating-system"), 4);
  EXPECT_EQ(ippEnumValue("client-type", "driver"), 5);
  EXPECT_EQ(ippEnumValue("client-type", "other"), 6);
}

TEST(IppClientInfoTests, ClientTypeIntToString) {
  EXPECT_STREQ(ippEnumString("client-type", 3), "application");
  EXPECT_STREQ(ippEnumString("client-type", 4), "operating-system");
  EXPECT_STREQ(ippEnumString("client-type", 5), "driver");
  EXPECT_STREQ(ippEnumString("client-type", 6), "other");
}

TEST(IppClientInfoTests, EncodeClientInfoCupsOption) {
  ScopedIppPtr ipp = WrapIpp(ippNew());
  ipp_attribute_t* client_info_attr = cupsEncodeOption(
      ipp.get(), IPP_TAG_OPERATION, "client-info",
      "{client-name=ChromeOS client-type=4 client-string-version=M108 "
      "client-patches=0.5410.0 "
      "client-version=no-value},{client-name=chromebook-123 "
      "client-type=6 client-string-version= client-version=no-value "
      "client-patches=no-value}");

  ASSERT_NE(client_info_attr, nullptr);
  EXPECT_STREQ(ippGetName(client_info_attr), "client-info");
  EXPECT_EQ(ippGetCount(client_info_attr), 2);

  ipp_t* first = ippGetCollection(client_info_attr, 0);
  ipp_t* second = ippGetCollection(client_info_attr, 1);

  EXPECT_NE(first, nullptr);
  EXPECT_NE(second, nullptr);

  CheckClientInfoMatch(first, {"ChromeOS", 4, "M108", "0.5410.0", "no-value"});
  CheckClientInfoMatch(second,
                       {"chromebook-123", 6, "", "no-value", "no-value"});
}

}  // namespace
