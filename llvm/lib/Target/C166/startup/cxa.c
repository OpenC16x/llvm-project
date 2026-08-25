/*===-- cxa.c - The C++ ABI a thrown exception needs, for the C166 ---*- C -*-=*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM
|* Exceptions.  See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/
/*
 * Enough of the Itanium C++ ABI for "throw" and "catch" to work: the calls the
 * compiler generates around a try block, and the personality routine that
 * reads the tables it emitted to decide what a frame does.
 *
 * Two things are smaller here than in a hosted implementation, and both are
 * choices rather than omissions.
 *
 * There is no heap.  A thrown object goes into one static buffer, so a program
 * can have one exception in flight at a time.  That covers throw and catch and
 * rethrow; what it does not cover is a second throw from inside a catch, and
 * that is refused rather than left to corrupt the first.
 *
 * Type matching is on the address of the type information object.  That is
 * exact-type matching: catching a base class by reference does not catch a
 * derived one, and catch(...) catches everything.  Doing better means walking
 * the __class_type_info hierarchy, which needs the vtables that the full
 * library brings with it.  A catch that would need it does not silently take
 * the wrong branch - it simply does not match, and the exception carries on to
 * whatever does.
 */

#include "unwind.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef signed long s32;
typedef signed short s16;

typedef const __far u8 *fptr;

/* The tables are read from wherever the exception table was linked, which is
 * the Flash, so this is the same far read the unwinder does. */
static u8 f8(fptr *p) { u8 v = **p; *p += 1; return v; }
static u16 f16(fptr *p) { u16 v = (u16)(*p)[0] | ((u16)(*p)[1] << 8); *p += 2; return v; }
static u32 f32(fptr *p) {
  u32 v = (u32)(*p)[0] | ((u32)(*p)[1] << 8) | ((u32)(*p)[2] << 16) |
          ((u32)(*p)[3] << 24);
  *p += 4;
  return v;
}
static u32 uleb(fptr *p) {
  u32 v = 0; unsigned shift = 0; u8 b;
  do { b = f8(p); v |= (u32)(b & 0x7F) << shift; shift += 7; } while (b & 0x80);
  return v;
}
static s32 sleb(fptr *p) {
  s32 v = 0; unsigned shift = 0; u8 b;
  do { b = f8(p); v |= (s32)(u32)(b & 0x7F) << shift; shift += 7; } while (b & 0x80);
  if (shift < 32 && (b & 0x40)) v |= -((s32)1 << shift);
  return v;
}

#define DW_EH_PE_omit    0xFF
#define DW_EH_PE_absptr  0x00
#define DW_EH_PE_uleb128 0x01
#define DW_EH_PE_udata2  0x02
#define DW_EH_PE_udata4  0x03
#define DW_EH_PE_sdata2  0x0A
#define DW_EH_PE_sdata4  0x0B
#define DW_EH_PE_pcrel   0x10

static u32 encoded(fptr *p, u8 enc) {
  fptr here = *p;
  u32 v = 0;
  if (enc == DW_EH_PE_omit) return 0;
  switch (enc & 0x0F) {
  case DW_EH_PE_absptr: v = f32(p); break;
  case DW_EH_PE_uleb128: v = uleb(p); break;
  case DW_EH_PE_udata2: v = f16(p); break;
  case DW_EH_PE_udata4: v = f32(p); break;
  case DW_EH_PE_sdata2: v = (u32)(s32)(s16)f16(p); break;
  case DW_EH_PE_sdata4: v = f32(p); break;
  default: return 0;
  }
  if ((enc & 0x70) == DW_EH_PE_pcrel) v += (u32)here;
  return v;
}

/* How big an entry of this encoding is, which is what stepping backwards
 * through the type table needs. */
static unsigned encodedSize(u8 enc) {
  switch (enc & 0x0F) {
  case DW_EH_PE_udata2: case DW_EH_PE_sdata2: return 2;
  case DW_EH_PE_absptr: case DW_EH_PE_udata4: case DW_EH_PE_sdata4: return 4;
  default: return 0;
  }
}

/*--- The exceptions in flight ----------------------------------------------*/

#define EXC_CLASS 0x43313636ul  /* "C166" */

/* Thrown objects live in a small fixed pool rather than on a heap, because
 * there is no heap.  More than one can be live at a time: throwing from inside
 * a catch is ordinary C++, and the exception being caught is not finished with
 * until its handler ends, so the two have to coexist.  Four deep is what this
 * carries; a program that needs more is told rather than quietly corrupting
 * the one underneath. */
#define MAX_NEST 4
#define OBJ_MAX 32

typedef struct {
  _Unwind_Exception header; /* first, so a header pointer is a slot pointer */
  u32 type;                 /* the type information the throw named          */
  void (*dtor)(void *);
  u8 used;
  u32 object[OBJ_MAX / 4];
} Exc;

static Exc pool[MAX_NEST];
static Exc *caughtStack[MAX_NEST];
static int caughtTop;

void __cxa_call_terminate(void);

static void cleanup(_Unwind_Reason_Code r, _Unwind_Exception *exc) {
  (void)r;
  ((Exc *)exc)->used = 0;
}

void *__cxa_allocate_exception(u16 size) {
  if (size > OBJ_MAX)
    __cxa_call_terminate();
  for (int i = 0; i < MAX_NEST; i++) {
    if (pool[i].used)
      continue;
    pool[i].used = 1;
    pool[i].type = 0;
    pool[i].dtor = 0;
    pool[i].header.exception_class = EXC_CLASS;
    pool[i].header.exception_cleanup = cleanup;
    pool[i].header.private_1 = 0;
    pool[i].header.private_2 = 0;
    return (void *)pool[i].object;
  }
  __cxa_call_terminate();
  return 0;
}

/* The slot an object pointer belongs to. */
static Exc *slotOfObject(void *obj) {
  for (int i = 0; i < MAX_NEST; i++)
    if ((void *)pool[i].object == obj)
      return &pool[i];
  return 0;
}

void __cxa_free_exception(void *obj) {
  Exc *e = slotOfObject(obj);
  if (e)
    e->used = 0;
}

/* What the compiler calls for "throw x".  The type information pointer is
 * kept in the slot so that the personality routine can match a catch against
 * the right one when several are live. */
void __cxa_throw(void *obj, void *tinfo, void (*dtor)(void *)) {
  Exc *e = slotOfObject(obj);
  if (!e)
    __cxa_call_terminate();
  e->type = (u32)(u16)tinfo;
  e->dtor = dtor;
  e->header.private_1 = 0;
  _Unwind_RaiseException(&e->header);
  /* Nothing caught it. */
  __cxa_call_terminate();
}

void *__cxa_begin_catch(void *p) {
  Exc *e = (Exc *)p;
  if (caughtTop < MAX_NEST)
    caughtStack[caughtTop++] = e;
  else
    __cxa_call_terminate();
  return (void *)e->object;
}

void __cxa_end_catch(void) {
  if (caughtTop <= 0)
    return;
  Exc *e = caughtStack[--caughtTop];
  if (e->dtor)
    e->dtor((void *)e->object);
  e->dtor = 0;
  e->used = 0;
}

/* "throw;" inside a catch, which sends the one being handled on its way
 * again rather than making a new one. */
void __cxa_rethrow(void) {
  if (caughtTop <= 0)
    __cxa_call_terminate();
  Exc *e = caughtStack[caughtTop - 1];
  e->header.private_1 = 0;
  _Unwind_RaiseException(&e->header);
  __cxa_call_terminate();
}

/*--- The personality routine ----------------------------------------------*/

/* Walks the tables the compiler emitted for one frame.
 *
 * The call site table says, for the range of the function the frame is
 * stopped in, whether there is a landing pad and which action to start from.
 * An action is a list of type table indices: a positive one is a catch, and
 * matching it means the handler is here; a zero one is a cleanup, which runs
 * on the way past but does not stop the unwinding.
 */
_Unwind_Reason_Code __gxx_personality_v0(int version, int actions,
                                         u32 exceptionClass,
                                         _Unwind_Exception *exc,
                                         _Unwind_Context *ctx) {
  if (version != 1)
    return _URC_FATAL_PHASE1_ERROR;

  u32 lsdaAddr = _Unwind_GetLanguageSpecificData(ctx);
  if (!lsdaAddr)
    return _URC_CONTINUE_UNWIND;

  /* Something thrown by another runtime is not ours to match types against;
   * cleanups still run for it. */
  int ours = (exceptionClass == EXC_CLASS);

  fptr p = (fptr)lsdaAddr;
  u32 funcStart = _Unwind_GetRegionStart(ctx);
  /* Where in the function we are: the call is a byte before the return. */
  u32 ip = _Unwind_GetIP(ctx);
  u32 offset = (ip ? ip - 1 : 0) - funcStart;

  u8 lpStartEnc = f8(&p);
  u32 lpStart = funcStart;
  if (lpStartEnc != DW_EH_PE_omit)
    lpStart = encoded(&p, lpStartEnc);

  u8 ttypeEnc = f8(&p);
  fptr ttypeBase = 0;
  if (ttypeEnc != DW_EH_PE_omit) {
    u32 ttypeOff = uleb(&p);
    ttypeBase = p + ttypeOff;
  }

  u8 callSiteEnc = f8(&p);
  u32 callSiteLen = uleb(&p);
  fptr callSites = p;
  fptr actionTable = p + callSiteLen;

  /* Find the call site the frame is stopped at. */
  u32 landingPad = 0;
  u32 actionRecord = 0;
  int found = 0;
  fptr cs = callSites;
  while (cs < actionTable) {
    u32 start = encoded(&cs, callSiteEnc);
    u32 len = encoded(&cs, callSiteEnc);
    u32 pad = encoded(&cs, callSiteEnc);
    u32 action = uleb(&cs);
    if (offset >= start && offset < start + len) {
      landingPad = pad;
      actionRecord = action;
      found = 1;
      break;
    }
  }

  /* Not in the table at all means the call could not throw, so nothing here
   * takes part in the unwinding. */
  if (!found || landingPad == 0)
    return _URC_CONTINUE_UNWIND;

  int hasCleanup = 0;
  u32 matchedSelector = 0;

  if (actionRecord == 0) {
    /* A landing pad with no action is a cleanup. */
    hasCleanup = 1;
  } else {
    fptr a = actionTable + (actionRecord - 1);
    for (;;) {
      fptr recordStart = a;
      s32 filter = sleb(&a);
      fptr afterFilter = a;
      s32 next = sleb(&a);

      if (filter == 0) {
        hasCleanup = 1;
      } else if (filter > 0) {
        /* A catch.  The type table is indexed backwards from its base. */
        if (ttypeEnc == DW_EH_PE_omit)
          break;
        unsigned size = encodedSize(ttypeEnc);
        if (!size)
          return _URC_FATAL_PHASE1_ERROR;
        fptr entry = ttypeBase - (u32)filter * size;
        u32 catchType = encoded(&entry, ttypeEnc);
        /* A zero entry is catch(...), which takes anything. */
        if (catchType == 0 || (ours && catchType == ((Exc *)exc)->type)) {
          matchedSelector = (u32)filter;
          break;
        }
      } else {
        /* An exception specification, which nothing here generates. */
        break;
      }

      if (next == 0)
        break;
      a = afterFilter + next;
      (void)recordStart;
    }
  }

  if (actions & _UA_SEARCH_PHASE) {
    if (matchedSelector)
      return _URC_HANDLER_FOUND;
    return _URC_CONTINUE_UNWIND;
  }

  /* The cleanup pass.  A frame with a cleanup and no match runs the cleanup
   * and comes back through _Unwind_Resume; the handler frame enters its catch
   * and stays there. */
  if (!matchedSelector && !hasCleanup)
    return _URC_CONTINUE_UNWIND;
  if ((actions & _UA_HANDLER_FRAME) && !matchedSelector)
    return _URC_CONTINUE_UNWIND;
  if (!(actions & _UA_HANDLER_FRAME) && matchedSelector && !hasCleanup)
    return _URC_CONTINUE_UNWIND;

  /* The landing pad reads the exception pointer and the selector out of the
   * first two argument registers, which is where the compiler's landingpad
   * instruction expects them. */
  _Unwind_SetGR(ctx, 2, (u32)(u16)(void *)exc);
  _Unwind_SetGR(ctx, 3, matchedSelector);
  _Unwind_SetIP(ctx, lpStart + landingPad);
  return _URC_INSTALL_CONTEXT;
}

/*--- The pieces the compiler's own output refers to -----------------------*/

/* Type information objects begin with a pointer into one of these vtables,
 * which is how a full implementation tells one kind of type from another.
 * Matching here is on the address of the object rather than on anything
 * reached through the pointer, so these exist to be pointed at and are never
 * read.  They are weak, so a program that links a real libc++abi gets that
 * one's instead. */
__attribute__((weak)) void *_ZTVN10__cxxabiv117__class_type_infoE[4];
__attribute__((weak)) void *_ZTVN10__cxxabiv120__si_class_type_infoE[4];
__attribute__((weak)) void *_ZTVN10__cxxabiv121__vmi_class_type_infoE[4];
__attribute__((weak)) void *_ZTVN10__cxxabiv123__fundamental_type_infoE[4];
__attribute__((weak)) void *_ZTVN10__cxxabiv119__pointer_type_infoE[4];

/* Where a program ends up when an exception cannot be handled: thrown with
 * one already in flight, thrown past every frame without being caught, or
 * resumed from a cleanup with nowhere left to go.
 *
 * The default stops the machine rather than returning, because there is
 * nowhere to return to.  It is weak so that a program can say what should
 * happen instead - printing something before it stops, most usefully. */
__attribute__((weak)) void __cxa_call_terminate(void) {
  for (;;)
    ;
}

/* Called for "throw;" outside a catch, and for a pure virtual call. */
__attribute__((weak)) void __cxa_pure_virtual(void) { __cxa_call_terminate(); }

/* std::terminate, which the compiler's own __clang_call_terminate calls when
 * an exception escapes somewhere the language does not allow it to - out of a
 * destructor during unwinding, or out of a noexcept function.  It is the same
 * place everything else ends up. */
__attribute__((weak)) void _ZSt9terminatev(void) { __cxa_call_terminate(); }
