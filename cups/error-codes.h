// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _CUPS_ERROR_CODES_H_
#define _CUPS_ERROR_CODES_H_

/*
 * C++ magic...
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* All operations defined in this file have no effect outside the main thread.
 */

/* Error codes are used to annotate results of execution of chosen routines. */
typedef enum error_code_e {
  EC_NONE = 0,                /* no errors reported */
  EC_UNKNOWN = 1,             /* unspecified error occurred */
  EC_INPUT_PARAMETER,         /* a function got wrong parameter(s) */
  EC_IO,                      /* an operation on a file descriptor failed */
  EC_MEMORY,                  /* cannot allocate memory */
  EC_DESTINATION_UNREACHABLE, /* cannot reach the target host/device */
  EC_UNEXPECTED_RESPONSE,     /* host or device sent unexpected response */
  EC_IPP_ATTRIBUTE            /* missing or incorrect IPP attribute */
} error_code_t;

/* These functions are used in the macros defined below. Do not use
 * them directly.
 *   _ec_begin(...) opens a new "scope" with the name |func|.
 *   _ec_end(...) closes the current "scope" with the name |func| and
 *         reports the code |err|. The pointer value |func| must be
 *         the same as in corresponding _ec_begin(...).
 *   _ec_end_auto(...) does the same as _ec_end(...) but reports
 *         the last error code from sub-scopes or EC_UNKNOWN if
 *         no error codes were reported.
 * |func| must be not nullptr for all these functions.
 */
void _ec_begin(const char* const func);
void _ec_end(const char* const func, const error_code_t err);
void _ec_end_auto(const char* const func);

/* This macro must be put at the beginning of every function using RETURN_*
 * macros. After that, all return statements in this function must be replaced
 * by proper RETURN_* macro.
 */
#define EC_FUNC                                 \
  static const char* const _ec_func = __func__; \
  _ec_begin(_ec_func);

/* Returns |return_value| and report a success. */
#define RETURN_OK(return_value) \
  {                             \
    _ec_end(_ec_func, EC_NONE); \
    return (return_value);      \
  }

/* Returns |return_value| and report a corresponding error code. */
#define RETURN_FAIL_UNKNOWN(return_value) \
  {                                       \
    _ec_end(_ec_func, EC_UNKNOWN);        \
    return (return_value);                \
  }
#define RETURN_FAIL_INPUT_PARAMETER(return_value) \
  {                                               \
    _ec_end(_ec_func, EC_INPUT_PARAMETER);        \
    return (return_value);                        \
  }
#define RETURN_FAIL_IO(return_value) \
  {                                  \
    _ec_end(_ec_func, EC_IO);        \
    return (return_value);           \
  }
#define RETURN_FAIL_MEMORY(return_value) \
  {                                      \
    _ec_end(_ec_func, EC_MEMORY);        \
    return (return_value);               \
  }
#define RETURN_FAIL_DESTINATION_UNREACHABLE(return_value) \
  {                                                       \
    _ec_end(_ec_func, EC_DESTINATION_UNREACHABLE);        \
    return (return_value);                                \
  }
#define RETURN_FAIL_UNEXPECTED_RESPONSE(return_value) \
  {                                                   \
    _ec_end(_ec_func, EC_UNEXPECTED_RESPONSE);        \
    return (return_value);                            \
  }
#define RETURN_FAIL_IPP_ATTRIBUTE(return_value) \
  {                                             \
    _ec_end(_ec_func, EC_IPP_ATTRIBUTE);        \
    return (return_value);                      \
  }

/* Returns |return_value| and report an the last error code from the current
 * function or functions called within the current function. If no errors were
 * reported it reports EC_UNKNOWN.
 */
#define RETURN_FAIL(return_value) \
  {                               \
    _ec_end_auto(_ec_func);       \
    return (return_value);        \
  }

/* Returns the last error code reported in the current function or functions
 * called within the current function. If no errors were reported it returns
 * EC_NONE.
 */
error_code_t ec_last_error();

/*
 * C++ magic...
 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_CUPS_ERROR_CODES_H_ */
