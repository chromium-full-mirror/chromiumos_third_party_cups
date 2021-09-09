// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppd-cache-private.h"

#include <ctype.h>
#include <string.h>

/*
 * 'pwg_unppdize_name()' - Convert a PPD keyword to a lowercase IPP keyword.
 */
void pwg_unppdize_name(
    const char* ppd,         // I - PPD keyword
    char* name,              // O - Name buffer
    size_t namesize,         // I - Size of name buffer
    const char* dashchars,   // I - Characters to be replaced by dashes
    const int exempt_x_dot)  // I - whether to dash-separate [x.][0-9]
{
  if (namesize <= 0)
    return;

  char* ptr;      /* Pointer into name buffer */
  char* end;      /* End of name buffer */
  int nodash = 1; /* Next char in IPP name cannot be a
                     dash (first char or after a dash) */

  if (islower(*ppd)) {
    /*
     * Already lowercase name, use as-is?
     */

    const char* ppdptr; /* Pointer into PPD keyword */

    for (ppdptr = ppd + 1; *ppdptr; ppdptr++)
      if (isupper(*ppdptr) || strchr(dashchars, *ppdptr) ||
          (*ppdptr == '-' && *(ppdptr - 1) == '-') ||
          (*ppdptr == '-' && *(ppdptr + 1) == '\0'))
        break;

    if (!*ppdptr) {
      strncpy(name, ppd, namesize);
      name[namesize - 1] = '\0';
      return;
    }
  }

  for (ptr = name, end = name + namesize - 1; *ppd && ptr < end; ppd++) {
    if (isalnum(*ppd)) {
      *ptr++ = (char)tolower(*ppd);
      nodash = 0;
    } else if (*ppd == '-' || strchr(dashchars, *ppd)) {
      if (nodash == 0) {
        *ptr++ = '-';
        nodash = 1;
      }
    } else {
      *ptr++ = *ppd;
      nodash = 0;
    }

    /*
     * We might be looking at the end of our allotted rope. Break out
     * early and lay down the NUL byte if we are.
     */
    if (ptr == end) {
      break;
    }

    if (nodash == 0) {
      if (!isupper(*ppd) && isalnum(*ppd) && isupper(ppd[1])) {
        *ptr++ = '-';
        nodash = 1;
      } else if (!isdigit(*ppd) && isdigit(ppd[1])) {
        if (!exempt_x_dot || (*ppd != 'x' && *ppd != '.')) {
          *ptr++ = '-';
          nodash = 1;
        }
      }
    }
  }

  /* Remove trailing dashes */
  while (ptr > name && *(ptr - 1) == '-')
    ptr--;

  *ptr = '\0';
}
