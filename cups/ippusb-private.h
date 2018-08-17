// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _CUPS_IPPUSB_H_
#define _CUPS_IPPUSB_H_

// Attempts to open the socket used to communicate with ippusb_manager, and if
// successful returns a file descriptor of the socket.
int open_ippusb_manager_socket(void);

// Sends the provided message through the stream socket referred to by |fd|.
void send_message(int fd, const char* msg);

// Attempts to receive a message from the stream socket referred to by |fd|.
char* get_message(int fd);

// Sends a query message to the ippusb_manager through the socket |fd|, and
// returns the response from ippusb_manager.
char* query_ippusb_manager(int fd, const char* msg);

// Verifies that the response string is not null and does not contain any
// invalid characters.
int valid_response(const char* response);

// Returns a new a new uri based on the given |uri| that replaces the scheme
// prefix with the given |scheme|.
char* change_scheme(const char* uri, const char* scheme);

// Waits for a maximum time of |timeout| until the socket at |filename| is ready
// to accept connections.
void wait_for_socket(const char* filename, long timeout);

#endif /* _CUPS_IPPUSB_H_ */
