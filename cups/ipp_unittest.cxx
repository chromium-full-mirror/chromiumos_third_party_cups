// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <string>
#include <memory>

#include "absl/types/optional.h"
#include "cups.h"
#include "cups-private.h"
#include "ipp.h"
#include "gtest/gtest.h"

namespace {

using ScopedIppPtr = std::unique_ptr<ipp_t, void (*)(ipp_t*)>;

const char* kClientInfoAllSupported[] = {
    "client-type",           "client-name",    "client-patches",
    "client-string-version", "client-version",
};
const char* kClientInfoOnlyRequiredMembersSupported[] = {
    "client-type",
    "client-name",
    "client-string-version",
};

ScopedIppPtr WrapIpp(ipp_t* ipp) {
  return ScopedIppPtr(ipp, &ippDelete);
}

struct ClientInfo {
  std::string name;
  int type;
  std::string str_version;
  absl::optional<std::string> patches;
  absl::optional<std::string> version;
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

  ASSERT_TRUE(name_attr);
  ASSERT_TRUE(type_attr);
  ASSERT_TRUE(string_version_attr);

  std::string name = ippGetString(name_attr, 0, nullptr);
  int type = ippGetInteger(type_attr, 0);
  std::string string_version = ippGetString(string_version_attr, 0, nullptr);
  EXPECT_EQ(name, expected.name);
  EXPECT_EQ(type, expected.type);
  EXPECT_EQ(string_version, expected.str_version);

  if (expected.patches.has_value()) {
    ASSERT_TRUE(patches_attr);
    std::string patches = ippGetString(patches_attr, 0, nullptr);
    EXPECT_EQ(patches, expected.patches.value());
  } else {
    EXPECT_FALSE(patches_attr);
  }
  if (expected.version.has_value()) {
    ASSERT_TRUE(version_attr);
    int version_len;
    char* version =
        static_cast<char*>(ippGetOctetString(version_attr, 0, &version_len));
    EXPECT_EQ(std::string(version, version_len), expected.version);

  } else {
    EXPECT_FALSE(version_attr);
  }
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
      "{client-name=ChromeOS client-type=4 client-string-version=108 "
      "client-patches=15278.35.0},{client-name=chromebook-123 "
      "client-type=6 client-string-version=}");

  ASSERT_TRUE(client_info_attr);
  EXPECT_STREQ(ippGetName(client_info_attr), "client-info");
  EXPECT_EQ(ippGetCount(client_info_attr), 2);

  ipp_t* first = ippGetCollection(client_info_attr, 0);
  ipp_t* second = ippGetCollection(client_info_attr, 1);

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  CheckClientInfoMatch(first,
                       {"ChromeOS", 4, "108", "15278.35.0", absl::nullopt});
  CheckClientInfoMatch(second,
                       {"chromebook-123", 6, "", absl::nullopt, absl::nullopt});
}

TEST(IppClientInfoTests, AddClientInfoOptionToIppRequestClientInfoUnsupported) {
  ScopedIppPtr ipp = WrapIpp(ippNew());
  std::string client_info_option =
      "{client-name=ChromeOS client-type=4 "
      "client-string-version=108 "
      "client-patches=15278.35.0},{client-name=chromebook-123 client-type=6 "
      "client-string-version=}";
  add_client_info_values_to_ipp(client_info_option.c_str(), nullptr, nullptr, 5,
                                ipp.get());

  ipp_attribute_t* client_info =
      ippFindAttribute(ipp.get(), "client-info", IPP_TAG_BEGIN_COLLECTION);
  ASSERT_FALSE(client_info);
}

TEST(IppClientInfoTests, AddClientInfoOptionToIppRequest) {
  ScopedIppPtr ipp = WrapIpp(ippNew());
  ipp_attribute_t* client_info_supported = ippAddStrings(
      ipp.get(), IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "client-info-supported", 5,
      nullptr, kClientInfoAllSupported);
  std::string client_info_option =
      "{client-name=ChromeOS client-type=4 "
      "client-string-version=108 client-version=version "
      "client-patches=15278.35.0},{client-name=chromebook-123 client-type=6 "
      "client-string-version=}";
  add_client_info_values_to_ipp(client_info_option.c_str(),
                                client_info_supported, nullptr, 5, ipp.get());

  ipp_attribute_t* client_info_attr =
      ippFindAttribute(ipp.get(), "client-info", IPP_TAG_BEGIN_COLLECTION);
  ASSERT_TRUE(client_info_attr);
  ASSERT_EQ(ippGetCount(client_info_attr), 2);

  ipp_t* first = ippGetCollection(client_info_attr, 0);
  ipp_t* second = ippGetCollection(client_info_attr, 1);

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  CheckClientInfoMatch(first, {"ChromeOS", 4, "108", "15278.35.0", "version"});
  CheckClientInfoMatch(second,
                       {"chromebook-123", 6, "", absl::nullopt, absl::nullopt});
}

TEST(IppClientInfoTests, AddClientInfoOptionToIppRequestMaxClientInfoLimit) {
  ScopedIppPtr ipp = WrapIpp(ippNew());
  ipp_attribute_t* client_info_supported = ippAddStrings(
      ipp.get(), IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "client-info-supported", 5,
      nullptr, kClientInfoAllSupported);
  std::string client_info_option =
      "{client-name=ChromeOS client-type=4 "
      "client-string-version=108 client-version=version "
      "client-patches=15278.35.0},{client-name=chromebook-123 client-type=6 "
      "client-string-version=}";
  add_client_info_values_to_ipp(client_info_option.c_str(),
                                client_info_supported, nullptr, 1, ipp.get());

  ipp_attribute_t* client_info_attr =
      ippFindAttribute(ipp.get(), "client-info", IPP_TAG_BEGIN_COLLECTION);
  ASSERT_TRUE(client_info_attr);
  ASSERT_EQ(ippGetCount(client_info_attr), 1);

  ipp_t* value = ippGetCollection(client_info_attr, 0);
  ASSERT_TRUE(value);
  CheckClientInfoMatch(value, {"ChromeOS", 4, "108", "15278.35.0", "version"});
}

TEST(IppClientInfoTests,
     AddClientInfoOptionToIppRequestClientTypeNotRequested) {
  ScopedIppPtr ipp = WrapIpp(ippNew());
  ipp_attribute_t* client_info_supported = ippAddStrings(
      ipp.get(), IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "client-info-supported", 5,
      nullptr, kClientInfoAllSupported);
  ipp_attribute_t* printer_requested_client_type =
      ippAddInteger(ipp.get(), IPP_TAG_PRINTER, IPP_TAG_ENUM,
                    "printer-requested-client-type", 6);
  std::string client_info_option =
      "{client-name=ChromeOS client-type=4 "
      "client-string-version=108 client-version=version "
      "client-patches=15278.35.0},{client-name=chromebook-123 "
      "client-type=6 "
      "client-string-version=}";
  add_client_info_values_to_ipp(client_info_option.c_str(),
                                client_info_supported,
                                printer_requested_client_type, 5, ipp.get());

  ipp_attribute_t* client_info_attr =
      ippFindAttribute(ipp.get(), "client-info", IPP_TAG_BEGIN_COLLECTION);
  ASSERT_TRUE(client_info_attr);
  ASSERT_EQ(ippGetCount(client_info_attr), 1);

  ipp_t* value = ippGetCollection(client_info_attr, 0);
  ASSERT_TRUE(value);
  CheckClientInfoMatch(value,
                       {"chromebook-123", 6, "", absl::nullopt, absl::nullopt});
}

TEST(IppClientInfoTests, AddClientInfoOptionToIppRequestUnsupportedMembers) {
  ScopedIppPtr ipp = WrapIpp(ippNew());
  ipp_attribute_t* client_info_supported = ippAddStrings(
      ipp.get(), IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "client-info-supported", 3,
      nullptr, kClientInfoOnlyRequiredMembersSupported);
  std::string client_info_option =
      "{client-name=ChromeOS client-type=4 "
      "client-string-version=108 client-version=version "
      "client-patches=15278.35.0},{client-name=chromebook-123 client-type=6 "
      "client-string-version=}";
  add_client_info_values_to_ipp(client_info_option.c_str(),
                                client_info_supported, nullptr, 5, ipp.get());

  ipp_attribute_t* client_info_attr =
      ippFindAttribute(ipp.get(), "client-info", IPP_TAG_BEGIN_COLLECTION);
  ASSERT_TRUE(client_info_attr);
  ASSERT_EQ(ippGetCount(client_info_attr), 2);

  ipp_t* first = ippGetCollection(client_info_attr, 0);
  ipp_t* second = ippGetCollection(client_info_attr, 1);

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  CheckClientInfoMatch(first,
                       {"ChromeOS", 4, "108", absl::nullopt, absl::nullopt});
  CheckClientInfoMatch(second,
                       {"chromebook-123", 6, "", absl::nullopt, absl::nullopt});
}

}  // namespace
