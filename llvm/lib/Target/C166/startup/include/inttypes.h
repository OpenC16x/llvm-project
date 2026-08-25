/* Widest integers, and the printf and scanf length modifiers for the sized
 * ones.
 *
 * Every format string here is built out of a macro clang predefines from the
 * target's own type mapping - __INT32_FMTd__ is "ld" on this part because
 * int32_t is long here, and would be "d" on a target where it is int.  Writing
 * the letters out by hand would be writing down a second copy of the type
 * mapping, and a copy that a change to the target would not update.  This way
 * there is one copy and the compiler owns it.
 *
 * The sysroot is searched before clang's own header directory, so this file is
 * the <inttypes.h> a program gets and clang's - which does nothing but
 * include_next to a hosted one - is never reached.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_INTTYPES_H
#define _C166_INTTYPES_H

#include <stdint.h>

typedef struct {
  intmax_t quot;
  intmax_t rem;
} imaxdiv_t;

#define PRId8 __INT8_FMTd__
#define PRId16 __INT16_FMTd__
#define PRId32 __INT32_FMTd__
#define PRId64 __INT64_FMTd__
#define PRIdLEAST8 __INT_LEAST8_FMTd__
#define PRIdLEAST16 __INT_LEAST16_FMTd__
#define PRIdLEAST32 __INT_LEAST32_FMTd__
#define PRIdLEAST64 __INT_LEAST64_FMTd__
#define PRIdFAST8 __INT_FAST8_FMTd__
#define PRIdFAST16 __INT_FAST16_FMTd__
#define PRIdFAST32 __INT_FAST32_FMTd__
#define PRIdFAST64 __INT_FAST64_FMTd__
#define PRIdPTR __INTPTR_FMTd__
#define PRIdMAX __INTMAX_FMTd__

#define PRIi8 __INT8_FMTi__
#define PRIi16 __INT16_FMTi__
#define PRIi32 __INT32_FMTi__
#define PRIi64 __INT64_FMTi__
#define PRIiLEAST8 __INT_LEAST8_FMTi__
#define PRIiLEAST16 __INT_LEAST16_FMTi__
#define PRIiLEAST32 __INT_LEAST32_FMTi__
#define PRIiLEAST64 __INT_LEAST64_FMTi__
#define PRIiFAST8 __INT_FAST8_FMTi__
#define PRIiFAST16 __INT_FAST16_FMTi__
#define PRIiFAST32 __INT_FAST32_FMTi__
#define PRIiFAST64 __INT_FAST64_FMTi__
#define PRIiPTR __INTPTR_FMTi__
#define PRIiMAX __INTMAX_FMTi__

#define PRIo8 __UINT8_FMTo__
#define PRIo16 __UINT16_FMTo__
#define PRIo32 __UINT32_FMTo__
#define PRIo64 __UINT64_FMTo__
#define PRIoLEAST8 __UINT_LEAST8_FMTo__
#define PRIoLEAST16 __UINT_LEAST16_FMTo__
#define PRIoLEAST32 __UINT_LEAST32_FMTo__
#define PRIoLEAST64 __UINT_LEAST64_FMTo__
#define PRIoFAST8 __UINT_FAST8_FMTo__
#define PRIoFAST16 __UINT_FAST16_FMTo__
#define PRIoFAST32 __UINT_FAST32_FMTo__
#define PRIoFAST64 __UINT_FAST64_FMTo__
#define PRIoPTR __UINTPTR_FMTo__
#define PRIoMAX __UINTMAX_FMTo__

#define PRIu8 __UINT8_FMTu__
#define PRIu16 __UINT16_FMTu__
#define PRIu32 __UINT32_FMTu__
#define PRIu64 __UINT64_FMTu__
#define PRIuLEAST8 __UINT_LEAST8_FMTu__
#define PRIuLEAST16 __UINT_LEAST16_FMTu__
#define PRIuLEAST32 __UINT_LEAST32_FMTu__
#define PRIuLEAST64 __UINT_LEAST64_FMTu__
#define PRIuFAST8 __UINT_FAST8_FMTu__
#define PRIuFAST16 __UINT_FAST16_FMTu__
#define PRIuFAST32 __UINT_FAST32_FMTu__
#define PRIuFAST64 __UINT_FAST64_FMTu__
#define PRIuPTR __UINTPTR_FMTu__
#define PRIuMAX __UINTMAX_FMTu__

#define PRIx8 __UINT8_FMTx__
#define PRIx16 __UINT16_FMTx__
#define PRIx32 __UINT32_FMTx__
#define PRIx64 __UINT64_FMTx__
#define PRIxLEAST8 __UINT_LEAST8_FMTx__
#define PRIxLEAST16 __UINT_LEAST16_FMTx__
#define PRIxLEAST32 __UINT_LEAST32_FMTx__
#define PRIxLEAST64 __UINT_LEAST64_FMTx__
#define PRIxFAST8 __UINT_FAST8_FMTx__
#define PRIxFAST16 __UINT_FAST16_FMTx__
#define PRIxFAST32 __UINT_FAST32_FMTx__
#define PRIxFAST64 __UINT_FAST64_FMTx__
#define PRIxPTR __UINTPTR_FMTx__
#define PRIxMAX __UINTMAX_FMTx__

#define PRIX8 __UINT8_FMTX__
#define PRIX16 __UINT16_FMTX__
#define PRIX32 __UINT32_FMTX__
#define PRIX64 __UINT64_FMTX__
#define PRIXLEAST8 __UINT_LEAST8_FMTX__
#define PRIXLEAST16 __UINT_LEAST16_FMTX__
#define PRIXLEAST32 __UINT_LEAST32_FMTX__
#define PRIXLEAST64 __UINT_LEAST64_FMTX__
#define PRIXFAST8 __UINT_FAST8_FMTX__
#define PRIXFAST16 __UINT_FAST16_FMTX__
#define PRIXFAST32 __UINT_FAST32_FMTX__
#define PRIXFAST64 __UINT_FAST64_FMTX__
#define PRIXPTR __UINTPTR_FMTX__
#define PRIXMAX __UINTMAX_FMTX__

/* scanf takes the same length modifiers, with one exception the standard
 * makes: there is no %hhi or %hi conversion for a scan of exactly 8 or 16
 * bits that differs from the print form, so these are the same strings. */
#define SCNd8 __INT8_FMTd__
#define SCNd16 __INT16_FMTd__
#define SCNd32 __INT32_FMTd__
#define SCNd64 __INT64_FMTd__
#define SCNdLEAST8 __INT_LEAST8_FMTd__
#define SCNdLEAST16 __INT_LEAST16_FMTd__
#define SCNdLEAST32 __INT_LEAST32_FMTd__
#define SCNdLEAST64 __INT_LEAST64_FMTd__
#define SCNdFAST8 __INT_FAST8_FMTd__
#define SCNdFAST16 __INT_FAST16_FMTd__
#define SCNdFAST32 __INT_FAST32_FMTd__
#define SCNdFAST64 __INT_FAST64_FMTd__
#define SCNdPTR __INTPTR_FMTd__
#define SCNdMAX __INTMAX_FMTd__

#define SCNi8 __INT8_FMTi__
#define SCNi16 __INT16_FMTi__
#define SCNi32 __INT32_FMTi__
#define SCNi64 __INT64_FMTi__
#define SCNiLEAST8 __INT_LEAST8_FMTi__
#define SCNiLEAST16 __INT_LEAST16_FMTi__
#define SCNiLEAST32 __INT_LEAST32_FMTi__
#define SCNiLEAST64 __INT_LEAST64_FMTi__
#define SCNiFAST8 __INT_FAST8_FMTi__
#define SCNiFAST16 __INT_FAST16_FMTi__
#define SCNiFAST32 __INT_FAST32_FMTi__
#define SCNiFAST64 __INT_FAST64_FMTi__
#define SCNiPTR __INTPTR_FMTi__
#define SCNiMAX __INTMAX_FMTi__

#define SCNo8 __UINT8_FMTo__
#define SCNo16 __UINT16_FMTo__
#define SCNo32 __UINT32_FMTo__
#define SCNo64 __UINT64_FMTo__
#define SCNoLEAST8 __UINT_LEAST8_FMTo__
#define SCNoLEAST16 __UINT_LEAST16_FMTo__
#define SCNoLEAST32 __UINT_LEAST32_FMTo__
#define SCNoLEAST64 __UINT_LEAST64_FMTo__
#define SCNoFAST8 __UINT_FAST8_FMTo__
#define SCNoFAST16 __UINT_FAST16_FMTo__
#define SCNoFAST32 __UINT_FAST32_FMTo__
#define SCNoFAST64 __UINT_FAST64_FMTo__
#define SCNoPTR __UINTPTR_FMTo__
#define SCNoMAX __UINTMAX_FMTo__

#define SCNu8 __UINT8_FMTu__
#define SCNu16 __UINT16_FMTu__
#define SCNu32 __UINT32_FMTu__
#define SCNu64 __UINT64_FMTu__
#define SCNuLEAST8 __UINT_LEAST8_FMTu__
#define SCNuLEAST16 __UINT_LEAST16_FMTu__
#define SCNuLEAST32 __UINT_LEAST32_FMTu__
#define SCNuLEAST64 __UINT_LEAST64_FMTu__
#define SCNuFAST8 __UINT_FAST8_FMTu__
#define SCNuFAST16 __UINT_FAST16_FMTu__
#define SCNuFAST32 __UINT_FAST32_FMTu__
#define SCNuFAST64 __UINT_FAST64_FMTu__
#define SCNuPTR __UINTPTR_FMTu__
#define SCNuMAX __UINTMAX_FMTu__

#define SCNx8 __UINT8_FMTx__
#define SCNx16 __UINT16_FMTx__
#define SCNx32 __UINT32_FMTx__
#define SCNx64 __UINT64_FMTx__
#define SCNxLEAST8 __UINT_LEAST8_FMTx__
#define SCNxLEAST16 __UINT_LEAST16_FMTx__
#define SCNxLEAST32 __UINT_LEAST32_FMTx__
#define SCNxLEAST64 __UINT_LEAST64_FMTx__
#define SCNxFAST8 __UINT_FAST8_FMTx__
#define SCNxFAST16 __UINT_FAST16_FMTx__
#define SCNxFAST32 __UINT_FAST32_FMTx__
#define SCNxFAST64 __UINT_FAST64_FMTx__
#define SCNxPTR __UINTPTR_FMTx__
#define SCNxMAX __UINTMAX_FMTx__

#ifdef __cplusplus
extern "C" {
#endif

intmax_t imaxabs(intmax_t v);
imaxdiv_t imaxdiv(intmax_t num, intmax_t den);
intmax_t strtoimax(const char *s, char **end, int base);
uintmax_t strtoumax(const char *s, char **end, int base);

#ifdef __cplusplus
}
#endif

#endif /* _C166_INTTYPES_H */
