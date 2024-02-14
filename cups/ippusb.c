// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "error-codes.h"
#include "ippusb-private.h"
#include "language-private.h"

#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

// NNNN-MMM.sock plus a terminator.
#define SOCKET_NAME_LEN 15

char* ippusb_host_to_socket_name(const char* host) {
  EC_FUNC;

  if (!host) {
    RETURN_FAIL_INPUT_PARAMETER(NULL);
  }

  _cupsLangPrintf(stderr, _("Looking up socket name for %s"), host);

  regex_t re;
  int ret = regcomp(&re, "^([0-9a-f]{4})[_-]([0-9a-f]{4})$", REG_ICASE | REG_EXTENDED);
  if (ret != 0) {
    char buf[256];
    regerror(ret, &re, buf, sizeof(buf));
    _cupsLangPrintf(stderr, _("Failed to compile regex: %s"), buf);
    regfree(&re);
    RETURN_FAIL_UNKNOWN(NULL);
  }

  // matches[0] holds the full match and then 1 per parenthesis in the regex.
  regmatch_t matches[3];
  ret = regexec(&re, host, 3, matches, 0);
  regfree(&re);
  if (ret != 0) {
    _cupsLangPrintf(stderr, _("IPP-USB regex did not match."));
    RETURN_FAIL_UNKNOWN(NULL);
  }

  // Copy out vid and pid from the matches.  We harcoded the number and length
  // of matches above, so we don't need to do any dynamic allocation here.
  char vid[5];
  strncpy(vid, host + matches[1].rm_so, 4);
  vid[4] = '\0';

  char pid[5];
  strncpy(pid, host + matches[2].rm_so, 4);
  pid[4] = '\0';

  char* response = malloc(SOCKET_NAME_LEN);
  snprintf(response, SOCKET_NAME_LEN, "%s-%s.sock", vid, pid);

  RETURN_OK(response);
}

int change_scheme(const char* uri, const char* scheme, size_t n,
                  char* fixed_uri) {
  if (!uri || !scheme || !fixed_uri) {
    _cupsLangPrintf(stderr, _("Received invalid arguments"));
    return 0;
  }

  // Find the ":" separator in |uri| which indicates the end of the scheme
  // portion.
  char* p = strchr(uri, ':');
  if (!p) {
    _cupsLangPrintf(stderr, _("Could not find \":\" separator in uri \"%s\""), uri);
    return 0;
  }

  // Attempt to write the contents of |uri| to |fixed_uri|, replacing the scheme
  // portion of |uri| with |scheme|.
  int ret = snprintf(fixed_uri, n, "%s%s", scheme, p);
  if (ret < 0 || ret >= n) {
    _cupsLangPrintf(stderr, _("Failed to write uri with new scheme \"%s\""),
                    scheme);
    return 0;
  }
  return 1;
}

int wait_for_socket(const char* filename, long timeout) {
  EC_FUNC;

  int fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if (fd < 0) {
    _cupsLangPrintf(stderr, _("Failed to create socket"));
    RETURN_FAIL_UNKNOWN(-1);
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, filename);

  _cupsLangPrintf(stderr, _("Waiting for %s to be ready for connections"),
                  filename);

  struct timespec start;
  if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
    _cupsLangPrintf(stderr, _("Failed to get clock time"));
    close(fd);
    RETURN_FAIL_UNKNOWN(-1);
  }

  int ret;
  while ((ret = connect(fd, (struct sockaddr*) &addr, sizeof(addr))) < 0) {
    int errsv = errno;
    struct timespec current;
    if (clock_gettime(CLOCK_MONOTONIC, &current) < 0) {
      _cupsLangPrintf(stderr, _("Failed to get clock time"));
      close(fd);
      RETURN_FAIL_UNKNOWN(-1);
    }

    if (current.tv_sec - start.tv_sec >= timeout) {
      _cupsLangPrintf(stderr,
                      _("Timed out waiting for socket %s, last error %d"),
                      filename, errsv);
      close(fd);
      RETURN_FAIL_DESTINATION_UNREACHABLE(ret);
    }
    usleep(100);
  }

  _cupsLangPrintf(stderr, _("%s is now ready for connections"), filename);

  close(fd);
  RETURN_OK(0);
}
