// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mman.h>

#include <cstdint>
#include <cstdio>

#include <base/files/file_util.h>
#include <cups/ppd.h>

// This section contains fragments of ppd-private.h and language-private.h
// header files from CUPS that are not installed along with other headers.
// Tested function _ppdOpen is used directly by cupstestppd and indirectly by
// lpadmin.
extern "C" {

typedef enum _ppd_localization_e {
  _PPD_LOCALIZATION_DEFAULT,
  _PPD_LOCALIZATION_ICC_PROFILES,
  _PPD_LOCALIZATION_NONE,
  _PPD_LOCALIZATION_ALL
} _ppd_localization_t;

ppd_file_t* _ppdOpen(cups_file_t* fp, _ppd_localization_t localization);

void _cupsSetLocale(char* argv[]);
}

namespace {

// It creates temporary file descriptor and writes to it the given content.
// Created file is seek to the beginning. The function returns a file
// descriptor or value <0 in case of an error.
int create_file_descriptor_with_content(const uint8_t* data, size_t size) {
  // get a file descriptor to a memory buffer
  int fd_tmp = memfd_create("cups_ppdopen_fuzz", MFD_ALLOW_SEALING);
  if (fd_tmp < 0) {
    return -1;
  }
  // save content to the file descriptor
  const char* data2 = reinterpret_cast<const char*>(data);
  if (!base::WriteFileDescriptor(fd_tmp, data2, size)) {
    close(fd_tmp);
    return -2;
  }
  // seek to the beginning of the created file
  if (lseek(fd_tmp, 0, SEEK_SET) < 0) {
    close(fd_tmp);
    return -3;
  }
  // return the file descriptor
  return fd_tmp;
}

// It runs function for PPD parsing in the similar way as in cupstestppd.
int cups_ppdopen_fuzz(const uint8_t* data, size_t size) {
  // create file descriptor emulating standard input
  int file_descriptor = create_file_descriptor_with_content(data, size);
  if (file_descriptor < 0) {
    // cannot create file descriptor - report an error
    return file_descriptor;
  }
  // an array with command line parameters
  char argv0[] = "cupstestppd";
  char argv1[] = "-";
  char*(argv[3]){argv0, argv1, nullptr};
  // start testing
  // all these function calls below were copied from cupstestppd
  _cupsSetLocale(argv);
  ppdSetConformance(PPD_CONFORM_STRICT);
  cups_file_t* cups_file = cupsFileOpenFd(file_descriptor, "r");
  if (cups_file == nullptr) {
    // cannot initialize cups_file_t - report an error
    close(file_descriptor);
    return -11;
  }
  ppd_file_t* ppd_file = _ppdOpen(cups_file, _PPD_LOCALIZATION_ALL);
  if (ppd_file == nullptr) {
    int line;
    ppd_status_t error = ppdLastError(&line);
    if (error <= PPD_ALLOC_ERROR) {
      // this condition means I/O error - report an error
      cupsFileClose(cups_file);
      return -12;
    }
  } else {
    ppdClose(ppd_file);
  }
  // end of testing
  // clean and exit
  cupsFileClose(cups_file);
  return 0;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  cups_ppdopen_fuzz(data, size);
  return 0;
}
