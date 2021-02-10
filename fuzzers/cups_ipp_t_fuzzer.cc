// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <cups/ipp.h>
#include <fuzzer/FuzzedDataProvider.h>

extern "C" {

namespace {

// The number of operations that can add entries to an ipp_t.
constexpr int kNumOperations = 9;

// Arbitrary max for attribute names.
constexpr int kNameMax = 100;

// Pool of names.
std::vector<std::vector<char>> names;

// Pool of sequences.
std::vector<std::vector<unsigned char>> sequences;

ipp_tag_t GenerateTag(FuzzedDataProvider& data_provider) {
  return (ipp_tag_t)data_provider.ConsumeIntegralInRange<int>(
      IPP_TAG_CUPS_INVALID, IPP_TAG_EXTENSION);
}

const char* GenerateName(FuzzedDataProvider& data_provider) {
  int name_len = data_provider.ConsumeIntegralInRange<int>(1, kNameMax);
  std::vector<char> name =
      data_provider.ConsumeBytesWithTerminator<char>(name_len);
  names.push_back(std::move(name));
  return names.back().data();
}

const unsigned char* GenerateSequence(FuzzedDataProvider& dprov, int length) {
  std::vector<unsigned char> seq = dprov.ConsumeBytes<unsigned char>(length);
  seq.resize(length, '\0');
  sequences.push_back(std::move(seq));
  return sequences.back().data();
}

// Fuzzes CUPS ipp_t creation and teardown.
void CupsIppTFuzz(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);

  // Collection of values so they don't leave scope.
  std::vector<std::string> values;

  ipp_t* message = ippNew();
  while (data_provider.remaining_bytes() != 0) {
    switch (data_provider.ConsumeIntegralInRange<int>(0, kNumOperations)) {
      case 0: {
        ipp_tag_t group = GenerateTag(data_provider);
        char value = (char)data_provider.ConsumeBool();
        const char* name = GenerateName(data_provider);
        ippAddBoolean(message, group, name, value);
        break;
      }
      case 1:
        // TODO: Add collections support
        break;
      case 2: {
        ipp_tag_t group = GenerateTag(data_provider);
        const char* name = GenerateName(data_provider);
        const ipp_uchar_t* value = GenerateSequence(data_provider, 11);
        ippAddDate(message, group, name, value);
        break;
      }
      case 3: {
        ipp_tag_t group = GenerateTag(data_provider);
        ipp_tag_t value_tag = GenerateTag(data_provider);
        const char* name = GenerateName(data_provider);
        int value = data_provider.ConsumeIntegral<int>();
        ippAddInteger(message, group, value_tag, name, value);
        break;
      }
      case 4: {
        ipp_tag_t group = GenerateTag(data_provider);
        const char* name = GenerateName(data_provider);
        values.push_back(data_provider.ConsumeRandomLengthString(kNameMax));
        ippAddOctetString(message, group, name, values.back().c_str(),
                          (int)values.back().length());
        break;
      }
      case 5: {
        ipp_tag_t group = GenerateTag(data_provider);
        ipp_tag_t value_tag = GenerateTag(data_provider);
        const char* name = GenerateName(data_provider);
        ippAddOutOfBand(message, group, value_tag, name);
        break;
      }
      case 6: {
        ipp_tag_t group = GenerateTag(data_provider);
        const char* name = GenerateName(data_provider);
        int lower = data_provider.ConsumeIntegral<int>();
        int upper = data_provider.ConsumeIntegral<int>();
        ippAddRange(message, group, name, lower, upper);
        break;
      }
      case 7: {
        ipp_tag_t group = GenerateTag(data_provider);
        const char* name = GenerateName(data_provider);
        ipp_res_t units =
            data_provider.ConsumeBool() ? IPP_RES_PER_CM : IPP_RES_PER_INCH;
        int xres = data_provider.ConsumeIntegral<int>();
        int yres = data_provider.ConsumeIntegral<int>();
        ippAddResolution(message, group, name, units, xres, yres);
        break;
      }
      case 8: {
        ippAddSeparator(message);
        break;
      }
      case 9: {
        ipp_tag_t group = GenerateTag(data_provider);
        ipp_tag_t value_tag = GenerateTag(data_provider);
        const char* name = GenerateName(data_provider);
        values.push_back(data_provider.ConsumeRandomLengthString(kNameMax));
        ippAddString(message, group, value_tag, name, /*language=*/NULL,
                     values.back().c_str());
        break;
      }
      default:
        // This shouldn't be reached.
        break;
    }
  }

  // Delete the IPP structure.
  // This is what's really being fuzzed.
  ippDelete(message);
}

}  // namespace
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  CupsIppTFuzz(data, size);
  names.clear();
  sequences.clear();
  return 0;
}
