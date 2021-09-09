// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#ifndef _CUPS_PPD_CACHE_PRIVATE_H_
#define _CUPS_PPD_CACHE_PRIVATE_H_

/*
 * C++ magic...
 */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Converts a name in a PPD, |ppd|, into a name suitable for inclusion as an IPP
// attribute, |name|. |namesize| specifies the length of the |name| buffer. If
// the buffer length is 0, |name| is not modified. |dashchars| specifies
// characters that should be replaced by dashes. |exempt_x_dot| specifies if "x"
// and "." should be exempt from dash separation.
void pwg_unppdize_name(const char* ppd,
                       char* name,
                       size_t namesize,
                       const char* dashchars,
                       int exempt_x_dot);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_CUPS_PPD_CACHE_PRIVATE_H_ */
