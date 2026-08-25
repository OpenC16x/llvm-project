/* Locales, of which this part has one.
 *
 * locale_t is an object type because the standard says the _l functions take
 * one, not because there is anything to put in it.  Everything is the C
 * locale; there is nowhere for another to come from.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_LOCALE_H
#define _C166_LOCALE_H

#include <stddef.h>

#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
#define LC_MESSAGES 6

#define LC_ALL_MASK 0x7F
#define LC_COLLATE_MASK (1 << LC_COLLATE)
#define LC_CTYPE_MASK (1 << LC_CTYPE)
#define LC_MONETARY_MASK (1 << LC_MONETARY)
#define LC_NUMERIC_MASK (1 << LC_NUMERIC)
#define LC_TIME_MASK (1 << LC_TIME)
#define LC_MESSAGES_MASK (1 << LC_MESSAGES)

struct __locale_data;

typedef struct __locale_t {
  struct __locale_data *__data;
} *locale_t;

struct lconv {
  char *decimal_point;
  char *thousands_sep;
  char *grouping;
  char *int_curr_symbol;
  char *currency_symbol;
  char *mon_decimal_point;
  char *mon_thousands_sep;
  char *mon_grouping;
  char *positive_sign;
  char *negative_sign;
  char int_frac_digits;
  char frac_digits;
  char p_cs_precedes;
  char p_sep_by_space;
  char n_cs_precedes;
  char n_sep_by_space;
  char p_sign_posn;
  char n_sign_posn;
};

#ifdef __cplusplus
extern "C" {
#endif

char *setlocale(int category, const char *name);
struct lconv *localeconv(void);
locale_t newlocale(int mask, const char *name, locale_t base);
locale_t uselocale(locale_t newloc);
void freelocale(locale_t loc);

#ifdef __cplusplus
}
#endif

#endif /* _C166_LOCALE_H */
