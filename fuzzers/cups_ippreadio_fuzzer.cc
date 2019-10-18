// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <cups/ipp.h>

extern "C" {

// Help structure storing information about unread part of the input buffer.
struct DataToRead {
  // Pointer to the first unread byte.
  const uint8_t* buffer;
  // Number of remaining bytes to read.
  size_t size;
};

// Writes content of |context|, which is a DataToRead, into |buffer|.
// Writes at most |bytes| bytes. Returns number of bytes read.
static ssize_t ipp_read_data(void* context, ipp_uchar_t* buffer, size_t bytes) {
  DataToRead* data = reinterpret_cast<DataToRead*>(context);
  if (data->size < bytes)
    bytes = data->size;
  memcpy(buffer, data->buffer, bytes);
  data->buffer += bytes;
  data->size -= bytes;
  return bytes;
}
}

namespace {

// It fuzzes CUPS function ippReadIO(...) for parsing IPP packages.
void cups_ippreadio_fuzz(const uint8_t* data, size_t size) {
  DataToRead data_to_read;
  data_to_read.buffer = data;
  data_to_read.size = size;

  // Create IPP structure from CUPS and call the tested function.
  ipp_t* ipp = ippNew();
  ippReadIO(&data_to_read, ipp_read_data, 1, NULL, ipp);

  // This code is a modified content of debug_attributes(ipp_t*) function from
  // ipp.c file. It just iterates over attributes and calls some functions.
  {
    ipp_tag_t group = IPP_TAG_ZERO;                  // Current group
    ipp_attribute_t* attr = ippFirstAttribute(ipp);  // Current attribute
    char buffer[1024];                               // Value buffer

    for (; attr; attr = ippNextAttribute(ipp)) {
      const char* name = ippGetName(attr);
      if (!name) {
        group = IPP_TAG_ZERO;
        continue;
      }
      if (group != ippGetGroupTag(attr)) {
        group = ippGetGroupTag(attr);
      }
      ippTagString(group);
      ippAttributeString(attr, buffer, sizeof(buffer));
      ippGetCount(attr);
      ippTagString(ippGetValueTag(attr));
    }
  }

  // Delete the IPP structure.
  ippDelete(ipp);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  cups_ippreadio_fuzz(data, size);
  return 0;
}
