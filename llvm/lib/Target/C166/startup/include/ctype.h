/* <ctype.h>, all of which exists.
 *
 * These are the one part of the C library that a part like this gets for
 * nothing: every one of them is a decision about a single character, with no
 * memory, no allocation and no locale behind it.  LLVM's own libc supplies
 * them and they are in libc.a, so unlike <string.h> and <stdlib.h> next door
 * there is nothing here that is declared and not defined.
 *
 * The argument is an int holding either an unsigned char or EOF, which is what
 * the standard says and what the implementations behind these assume; passing
 * a plain char that happened to be negative is undefined there as it is
 * anywhere else.  None of them is a macro here, because there is no locale to
 * consult and so nothing a macro would save.
 *
 * isascii and toascii are not in the C standard - they are POSIX, and older
 * than it - but they are what libc.a defines and a program written for a part
 * this size is as likely to want them as the rest.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_CTYPE_H
#define _C166_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

int isascii(int c);
int toascii(int c);

#ifdef __cplusplus
}
#endif

#endif /* _C166_CTYPE_H */
