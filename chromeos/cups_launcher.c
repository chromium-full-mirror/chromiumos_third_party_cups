// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

static const char kSocketPath[] = "/run/cups/cups.sock";
static const char kSocketGroup[] = "lp";
static const char kRegularConfig[] = "/etc/cups/cupsd.conf";
static const char kDebugConfig[] = "/etc/cups/cupsd-debug.conf";
static const char kCupsdPath[] = "/usr/sbin/cupsd";
static const char kDebugTriggerPath[] = "/run/cups/debug/debug-flag";
static const char kCupsDebugLevel[] = "5";

// cupsd debug messages aren't organized in any coherent way, so this is
// essentially a list of DEBUG_printf message prefixes that contain useful
// information.  This list tries to capture most of the parsing and HTTP/IPP
// handling while omitting as much as possible of the low-level tracking of
// bytes sent and received.
static const char kCupsDebugFilterRegex[] =
    "^("
    "httpAddr"
    "|httpConnect"
    "|httpGetHost"
    "|httpReadRequest:"
    "|httpReconnect2:"
    "|httpSet"
    "|httpUpdate"
    "|httpWriteResponse"
    "|http_create"
    "|http_resolve"
    "|_httpResolveURI"
    "|_httpTLSStart"
    "|_httpTLSStop"
    "|_httpUpdate:"
    "|ippAdd"
    "|ippWriteIO:"
    "|_ipp"
    "|cupsAddDestMediaOptions"
    "|cupsAddOption"
    "|cupsDo"
    "|cupsFileOpen"
    "|cupsGetDest"
    "|cupsGetDevices"
    "|cupsGetNamed"
    "|cupsGetOption"
    "|cupsGetPPD"
    "|cupsGetResponse"
    "|cupsParse"
    "|cupsSend"
    "|cupsWrite"
    "|cups_dnssd"
    "|cups_get_printer"
    "|_cupsGetDest"
    "|_ppd"
    "|ppd"
    ")";

// Removes the file for the listening socket without failing if the file is
// missing.  Returns 0 on success or an errno value on failure.
static int remove_socket() {
  if (unlink(kSocketPath) < 0) {
    if (errno != ENOENT) {
      syslog(LOG_ERR, "Unable to unlink %s: %s\n", kSocketPath,
             strerror(errno));
      return errno;
    }
  }

  return 0;
}

// Do-nothing signal handler.  This is used just to allow epoll calls to be
// cleanly interrupted by signals.
static void handle_signal(int signum){};

int main(int argc, char** argv) {
  // Look up the group we will chown the socket file to.
  errno = 0;  // Needed to detect missing group vs error.
  struct group* grpinfo = getgrnam(kSocketGroup);
  if (!grpinfo) {
    syslog(LOG_ERR, "Unable to look up group %s: %s", kSocketGroup,
           strerror(errno));
    return 1;
  }
  gid_t sockgroup = grpinfo->gr_gid;

  int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd < 0) {
    syslog(LOG_ERR, "Unable to create socket: %s", strerror(errno));
    return 1;
  }

  // Add the socket to epoll immediately so we don't have a hole where events
  // can be lost between the bind and epoll.
  int pollfd = epoll_create1(EPOLL_CLOEXEC);
  if (pollfd < 0) {
    syslog(LOG_ERR, "Failed to create epoll fd: %s", strerror(errno));
    remove_socket();
    return 1;
  }
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = EPOLLIN;
  if (epoll_ctl(pollfd, EPOLL_CTL_ADD, sockfd, &event) < 0) {
    syslog(LOG_ERR, "Failed to call epoll_ctl: %s", strerror(errno));
    remove_socket();
    return 1;
  }

  if (remove_socket()) {
    return 1;
  }

  struct sockaddr_un my_addr;
  memset(&my_addr, 0, sizeof(my_addr));
  my_addr.sun_family = AF_UNIX;
  strncpy(my_addr.sun_path, kSocketPath, sizeof(my_addr.sun_path) - 1);

  // Only user and group should be able to connect.
  mode_t mask = umask(0117);
  if (bind(sockfd, (struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
    syslog(LOG_ERR, "Failed to bind to %s: %s", kSocketPath, strerror(errno));
    remove_socket();
    return 1;
  }
  umask(mask);

  // Change the group owner of the socket to the expected group.  There is a
  // small window in which the group on the socket is our initial group instead
  // of the intended one.  However, cupsd is started as a group that contains no
  // other users, so this is a window of reduced permissions rather than a
  // window of excess access.
  if (chown(kSocketPath, -1, sockgroup)) {
    syslog(LOG_ERR, "Failed to change %s to group %d: %s", kSocketPath,
           sockgroup, strerror(errno));
    remove_socket();
    return 1;
  }

  if (listen(sockfd, SOMAXCONN) < 0) {
    syslog(LOG_ERR, "Failed to listen on %s: %s", kSocketPath, strerror(errno));
    remove_socket();
    return 1;
  }

  // Set up environment variables to mimic upstart socket activation.
  char fdstr[11];
  snprintf(fdstr, sizeof(fdstr), "%d", sockfd);
  setenv("UPSTART_FDS", fdstr, 1);
  setenv("UPSTART_EVENTS", "socket", 1);

  // We run as pid 1 inside the minijail, so the kernel blocks signals by
  // default.  Set up signal handlers here to say that we want these signals
  // delivered.  The signal handler doesn't do anything other than allow
  // epoll_wait to be interrupted.
  signal(SIGINT, handle_signal);
  signal(SIGKILL, handle_signal);
  signal(SIGTERM, handle_signal);

  syslog(LOG_INFO, "Waiting for events on %s", kSocketPath);
  int count = epoll_wait(pollfd, &event, 1, -1);
  if (count < 0 && errno != EINTR) {
    syslog(LOG_ERR, "Failed to call epoll_wait: %s", strerror(errno));
    remove_socket();
    return 1;
  }
  if (count == 0 || (count < 0 && errno == EINTR)) {
    syslog(LOG_INFO, "Stopping after signal received");
    remove_socket();
    return 0;
  }

  // There is at least one event.  We only added one fd above, so we know
  // the only entry in event is exactly the incoming socket.
  if (!(event.events & EPOLLIN)) {
    syslog(LOG_ERR, "Unexpected event %u from epoll_wait: %s", event.events,
           strerror(errno));
    remove_socket();
    return 1;
  }

  // Pass a debug config file to cupsd if /run/cups/debug/debug-flag exists.
  // Otherwise pass the normal config.  We check here instead of up front so
  // that the flag can be changed at runtime, not just at boot.
  struct stat sb;
  const char* config_path;
  if (stat(kDebugTriggerPath, &sb) < 0) {
    if (errno != ENOENT) {
      syslog(LOG_ERR, "Failed to stat %s: %s", kDebugTriggerPath,
             strerror(errno));
      remove_socket();
      return 1;
    }
    config_path = kRegularConfig;
  } else {
    // The file exists.  We don't care what attributes were returned from stat.
    config_path = kDebugConfig;

    // Enable extra cupsd debugging with some extra environment variables.
    // We don't replace the environment so that people can override these
    // by hand-editing the init script.
    setenv("CUPS_DEBUG_LEVEL", kCupsDebugLevel, 0);
    setenv("CUPS_DEBUG_LOG", "-", 0);
    setenv("CUPS_DEBUG_FILTER", kCupsDebugFilterRegex, 0);
  }

  // Replace this process with cupsd.  Upstart will manage cleaning up and
  // starting over when it exits.
  syslog(LOG_INFO, "Launching cupsd with config %s", config_path);
  if (execl(kCupsdPath, kCupsdPath, "-f", "-l", "-c", config_path,
            (char*)NULL) < 0) {
    syslog(LOG_ERR, "Failed to exec %s: %s", kCupsdPath, strerror(errno));
    remove_socket();
    return 1;
  }

  // Unreachable.
  return 0;
}
