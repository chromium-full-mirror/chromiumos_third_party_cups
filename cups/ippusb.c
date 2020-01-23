// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ippusb-private.h"
#include "language-private.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

int open_ippusb_manager_socket(void) {
  int fd;

  _cupsLangPrintf(stderr, _("Attempting to open socket"));

  fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if (fd < 0) {
    _cupsLangPrintf(stderr, _("Failed to open stream socket: %s"),
                    strerror(errno));
    _exit(1);
  }

  _cupsLangPrintf(stderr, _("Attempting to connect to socket"));

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, "/run/ippusb/ippusb_manager.sock");

  if (connect(fd, &addr, sizeof(struct sockaddr_un)) != 0) {
    close(fd);
    _cupsLangPrintf(stderr, _("Failed to connect to socket: %s"),
                    strerror(errno));
    _exit(1);
  }

  return fd;
}

// Writes a message to the socket described by |fd| as a stream of bytes. The
// first byte represents the length of the message, and the following bytes are
// filled using |msg|.
void send_message(int fd, const char* msg) {
  size_t remaining = strlen(msg) + 1;
  if (remaining > UINT8_MAX) {
    _cupsLangPrintf(stderr, _("The message to be sent is too large"));
    _exit(1);
  }

  // Send the length of the message.
  uint8_t message_length = (uint8_t) remaining;
  if (send(fd, &message_length, 1, 0) < 0) {
    _cupsLangPrintf(stderr, _("Failed to send message length"));
    _exit(1);
  }

  size_t total = 0;

  while (remaining > 0) {
    size_t sent = (size_t)send(fd, msg + total, remaining, MSG_NOSIGNAL);

    if (sent < 0) {
      _cupsLangPrintf(stderr, _("Failed to send message"));
      _exit(1);
    }

    total += sent;
    if (sent >= remaining)
      remaining = 0;
    else
      remaining -= sent;
  }
}

char* get_message(int fd) {
  // Poll the file descriptor first before trying to read. In the event that
  // ippusb_manager exited unexpectedly before responding on the socket we want
  // to be able to exit before blocking on read.
  struct pollfd poll_fd;
  poll_fd.fd = fd;
  poll_fd.events = POLLIN;
  int timeout = 5000;
  if (poll(&poll_fd, 1, timeout) <= 0) {
    _cupsLangPrintf(stderr, _("Failed to receive response"));
    _exit(1);
  }

  // Get the first byte out of the stream which contains the length of the
  // message.
  uint8_t message_length;
  if (recv(fd, &message_length, 1, 0) < 0) {
    _cupsLangPrintf(stderr, _("Failed to get message length"));
    _exit(1);
  }

  char* buf = (char*) malloc(sizeof(*buf) * message_length);
  size_t gotten_size;
  size_t total_size = 0;

  while (total_size < message_length) {
    gotten_size =
        (size_t)recv(fd, buf + total_size, message_length - total_size, 0);

    if (gotten_size < 0) {
      _cupsLangPrintf(stderr, _("Failed to receive message"));
      _exit(1);
    }

    total_size += gotten_size;
  }

  return buf;
}

char* query_ippusb_manager(int fd, const char* msg) {
  _cupsLangPrintf(stderr, _("Attempting to write to ippusb_manager socket"));
  send_message(fd, msg);

  _cupsLangPrintf(stderr, _("Attempting to read response"));
  char* response = get_message(fd);
  _cupsLangPrintf(stderr, _("Finished reading response"));

  if (!valid_response(response)) {
    _cupsLangPrintf(stderr, _("Invalid response: ``%s''"), response);
    _exit(1);
  }

  if (!strcasecmp(response, "device not found")) {
    _cupsLangPrintf(stderr, _("Device not found"));
    _exit(1);
  }

  return response;
}

int valid_response(const char* response) {
  if (!response)
    return 0;

  const char* p = response;
  while (*p) {
    if (!isalpha(*p) && !isdigit(*p) && *p != '_' && *p != '.' && *p != ' ' &&
        *p != '-')
      return 0;
    ++p;
  }

  return 1;
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
    _cupsLangPrintf(stderr, _("Could not \":\" separator in uri \"%s\""), uri);
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

void wait_for_socket(const char* filename, long timeout) {
  int fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if (fd < 0) {
    _cupsLangPrintf(stderr, _("Failed to create socket"));
    _exit(1);
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
    _exit(1);
  }

  while (connect(fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
    struct timespec current;
    if (clock_gettime(CLOCK_MONOTONIC, &current) < 0) {
      _cupsLangPrintf(stderr, _("Failed to get clock time"));
      _exit(1);
    }

    if (current.tv_sec - start.tv_sec >= timeout) {
      _cupsLangPrintf(stderr, _("Timed out waiting for socket %s"), filename);
      _exit(1);
    }
    usleep(100);
  }

  _cupsLangPrintf(stderr, _("%s is now ready for connections"), filename);

  close(fd);
}
