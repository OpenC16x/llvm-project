/*===-- unwind.c - Walking a C166 stack with the program's own CFI ---*- C -*-=*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM
|* Exceptions.  See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/
/*
 * A DWARF unwinder small enough to live on the part.  It reads the .eh_frame
 * the compiler emitted, runs the call frame instructions for the address a
 * frame is at, and applies the resulting rules to step back one frame.
 *
 * Two things are different from the same job on a hosted machine.
 *
 * The tables are in Flash, above the first 64 KByte, so every read of them
 * goes through a far pointer.  Nothing else here does: the registers and the
 * rules are in RAM, which a near pointer reaches.
 *
 * And a return address is on the system stack rather than in a register or at
 * an offset from the canonical frame address, so the rule for it is a DWARF
 * expression.  The expressions the compiler emits are built from five
 * operations - see C166MCTargetDesc.cpp - and the evaluator below covers
 * those and says so when it meets anything else, rather than guessing.
 */

#include "unwind.h"

void __cxa_call_terminate(void);

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef signed long s32;
typedef signed short s16;

typedef const __far u8 *fptr;

extern __far const u8 __eh_frame_start[];
extern __far const u8 __eh_frame_end[];

/*--- Reading the tables ---------------------------------------------------*/

static u8 f8(fptr *p) { u8 v = **p; *p += 1; return v; }

static u16 f16(fptr *p) {
  u16 v = (u16)(*p)[0] | ((u16)(*p)[1] << 8);
  *p += 2;
  return v;
}

static u32 f32(fptr *p) {
  u32 v = (u32)(*p)[0] | ((u32)(*p)[1] << 8) | ((u32)(*p)[2] << 16) |
          ((u32)(*p)[3] << 24);
  *p += 4;
  return v;
}

static u32 uleb(fptr *p) {
  u32 v = 0;
  unsigned shift = 0;
  u8 b;
  do {
    b = f8(p);
    v |= (u32)(b & 0x7F) << shift;
    shift += 7;
  } while (b & 0x80);
  return v;
}

static s32 sleb(fptr *p) {
  s32 v = 0;
  unsigned shift = 0;
  u8 b;
  do {
    b = f8(p);
    v |= (s32)(u32)(b & 0x7F) << shift;
    shift += 7;
  } while (b & 0x80);
  if (shift < 32 && (b & 0x40))
    v |= -((s32)1 << shift);
  return v;
}

/* The pointer encodings that appear here.  DW_EH_PE_omit is 0xFF; the low
 * nibble is the format and the high nibble how it is applied.  The compiler
 * emits absptr for the personality routine and the language specific data,
 * udata2 for a type table entry and pcrel|sdata4 for the address a frame
 * description entry describes; see C166TargetObjectFile.cpp. */
#define DW_EH_PE_omit    0xFF
#define DW_EH_PE_absptr  0x00
#define DW_EH_PE_uleb128 0x01
#define DW_EH_PE_udata2  0x02
#define DW_EH_PE_udata4  0x03
#define DW_EH_PE_sleb128 0x09
#define DW_EH_PE_sdata2  0x0A
#define DW_EH_PE_sdata4  0x0B
#define DW_EH_PE_pcrel   0x10

static u32 encoded(fptr *p, u8 enc) {
  fptr here = *p;
  u32 v = 0;
  if (enc == DW_EH_PE_omit)
    return 0;
  switch (enc & 0x0F) {
  case DW_EH_PE_absptr: v = f32(p); break;  /* four bytes: a 24 bit address */
  case DW_EH_PE_uleb128: v = uleb(p); break;
  case DW_EH_PE_udata2: v = f16(p); break;
  case DW_EH_PE_udata4: v = f32(p); break;
  case DW_EH_PE_sleb128: v = (u32)sleb(p); break;
  case DW_EH_PE_sdata2: v = (u32)(s32)(s16)f16(p); break;
  case DW_EH_PE_sdata4: v = f32(p); break;
  default: return 0;
  }
  if ((enc & 0x70) == DW_EH_PE_pcrel)
    v += (u32)here;
  return v;
}

/*--- What the tables say about one frame ----------------------------------*/

/* The registers a rule can be about, packed so that the table below is small:
 * the sixteen general purpose ones, then the four the unwinder needs names
 * for.  A DWARF number outside these is one nothing here emits. */
#define NREGS 20
#define REG_PSW 16
#define REG_SYSSP 17
#define REG_CSP 18
#define REG_PC 19

#define DW_PSW 32
#define DW_SYSSP 36
#define DW_CSP 44
#define DW_PC 45

static int slotOf(u32 dwreg) {
  if (dwreg < 16)
    return (int)dwreg;
  switch (dwreg) {
  case DW_PSW: return REG_PSW;
  case DW_SYSSP: return REG_SYSSP;
  case DW_CSP: return REG_CSP;
  case DW_PC: return REG_PC;
  default: return -1;
  }
}

enum {
  RULE_UNDEFINED = 0, /* nothing says where it is; treat as lost      */
  RULE_SAME,          /* the caller's copy is the one we have         */
  RULE_OFFSET,        /* at CFA + arg                                 */
  RULE_VAL_OFFSET,    /* is CFA + arg                                 */
  RULE_REGISTER,      /* in another register                          */
  RULE_EXPR,          /* at the address the expression computes       */
  RULE_VAL_EXPR       /* is what the expression computes              */
};

/* A rule is four bytes, and the shape is the reason.  Two Rows are live at the
 * deepest point of a walk - the one __unw_step is building and the one
 * frameInfo keeps to restore from - so every byte here is paid for twice, and
 * a Row is twenty rules.  At the ten bytes this used to be that came to 416
 * bytes of a stack that is 1024, and a throw did not fit in it: 1262 bytes
 * against 1024, which is what llvm/utils/C166Sim's ABI stack check reported
 * the day it was written.
 *
 * Three things make it four.  A rule that has an argument - an offset or a
 * register number - is never one that has an expression, so the two share a
 * field.  An expression's length is small enough to ride in the spare bits of
 * the kind, of which there are seven.  And an expression is always inside
 * .eh_frame, so where it is fits in a sixteen bit offset from the start of
 * that section rather than needing the whole address; the linker scripts
 * assert that the section is small enough for that to be true. */
typedef struct {
  u8 kind : 3;    /* one of the RULE_ values above                    */
  u8 exprlen : 5; /* how long the expression is, when there is one    */
  union {
    s16 arg;  /* an offset, or a register number                      */
    u16 expr; /* where the expression is, from __eh_frame_start       */
  } u;
} Rule;

/* The most an exprlen can hold.  A call frame expression this target emits is
 * a handful of bytes - the return address one is the longest and is under ten
 * - so this is not a limit anything reaches; a rule that would exceed it is
 * refused rather than truncated, which stops the walk instead of unwinding to
 * a wrong address. */
#define MAX_EXPR_LEN 31

typedef struct {
  Rule reg[NREGS];
  u8 cfaReg;      /* the register the canonical frame address is from */
  s16 cfaOffset;
  u32 loc;        /* the address the rules so far describe            */
} Row;

/* Where a rule's expression actually is.  The offset is from the start of
 * .eh_frame, which is a far address, and the linker script keeps that section
 * inside one segment - so this addition cannot carry into the segment. */
static u32 ruleExpr(const Rule *r) {
  return (u32)__eh_frame_start + r->u.expr;
}

typedef struct {
  u32 codeAlign;
  s32 dataAlign;
  u32 raColumn;
  u8 fdeEncoding;
  u8 lsdaEncoding;
  u32 personality;
  int hasLSDA;
} CIE;

/*--- The expression evaluator ---------------------------------------------*/

#define DW_OP_deref_size 0x94
#define DW_OP_const1u    0x08
#define DW_OP_shl        0x24
#define DW_OP_or         0x21
#define DW_OP_bregx      0x92

/* Reads a word out of the machine's own memory.  The stacks and the frames
 * are in RAM, which is inside the first 64 KByte, so this is a near read. */
static u16 loadWord(u32 addr) { return *(const volatile u16 *)(u16)addr; }

/* Runs one of the expressions the compiler emits.  Returns 0 and sets *ok to
 * zero if it meets an operation this does not implement, which is the honest
 * answer: the alternative is to carry on with a wrong stack. */
static u32 runExpr(const _Unwind_Context *ctx, u32 exprAddr, u8 len, u32 cfa,
                   int *ok) {
  fptr p = (fptr)exprAddr;
  fptr end = p + len;
  u32 stack[8];
  int sp = 0;

  *ok = 1;
  /* An expression for a register rule starts with the canonical frame address
   * on the stack, which is what the standard says. */
  stack[sp++] = cfa;

  while (p < end) {
    u8 op = f8(&p);
    switch (op) {
    case DW_OP_bregx: {
      u32 reg = uleb(&p);
      s32 off = sleb(&p);
      int slot = slotOf(reg);
      if (slot < 0 || sp >= 8) { *ok = 0; return 0; }
      u32 base;
      if (slot < 16)
        base = ctx->reg[slot];
      else if (slot == REG_SYSSP)
        base = ctx->syssp;
      else if (slot == REG_CSP)
        base = ctx->csp;
      else if (slot == REG_PC)
        base = ctx->pc;
      else
        base = 0;
      stack[sp++] = base + (u32)off;
      break;
    }
    case DW_OP_deref_size: {
      u8 size = f8(&p);
      if (sp < 1 || size != 2) { *ok = 0; return 0; }
      stack[sp - 1] = loadWord(stack[sp - 1]);
      break;
    }
    case DW_OP_const1u:
      if (sp >= 8) { *ok = 0; return 0; }
      stack[sp++] = f8(&p);
      break;
    case DW_OP_shl:
      if (sp < 2) { *ok = 0; return 0; }
      sp--;
      stack[sp - 1] <<= stack[sp];
      break;
    case DW_OP_or:
      if (sp < 2) { *ok = 0; return 0; }
      sp--;
      stack[sp - 1] |= stack[sp];
      break;
    default:
      *ok = 0;
      return 0;
    }
  }
  if (sp < 1) { *ok = 0; return 0; }
  return stack[sp - 1];
}

/*--- Running the call frame instructions ----------------------------------*/

#define DW_CFA_advance_loc      0x40
#define DW_CFA_offset           0x80
#define DW_CFA_restore          0xC0
#define DW_CFA_nop              0x00
#define DW_CFA_set_loc          0x01
#define DW_CFA_advance_loc1     0x02
#define DW_CFA_advance_loc2     0x03
#define DW_CFA_advance_loc4     0x04
#define DW_CFA_offset_extended  0x05
#define DW_CFA_restore_extended 0x06
#define DW_CFA_undefined        0x07
#define DW_CFA_same_value       0x08
#define DW_CFA_register         0x09
#define DW_CFA_remember_state   0x0A
#define DW_CFA_restore_state    0x0B
#define DW_CFA_def_cfa          0x0C
#define DW_CFA_def_cfa_register 0x0D
#define DW_CFA_def_cfa_offset   0x0E
#define DW_CFA_def_cfa_expression 0x0F
#define DW_CFA_expression       0x10
#define DW_CFA_offset_extended_sf 0x11
#define DW_CFA_def_cfa_sf       0x12
#define DW_CFA_def_cfa_offset_sf 0x13
#define DW_CFA_val_offset       0x14
#define DW_CFA_val_offset_sf    0x15
#define DW_CFA_val_expression   0x16

/* Applies the instructions between p and end to row, stopping once the rules
 * describe pc.  Returns zero if it meets something it cannot do. */
static int runCFI(Row *row, const CIE *cie, fptr p, fptr end, u32 pc,
                  const Row *initial) {
  Row saved;
  int haveSaved = 0;

  while (p < end) {
    if (row->loc > pc)
      break;
    u8 op = f8(&p);

    if (op & 0xC0) {
      u8 operand = op & 0x3F;
      switch (op & 0xC0) {
      case DW_CFA_advance_loc:
        row->loc += (u32)operand * cie->codeAlign;
        continue;
      case DW_CFA_offset: {
        s32 off = (s32)uleb(&p) * cie->dataAlign;
        int slot = slotOf(operand);
        if (slot >= 0) {
          row->reg[slot].kind = RULE_OFFSET;
          row->reg[slot].u.arg = (s16)off;
        }
        continue;
      }
      case DW_CFA_restore: {
        int slot = slotOf(operand);
        if (slot >= 0 && initial)
          row->reg[slot] = initial->reg[slot];
        continue;
      }
      }
    }

    switch (op) {
    case DW_CFA_nop:
      break;
    case DW_CFA_set_loc:
      row->loc = encoded(&p, cie->fdeEncoding);
      break;
    case DW_CFA_advance_loc1:
      row->loc += (u32)f8(&p) * cie->codeAlign;
      break;
    case DW_CFA_advance_loc2:
      row->loc += (u32)f16(&p) * cie->codeAlign;
      break;
    case DW_CFA_advance_loc4:
      row->loc += f32(&p) * cie->codeAlign;
      break;
    case DW_CFA_def_cfa:
      row->cfaReg = (u8)uleb(&p);
      row->cfaOffset = (s16)uleb(&p);
      break;
    case DW_CFA_def_cfa_sf: {
      row->cfaReg = (u8)uleb(&p);
      row->cfaOffset = (s16)(sleb(&p) * cie->dataAlign);
      break;
    }
    case DW_CFA_def_cfa_register:
      row->cfaReg = (u8)uleb(&p);
      break;
    case DW_CFA_def_cfa_offset:
      row->cfaOffset = (s16)uleb(&p);
      break;
    case DW_CFA_def_cfa_offset_sf:
      row->cfaOffset = (s16)(sleb(&p) * cie->dataAlign);
      break;
    case DW_CFA_undefined:
    case DW_CFA_same_value: {
      int slot = slotOf(uleb(&p));
      if (slot >= 0)
        row->reg[slot].kind =
            (op == DW_CFA_undefined) ? RULE_UNDEFINED : RULE_SAME;
      break;
    }
    case DW_CFA_offset_extended:
    case DW_CFA_val_offset: {
      int slot = slotOf(uleb(&p));
      s32 off = (s32)uleb(&p) * cie->dataAlign;
      if (slot >= 0) {
        row->reg[slot].kind =
            (op == DW_CFA_val_offset) ? RULE_VAL_OFFSET : RULE_OFFSET;
        row->reg[slot].u.arg = (s16)off;
      }
      break;
    }
    case DW_CFA_offset_extended_sf:
    case DW_CFA_val_offset_sf: {
      int slot = slotOf(uleb(&p));
      s32 off = sleb(&p) * cie->dataAlign;
      if (slot >= 0) {
        row->reg[slot].kind =
            (op == DW_CFA_val_offset_sf) ? RULE_VAL_OFFSET : RULE_OFFSET;
        row->reg[slot].u.arg = (s16)off;
      }
      break;
    }
    case DW_CFA_register: {
      int slot = slotOf(uleb(&p));
      u32 other = uleb(&p);
      if (slot >= 0) {
        row->reg[slot].kind = RULE_REGISTER;
        row->reg[slot].u.arg = (s16)other;
      }
      break;
    }
    case DW_CFA_restore_extended: {
      int slot = slotOf(uleb(&p));
      if (slot >= 0 && initial)
        row->reg[slot] = initial->reg[slot];
      break;
    }
    case DW_CFA_expression:
    case DW_CFA_val_expression: {
      int slot = slotOf(uleb(&p));
      u32 len = uleb(&p);
      if (slot >= 0) {
        /* Refused rather than truncated: a rule this cannot hold is one the
         * walk would apply wrongly, and stopping is the safe answer. */
        u32 off = (u32)p - (u32)__eh_frame_start;
        if (len > MAX_EXPR_LEN || off > 0xFFFFu)
          return 0;
        row->reg[slot].kind =
            (op == DW_CFA_val_expression) ? RULE_VAL_EXPR : RULE_EXPR;
        row->reg[slot].u.expr = (u16)off;
        row->reg[slot].exprlen = (u8)len;
      }
      p += len;
      break;
    }
    case DW_CFA_remember_state:
      saved = *row;
      haveSaved = 1;
      break;
    case DW_CFA_restore_state:
      if (!haveSaved)
        return 0;
      { u32 loc = row->loc; *row = saved; row->loc = loc; }
      break;
    case DW_CFA_def_cfa_expression:
      /* Nothing here emits one: the canonical frame address on this target is
       * always R0 with an offset.  Refusing is better than pretending. */
      return 0;
    default:
      return 0;
    }
  }
  return 1;
}

/*--- Finding the entry for an address -------------------------------------*/

static int readCIE(fptr p, fptr end, CIE *cie, fptr *instrs) {
  cie->codeAlign = 1;
  cie->dataAlign = -2;
  cie->raColumn = DW_PC;
  cie->fdeEncoding = DW_EH_PE_absptr;
  cie->lsdaEncoding = DW_EH_PE_omit;
  cie->personality = 0;
  cie->hasLSDA = 0;

  u8 version = f8(&p);
  if (version != 1 && version != 3)
    return 0;

  /* The augmentation string says what the augmentation data holds. */
  char aug[8];
  unsigned n = 0;
  for (;;) {
    u8 c = f8(&p);
    if (!c)
      break;
    if (n < sizeof(aug) - 1)
      aug[n++] = (char)c;
  }
  aug[n] = 0;

  cie->codeAlign = uleb(&p);
  cie->dataAlign = sleb(&p);
  cie->raColumn = (version == 1) ? f8(&p) : uleb(&p);

  if (aug[0] == 'z') {
    u32 len = uleb(&p);
    fptr augEnd = p + len;
    for (unsigned i = 1; i < n; i++) {
      switch (aug[i]) {
      case 'R': cie->fdeEncoding = f8(&p); break;
      case 'L': cie->lsdaEncoding = f8(&p); cie->hasLSDA = 1; break;
      case 'P': {
        u8 enc = f8(&p);
        cie->personality = encoded(&p, enc);
        break;
      }
      case 'S': break;  /* a signal frame; nothing here makes one */
      default: return 0;
      }
    }
    p = augEnd;
  }
  if (instrs)
    *instrs = p;
  (void)end;
  return 1;
}

/* Finds the frame description entry covering pc, and the common information
 * entry it points at.  Returns zero when there is none, which is how a walk
 * ends: the outermost frame is the one nothing describes. */
/* The last common information entry parsed, kept because a search asks for the
 * same one over and over. */
static fptr cachedCIE;
static CIE cachedInfo;
static fptr cachedInstrs;
static fptr cachedEnd;

/* And the last entry found, because every frame is looked up twice: once to
 * tell a personality routine about it and once to step out of it. */
static u32 lastPC;
static int lastOK;
static CIE lastCIE;
static fptr lastInstrs, lastInstrsEnd, lastCieInstrs, lastCieEnd;
static u32 lastStart, lastLSDA;

static int findFDEUncached(u32 pc, CIE *cie, fptr *instrs, fptr *instrsEnd,
                           u32 *start, u32 *lsda, fptr *cieInstrs,
                           fptr *cieEnd);

static int findFDE(u32 pc, CIE *cie, fptr *instrs, fptr *instrsEnd, u32 *start,
                   u32 *lsda, fptr *cieInstrs, fptr *cieEnd) {
  if (pc != lastPC || !lastPC) {
    lastOK = findFDEUncached(pc, &lastCIE, &lastInstrs, &lastInstrsEnd,
                             &lastStart, &lastLSDA, &lastCieInstrs,
                             &lastCieEnd);
    lastPC = pc;
  }
  if (!lastOK)
    return 0;
  *cie = lastCIE;
  *instrs = lastInstrs;
  *instrsEnd = lastInstrsEnd;
  *start = lastStart;
  *lsda = lastLSDA;
  *cieInstrs = lastCieInstrs;
  *cieEnd = lastCieEnd;
  return 1;
}

static int findFDEUncached(u32 pc, CIE *cie, fptr *instrs, fptr *instrsEnd,
                           u32 *start, u32 *lsda, fptr *cieInstrs,
                           fptr *cieEnd) {
  fptr p = (fptr)__eh_frame_start;
  fptr limit = (fptr)__eh_frame_end;

  while (p < limit) {
    /* the entry begins here; length counts from after the field */
    u32 length = f32(&p);
    if (length == 0)
      break;                       /* the terminator */
    if (length == 0xFFFFFFFFul)
      return 0;                    /* 64 bit DWARF; nothing here emits it */
    fptr next = p + length;
    u32 cieField = f32(&p);
    if (cieField == 0) {           /* a CIE: not what we are looking for */
      p = next;
      continue;
    }

    /* The field counts back from itself to the CIE.
     *
     * Parsing it again for every entry examined is most of what this search
     * costs, because a program has one or two common information entries and
     * as many frame description entries as it has functions.  One remembered
     * answer covers a whole scan. */
    fptr ciePtr = (p - 4) - cieField;
    if (cachedCIE != ciePtr) {
      fptr cp = ciePtr;
      u32 cieLen = f32(&cp);
      if (f32(&cp) != 0)
        return 0;
      if (!readCIE(cp, ciePtr + 4 + cieLen, &cachedInfo, &cachedInstrs))
        return 0;
      cachedEnd = ciePtr + 4 + cieLen;
      cachedCIE = ciePtr;
    }
    *cie = cachedInfo;
    *cieInstrs = cachedInstrs;
    *cieEnd = cachedEnd;

    u32 begin = encoded(&p, cie->fdeEncoding);
    u32 range = encoded(&p, cie->fdeEncoding & 0x0F);

    u32 fdeLSDA = 0;
    if (cie->hasLSDA) {
      u32 len = uleb(&p);
      fptr augEnd = p + len;
      fdeLSDA = encoded(&p, cie->lsdaEncoding);
      p = augEnd;
    }

    if (pc >= begin && pc < begin + range) {
      *instrs = p;
      *instrsEnd = next;
      *start = begin;
      *lsda = fdeLSDA;
      return 1;
    }
    p = next;
  }
  return 0;
}

/*--- Stepping back one frame ----------------------------------------------*/

static void initialRow(Row *row) {
  for (int i = 0; i < NREGS; i++) {
    row->reg[i].kind = RULE_SAME;
    row->reg[i].exprlen = 0;
    row->reg[i].u.arg = 0;
  }
  row->cfaReg = 0;
  row->cfaOffset = 0;
  row->loc = 0;
}

/* The rules for the frame ctx is in, and the canonical frame address they
 * produce.  Both stepping out of a frame and asking what a personality routine
 * should be told about it need this, so it is one function.
 *
 * The address looked up is a byte before the one a return would go to, because
 * that is where the call is; a call at the very end of a function would
 * otherwise be attributed to whatever follows it. */
static int frameInfo(const _Unwind_Context *ctx, Row *row, u32 *cfa,
                     u32 *start, u32 *lsda, CIE *cieOut) {
  CIE cie;
  fptr instrs, instrsEnd, cieInstrs, cieEnd;

  u32 lookup = ctx->pc ? ctx->pc - 1 : 0;
  if (!findFDE(lookup, &cie, &instrs, &instrsEnd, start, lsda, &cieInstrs,
               &cieEnd))
    return 0;

  initialRow(row);
  row->loc = *start;

  /* The common information entry's instructions first.  They are the rules
   * every frame starts from, and on this target they are where the two stack
   * arrangement is described - the return address expression and the one that
   * pops the system stack are both in there, so almost every function repeats
   * nothing. */
  if (!runCFI(row, &cie, cieInstrs, cieEnd, 0xFFFFFFFFul, 0))
    return 0;

  Row initial = *row;
  if (!runCFI(row, &cie, instrs, instrsEnd, lookup, &initial))
    return 0;

  /* The canonical frame address, which here is always a register plus an
   * offset - the compiler emits no expression for it. */
  int slot = slotOf(row->cfaReg);
  if (slot < 0)
    return 0;
  u32 base = (slot < 16) ? ctx->reg[slot]
                         : (slot == REG_SYSSP ? ctx->syssp : ctx->csp);
  *cfa = base + (u32)(s32)row->cfaOffset;
  if (cieOut)
    *cieOut = cie;
  return 1;
}

/* Fills in what a personality routine is told about the frame ctx is in,
 * without stepping out of it. */
int __unw_describe(_Unwind_Context *ctx) {
  Row row;
  CIE cie;
  u32 cfa, start, lsda;
  if (!frameInfo(ctx, &row, &cfa, &start, &lsda, &cie))
    return 0;
  ctx->cfa = cfa;
  ctx->start = start;
  ctx->lsda = lsda;
  ctx->fde = cie.personality;
  return 1;
}

u32 __unw_personality(_Unwind_Context *ctx) { return ctx->fde; }

_Unwind_Reason_Code __unw_step(_Unwind_Context *ctx) {
  u32 start, lsda, cfa;
  Row row;
  if (!frameInfo(ctx, &row, &cfa, &start, &lsda, 0))
    return _URC_END_OF_STACK;



  /* Apply the rules to produce the caller's state.  The order matters: every
   * expression is written in terms of this frame's registers, so they are all
   * evaluated before any of them is stored. */
  _Unwind_Context out = *ctx;
  u32 value[NREGS];
  /* One bit per register rather than one byte: twenty bytes of a stack this
   * size is worth the shift, and there are only twenty of them. */
  u32 have = 0;
#define HAVE(i) (have & (1ul << (i)))
#define SET_HAVE(i) (have |= 1ul << (i))

  for (int i = 0; i < NREGS; i++) {
    value[i] = 0;
    int ok = 1;
    switch (row.reg[i].kind) {
    case RULE_SAME:
      break;
    case RULE_UNDEFINED:
      break;
    case RULE_OFFSET:
      value[i] = loadWord(cfa + (u32)(s32)row.reg[i].u.arg);
      SET_HAVE(i);
      break;
    case RULE_VAL_OFFSET:
      value[i] = cfa + (u32)(s32)row.reg[i].u.arg;
      SET_HAVE(i);
      break;
    case RULE_REGISTER: {
      int from = slotOf((u32)row.reg[i].u.arg);
      if (from < 0)
        return _URC_FATAL_PHASE1_ERROR;
      value[i] = (from < 16) ? ctx->reg[from]
                             : (from == REG_SYSSP ? ctx->syssp
                                : from == REG_CSP  ? ctx->csp
                                                   : ctx->pc);
      SET_HAVE(i);
      break;
    }
    case RULE_EXPR: {
      u32 addr = runExpr(ctx, ruleExpr(&row.reg[i]), row.reg[i].exprlen,
                         cfa, &ok);
      if (!ok)
        return _URC_FATAL_PHASE1_ERROR;
      value[i] = loadWord(addr);
      SET_HAVE(i);
      break;
    }
    case RULE_VAL_EXPR:
      value[i] = runExpr(ctx, ruleExpr(&row.reg[i]), row.reg[i].exprlen,
                         cfa, &ok);
      if (!ok)
        return _URC_FATAL_PHASE1_ERROR;
      SET_HAVE(i);
      break;
    }
  }

  for (int i = 0; i < 16; i++)
    if (HAVE(i))
      out.reg[i] = (u16)value[i];
  if (HAVE(REG_SYSSP))
    out.syssp = (u16)value[REG_SYSSP];
  if (HAVE(REG_CSP))
    out.csp = (u16)value[REG_CSP];
  if (HAVE(REG_PC))
    out.pc = value[REG_PC];

  /* The canonical frame address is the caller's ABI stack pointer, which is
   * what says where its frame is. */
  out.reg[0] = (u16)cfa;
  out.cfa = cfa;
  out.start = start;
  out.lsda = lsda;

  /* A frame whose return address came back as zero is the outermost one:
   * crt0 starts the program with a zero there so that a walk ends rather than
   * wandering off into whatever the reset left behind. */
  if (!HAVE(REG_PC) || out.pc == 0 || out.pc == ctx->pc)
    return _URC_END_OF_STACK;
#undef HAVE
#undef SET_HAVE

  *ctx = out;
  return _URC_NO_REASON;
}

/*--- What a program calls -------------------------------------------------*/

u32 _Unwind_GetIP(_Unwind_Context *ctx) { return ctx->pc; }
void _Unwind_SetIP(_Unwind_Context *ctx, u32 pc) { ctx->pc = pc; }
u32 _Unwind_GetRegionStart(_Unwind_Context *ctx) { return ctx->start; }
u32 _Unwind_GetLanguageSpecificData(_Unwind_Context *ctx) { return ctx->lsda; }

u32 _Unwind_GetGR(_Unwind_Context *ctx, int reg) {
  int slot = slotOf((u32)reg);
  if (slot < 0)
    return 0;
  if (slot < 16)
    return ctx->reg[slot];
  if (slot == REG_SYSSP)
    return ctx->syssp;
  if (slot == REG_CSP)
    return ctx->csp;
  return ctx->pc;
}

void _Unwind_SetGR(_Unwind_Context *ctx, int reg, u32 value) {
  int slot = slotOf((u32)reg);
  if (slot < 0)
    return;
  if (slot < 16)
    ctx->reg[slot] = (u16)value;
  else if (slot == REG_SYSSP)
    ctx->syssp = (u16)value;
  else if (slot == REG_CSP)
    ctx->csp = (u16)value;
  else
    ctx->pc = value;
}

_Unwind_Reason_Code _Unwind_Backtrace(_Unwind_Reason_Code (*fn)(_Unwind_Context *,
                                                                void *),
                                      void *arg) {
  _Unwind_Context ctx;
  __unw_getcontext(&ctx);
  ctx.cfa = 0;
  ctx.fde = 0;
  ctx.lsda = 0;
  ctx.start = 0;

  for (;;) {
    _Unwind_Reason_Code r = fn(&ctx, arg);
    if (r != _URC_NO_REASON)
      return r;
    r = __unw_step(&ctx);
    if (r == _URC_END_OF_STACK)
      return _URC_END_OF_STACK;
    if (r != _URC_NO_REASON)
      return r;
  }
}


/*--- Raising an exception -------------------------------------------------*/

u32 _Unwind_GetCFA(_Unwind_Context *ctx) { return ctx->cfa; }

/* The two passes the Itanium ABI describes.
 *
 * The first asks every frame's personality routine whether it will handle the
 * exception, and changes nothing.  Only when one says yes does the second pass
 * run, unwinding for real: cleanups happen on the way, and at the frame that
 * said yes the personality installs a landing pad and the machine is put into
 * that frame and continues there.
 *
 * Doing it in two passes is what makes a throw that nothing catches leave the
 * stack alone, so the program can be stopped where the throw was rather than
 * somewhere half unwound. */
static _Unwind_Reason_Code raise(_Unwind_Exception *exc, _Unwind_Context *start) {
  _Unwind_Context ctx = *start;

  /* Pass one: look for a handler. */
  for (;;) {
    if (!__unw_describe(&ctx))
      return _URC_END_OF_STACK;
    u32 p = __unw_personality(&ctx);
    if (p) {
      _Unwind_Personality_Fn fn = (_Unwind_Personality_Fn)(u16)p;
      _Unwind_Reason_Code r =
          fn(1, _UA_SEARCH_PHASE, exc->exception_class, exc, &ctx);
      if (r == _URC_HANDLER_FOUND) {
        exc->private_1 = ctx.cfa;
        break;
      }
      if (r != _URC_CONTINUE_UNWIND)
        return _URC_FATAL_PHASE1_ERROR;
    }
    if (__unw_step(&ctx) != _URC_NO_REASON)
      return _URC_END_OF_STACK;
  }

  /* Pass two: unwind for real, stopping at the frame that said yes. */
  ctx = *start;
  for (;;) {
    if (!__unw_describe(&ctx))
      return _URC_FATAL_PHASE2_ERROR;
    u32 p = __unw_personality(&ctx);
    if (p) {
      int actions = _UA_CLEANUP_PHASE;
      if (ctx.cfa == exc->private_1)
        actions |= _UA_HANDLER_FRAME;
      _Unwind_Personality_Fn fn = (_Unwind_Personality_Fn)(u16)p;
      _Unwind_Reason_Code r =
          fn(1, actions, exc->exception_class, exc, &ctx);
      if (r == _URC_INSTALL_CONTEXT) {
        __unw_resume(&ctx);   /* does not return */
        return _URC_FATAL_PHASE2_ERROR;
      }
      if (r != _URC_CONTINUE_UNWIND)
        return _URC_FATAL_PHASE2_ERROR;
    }
    if (__unw_step(&ctx) != _URC_NO_REASON)
      return _URC_FATAL_PHASE2_ERROR;
  }
}

_Unwind_Reason_Code _Unwind_RaiseException(_Unwind_Exception *exc) {
  _Unwind_Context ctx;
  __unw_getcontext(&ctx);
  ctx.cfa = 0;
  ctx.fde = 0;
  ctx.lsda = 0;
  ctx.start = 0;
  return raise(exc, &ctx);
}

/* Carrying on after a cleanup has run.  The frame that called this is the one
 * the cleanup was in, so the walk starts again from there and finds the same
 * handler; the search pass is not repeated, because private_1 still holds the
 * frame it chose. */
void _Unwind_Resume(_Unwind_Exception *exc) {
  _Unwind_Context ctx;
  __unw_getcontext(&ctx);
  ctx.cfa = 0;
  ctx.fde = 0;
  ctx.lsda = 0;
  ctx.start = 0;

  for (;;) {
    if (!__unw_describe(&ctx))
      break;
    u32 p = __unw_personality(&ctx);
    if (p) {
      int actions = _UA_CLEANUP_PHASE;
      if (ctx.cfa == exc->private_1)
        actions |= _UA_HANDLER_FRAME;
      _Unwind_Personality_Fn fn = (_Unwind_Personality_Fn)(u16)p;
      _Unwind_Reason_Code r = fn(1, actions, exc->exception_class, exc, &ctx);
      if (r == _URC_INSTALL_CONTEXT)
        __unw_resume(&ctx);
      if (r != _URC_CONTINUE_UNWIND)
        break;
    }
    if (__unw_step(&ctx) != _URC_NO_REASON)
      break;
  }
  __cxa_call_terminate();
}

void _Unwind_DeleteException(_Unwind_Exception *exc) {
  if (exc->exception_cleanup)
    exc->exception_cleanup(_URC_FOREIGN_EXCEPTION_CAUGHT, exc);
}
