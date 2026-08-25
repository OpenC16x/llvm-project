/* errno, for a part with one thread and no operating system.
 *
 * The value is a single int in the C library, because there is nothing here
 * for it to be per-thread of.  It is reached through a macro rather than
 * named directly, which is what the standard asks for and what lets this
 * change later without every user of it changing too.
 *
 * The numbers are the ones Linux uses.  Nothing on this part produces most of
 * them - there are no files and no processes - but code written against a
 * hosted C library tests for them by name, and giving them the values everyone
 * else gives them costs nothing and means a value that crosses a boundary
 * means the same on both sides.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_ERRNO_H
#define _C166_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

extern int __c166_errno;

#ifdef __cplusplus
}
#endif

#define errno __c166_errno

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define ENOTBLK 15
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define EDEADLK 35
#define ENAMETOOLONG 36
#define ENOLCK 37
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define EWOULDBLOCK EAGAIN
#define EOVERFLOW 75
#define EILSEQ 84
#define ENOTSUP 95
#define EOPNOTSUPP 95
#define EAFNOSUPPORT 97
#define EADDRINUSE 98
#define ECONNRESET 104
#define ETIMEDOUT 110
#define ECONNREFUSED 111

#endif /* _C166_ERRNO_H */
