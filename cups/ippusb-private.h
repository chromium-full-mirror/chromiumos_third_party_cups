// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _CUPS_IPPUSB_H_
#define _CUPS_IPPUSB_H_

#include <stddef.h>

// The ippusb URI is always expected to be in one of the two following formats:
//
//   ippusb://<vid>_<pid>/ipp/print // 28 characters
//   ipp://<vid>_<pid>/ipp/print    // 25 characters
//
// Where <vid> and <pid> are 4-digit hexidecimal integers. Since the URI is
// expected to match a strict format, the maximum size is well-defined. We allow
// for one more character than the larger scheme to account for the terminating
// NULL byte.
#define MAX_IPPUSB_URI 29

// Returns the base socket name expected for |host|.  The caller is responsible
// for freeing the returned string.
char* ippusb_host_to_socket_name(const char* host);

// Writes the contents of |uri| to |fixed_uri| but replacing the "scheme"
// portion with |scheme|. The given value |n| represents the maximum number of
// characters to be written to |fixed_uri|. Returns 1 on success, 0 on failure.
int change_scheme(const char* uri, const char* scheme, size_t n,
                  char* fixed_uri);

// Waits for a maximum time of |timeout| seconds until the socket at |filename|
// is ready to accept connections. Returns -1 on failure, 0 otherwise.
int wait_for_socket(const char* filename, long timeout);

#endif /* _CUPS_IPPUSB_H_ */
