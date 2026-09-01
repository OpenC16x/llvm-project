# User Guide for the C166 Target

## Introduction

The C166 target generates code for the Infineon C166 family of 16-bit
microcontrollers and the parts derived from it: the C167, STMicroelectronics'
ST10, and Infineon's XC16x. It lives in the `llvm/lib/Target/C166` directory.

These are deeply embedded parts. There is no operating system, program memory
is measured in tens of kilobytes, and the memory map is a property of the
board rather than of the compiler — so linking one of these programs needs a
linker script, and this page says what that script has to provide.

The target triple is `c166`. `c166-unknown-elf` and `c166-none-elf` are
accepted and mean the same thing; only ELF output is produced.

## Choosing a part

Two options select the target, and they answer different questions.

`-mcpu=` names the **core**: which instructions exist, and which special
function registers can be named.

| `-mcpu=` | What it is |
| -------- | ---------- |
| `generic` | The second-generation core: the `EXTend` instructions and `ATOMIC`, with the special function registers every part shares. This is the default. |
| `c166` | The first generation. No `EXTend` instructions, so no far addressing at all, and no `ATOMIC`. |
| `c167` | Second generation with the C167CS's peripheral registers. |
| `st10` | Second generation with the ST10F269's peripheral registers. |
| `xc16x` | Second generation with the XC164CM's peripheral registers and the multiply-accumulate coprocessor. |

The default is `generic` rather than `c166` deliberately: every part still
sold is second generation, and a default that could not reach a far object
would not be able to build its own runtime. Naming the oldest core is
something you ask for by name.

`-mmcu=` names a **part**, which selects a core and, with it, how much memory
that part has. It exists so that moving between derivatives does not mean
editing a memory map by hand:

```
xc164cm-8f  xc164gm-8f  xc164sm-8f  xc164tm-8f  xc164km-8f  xc164lm-8f
xc164cm-4f  xc164gm-4f  xc164sm-4f  xc164tm-4f  xc164km-4f  xc164lm-4f
xc164cs-16f xc164cs-8f  xc164cs-16r xc164cs-8r
c167sr-lm   c167cr-lm   c167cr-4rm  c167cr-16rm
st10f269    st10f272b   st10f272e
```

Naming a part that is not in that list is an error rather than a part with no
memory, because the point of naming one is not to have to know its map.
Every row comes from a derivative table or data sheet; nothing is inferred
from a part number, since the letters in an XC164 name say which peripherals
it has and the suffix says the memory, and a row guessed from the name would
be wrong in a way that only shows when a program overflows a region that is
not there.

Both may be given, but they have to agree: naming a part and then a core the
part does not have is refused. The two are read in different places — the
startup file follows `-mcpu=` and the memory map follows the part — so a
disagreement builds one part's startup against another part's map, which
links and does not run.

### The coprocessor

The multiply-accumulate unit is a feature rather than a core, because
`-mcpu=st10` covers parts that have it and parts that do not — ST's own
programming manual says to consult the device data sheet. `-mcpu=xc16x`
implies it. Elsewhere it has to be asked for:

```
clang --target=c166 -mcpu=st10 -mmac ...
```

`-mno-mac` is the other way, and turns the unit off on a core that would
otherwise imply it. Neither flag leaves the decision to `-mcpu=` and `-mmcu=`.

The simulator takes the same thing in the same spelling — `c166-sim
--mcpu=st10 --mattr=+mac` — because its decoder is the target's own, and
without being told it would refuse the instructions the compiler had just
emitted for that part.

What the unit does for ordinary C, without an asm statement: `acc += a * b`
with 16-bit operands and a 32-bit accumulator, `(a * b + 0x8000) >> 16` as one
rounding multiply, and a 32-bit signed `min` or `max` — which is four
straight-line instructions where the alternative is a tree of five basic
blocks, measured at 2.4 times faster and a third of the code.

## Types

A 16-bit machine, so `int` is a register and the bus is a word wide:

| Type | Width | Alignment |
| ---- | ----- | --------- |
| `char` | 8 | 8 |
| `short`, `int` | 16 | 16 |
| `long`, `float` | 32 | 16 |
| `long long`, `double`, `long double` | 64 | 16 |
| pointer (near) | 16 | 16 |
| pointer (far) | 32 | 16 |

Nothing needs more than word alignment. `size_t` is `unsigned int` and
`ptrdiff_t` is `int`, both 16 bits. `char32_t` is `unsigned long`, because
`int` is too narrow to hold a code point.

Objects up to a word are lock free: one byte or one aligned word is a single
bus cycle, and `ATOMIC` covers the read, the change and the write back.
Anything wider is a library call.

`thread_local` compiles, and means `static`. These parts run one thread.

## Near and far

A near address is 16 bits and is resolved through the data page pointers, of
which three are available to point at code and constants — so **48 KByte of
program memory is reachable by a near address**, whatever the part has. Data
declared `__far` and functions declared `__attribute__((far))` reach the full
24-bit space instead.

```c
__far const char message[] = "in another segment";

__attribute__((far)) void handler(void);
```

`__far` is a predefined macro for `__attribute__((address_space(1)))`. A far
data access costs an `EXTend` prefix; a far call uses `CALLS` and `RETS` and
one more stack word, which is what makes it reachable from another segment.
The attribute is honoured on declarations as well as definitions — it is what
tells a caller in another translation unit which call sequence to use.

`-mcpu=c166` has no `EXTend` instructions, so far addressing is unavailable
on the first-generation core.

## Bit variables

Setting, clearing or testing one bit of a variable is a single two-byte
instruction if the variable is in the bit-addressable RAM, which `__bitaddr`
places it in:

```c
__bitaddr volatile unsigned short flags;

void arm(void)    { flags |= 8; }             /* bset flags.3     */
void disarm(void) { flags &= ~8; }            /* bclr flags.3     */
void poll(void)   { if (flags & 8) act(); }   /* jnb  flags.3, .. */
```

Without it those are a load, a mask and a store — eight bytes against two, and
with a gap between the read and the write in which an interrupt touching the
same word loses what it set. The bit instruction does the whole thing as one
bus operation, which is the reason to want it beyond the size.

Some limits, all of them the machine's:

- **The bit has to be a constant.** `flags |= 8` names one; `flags |= 1u << n`
  does not, and stays a shift and an `OR`.
- **The space is 128 words**, `FD00H` to `FDFEH`. That is all the RAM the
  instructions can name. Overflowing it is a link error, not a silent
  truncation.
- **The branch form needs the load to fold into it**, which it does for a
  `volatile` variable — the spelling a word an interrupt also touches wants
  anyway. Without `volatile` the compiler may have moved the load away from the
  branch, and then a compare and jump is what comes out. `bset` and `bclr` are
  selected either way.

Reading or writing the whole word is an ordinary access, and a pointer to one
of these is an ordinary `unsigned short *`: the instruction needs the address
in itself rather than in a register, so going through a pointer is an ordinary
load or store.

Registers are bit-addressable too, and need no attribute — bitoff `F0H + n`
names `R0` to `R15`, so `x |= 8` on a local is `bset` on whichever register the
allocator picked.

## Interrupt handlers

```c
__attribute__((interrupt)) void timer0(void) { ... }
```

The handler saves and restores every register it touches and returns with
`RETI`. It must take no parameters and return `void`, cannot be called from
C, and is never inlined.

An optional trap number claims that slot of the vector table and has the
compiler write what goes in it:

```c
__attribute__((interrupt(26))) void t3_isr(void) { ... }
```

A slot holds a jump rather than an address, because the hardware branches to
the slot instead of reading through it, so what is emitted is a `JMPS` to the
handler in a section named `.vectors.026` — the trap number in decimal, padded
to three digits. The three linker scripts under
`llvm/lib/Target/C166/startup/` place all 128 of them at four times their
number, and nothing has to be asked for: the table appears when a program has
handlers and costs nothing when it does not. The number is 1 to 127, trap 0
being reset, and two handlers asking for one slot is an error.

### A register bank of its own

```c
__attribute__((interrupt(36), c166_bank)) void t4_isr(void) { ... }
```

`R0` to `R15` are a window into internal RAM at the address the context
pointer holds, so moving that pointer moves the whole window. With
`c166_bank` the handler enters with `SCXT CP, #bank` and leaves with
`POP CP`: the interrupted code's registers are never touched, so there is
nothing to spill and nothing to reload — one instruction in and one out,
against up to fifteen of each.

| a handler that calls a function | without | with |
|---|---|---|
| entry and exit | 114 states | **64** |
| handler size | 110 bytes | **42** |

A leaf handler that touches one register saves nothing — there was only one
register to save. The win is in handlers with a lot live at once, which is
what calling anything makes of one.

The bank is 32 bytes of internal RAM, reserved by the compiler and placed by
the linker script — the only memory a context pointer may name. The scripts
here have room for seven; an eighth is a link error naming the region. Each
handler gets its own, so a higher-priority handler interrupting a banked one
cannot corrupt it.

A `NOP` follows the `SCXT`: no manual to hand says whether the instruction
after one that writes `CP` already sees the new window, and being wrong would
put the handler's first register write in the interrupted code's bank.

`MDL`, `MDH`, `MDC` and the MAC accumulator are not part of the window and are
still saved by a handler that disturbs them. `R0` is the ABI stack pointer and
belongs to the interrupted code, so a handler that calls anything or needs a
frame copies it across in three instructions; a leaf that fits in registers
pays nothing for it.

Without a number the handler is still a handler, and reaching it is left to a
hand-written vector — which is what a program with a shared interrupt node or
its own dispatcher wants. That is the same section by another route:

```c
asm(".section .vectors.026,\"ax\",@progbits\n\t"
    "jmps #seg(t3_isr), sof(t3_isr)\n\t"
    ".text");
```

or, better, the `VECTOR_` macros in `startup/xc164cm-vectors.inc`, which name
every source the part has after its interrupt control register rather than
after a number that has to be looked up.

That the saving and restoring is right is checked rather than assumed:
`llvm/utils/C166Sim/differential/interrupts.c` runs a computation whose every
step depends on the last while the simulator fires a handler into it a few
dozen times, and the answer has to match the host's, which had no interrupts
at all. The simulator raises the requests itself — see `--interrupt-at` and
`--interrupt-every` in `llvm/utils/C166Sim/README.txt`, since there are no
peripherals to raise them.

Both attributes are described in Clang's attribute reference alongside every
other target's.

## Inline assembly

Two constraints name a register class:

| Constraint | Registers | Why |
|---|---|---|
| `r` | R0 to R15 | The general purpose registers. |
| `q` | R0 to R3 | The pointer field of an indirect form is two bits wide, so `add Rwn, [Rwm]` can only name these four. `r` would hand back whichever register was spare, and the result assembles only by luck. |

A byte operand needs no constraint of its own: give `r` a byte-sized value and
it gets a byte register, because R0 to R7 have byte halves and the compiler
picks one.

```c
unsigned char swapped(unsigned char v) {
  unsigned char r;
  __asm__("movb %0, %1" : "=r"(r) : "r"(v));   /* movb rl2, rl2 */
  return r;
}
```

There is no constraint for a register *pair*. A 32-bit value in an asm
statement arrives as two operands in two registers that need not be adjacent,
so an instruction that wants a pair — the divides, which read MDL and MDH —
has to be written with the registers named.

### Register names

An asm statement can name a register in a clobber list, or pin a value to one
with `register ... __asm__("name")`:

- `r0` to `r15`, and the byte halves `rl0`/`rh0` through `rl7`/`rh7`
- `psw`, `mdl`, `mdh`, `mdc`, `sp`, `cp`
- the multiply-accumulate unit's: `idx0`, `idx1`, `qx0`, `qx1`, `qr0`, `qr1`,
  `mal`, `mah`, `mas`, `mcw`, `msw`, `mrw`

GCC's `"{name}"` constraint syntax is not a thing clang implements, for this
target or any other; `register ... __asm__("name")` is the spelling that
works.

Everything from `psw` down in that list is a special function register, which
is to say a memory location with a name rather than a register the machine can
move to and from. Pinning a value to one is still written the ordinary way,
and the compiler emits the absolute addressed `MOV` that reaches it — the same
instruction the assembler emits for `mov r2, idx0`.

Three things cannot be pinned to one, and say so rather than producing
something that only looks right:

- `mas`, which is the saturated view of the accumulator's high word and has no
  address of its own. `CoSTORE` names it by a five-bit code, and no move takes
  one.
- a register the selected part does not have — `qx0` without `+mac`, say. The
  name list is flat, as a GCC register list is, so it does not know which part
  is selected; the refusal comes from the code generator, which does.
- a byte value. A byte write to a word-wide special function register writes
  the whole word, with `00H` in the half that was not addressed, so
  `register unsigned char x __asm__("mal")` would quietly throw away the other
  half of MAL.

All three are still fine in a clobber list, which asks for no move.

### Reaching the coprocessor

Nothing selects the coprocessor's repeatable forms, so an asm statement is how
they are used. Two things have to be right, and only one of them is about the
assembly.

**Where the data is.** IDX0 and IDX1 reach the internal dual-port RAM and
nothing else — PM0036 section 2.1, with `CoMOV` the one exception. Ordinary
static data is not there; it is in whatever RAM the part uses for it, which on
most parts the coprocessor cannot address at all. So an array a `CoMAC` walks
through `[idx0+]` must be declared `__dpram`:

```c
__dpram short delay[8];             /* IDX0 walks this   */
static const short coef[8] = {…};   /* a register walks this, so anywhere */
```

The second operand goes through a general purpose register, which reaches the
whole memory space, so a coefficient table needs no attribute. The dual-port
RAM is 2 KByte on the parts here and the stacks are in it, so what goes there
should be what has to.

**Which pointer.** Which of the two an instruction runs on is in the encoding
rather than in an operand, so naming the register is the only way to say it:

```c
long dot(unsigned short n) {
  register const short *p __asm__("idx0") = delay;
  register unsigned short rep __asm__("mrw") = n - 1;
  register unsigned short lo __asm__("mal");
  register unsigned short hi __asm__("mah");
  const short *q = coef;
  unsigned short zero = 0;
  __asm__ volatile("coload %5, %5\n\t"
                   "repeat mrw times comac [idx0+], [%1+]"
                   : "+r"(p), "+q"(q), "=r"(lo), "=r"(hi), "+r"(rep)
                   : "r"(zero)
                   : "msw", "memory");
  return ((long)hi << 16) | lo;
}
```

The pointers are read-write because the instruction steps them: told they were
inputs, the compiler would go on using a register holding a value that is no
longer there. The accumulator is cleared from a register holding zero rather
than with `coload r0, r0`, which would load the ABI stack pointer. `repeat mrw
times` takes the count from the MAC repeat word, which the manual gives as
`MRW[12:0] + 1` — hence the `n - 1` — and a literal count is `repeat 8 times`.

`llvm/utils/C166Sim/differential/macasm.c` is this worked through, run in the
simulator and checked against the host. The simulator stops with *an IDX
pointer outside the dual-port RAM* if the array was not placed, which is a
mistake nothing else catches: it assembles, it links, and on the part it reads
whatever the unit sees there instead.

## Predefined macros

| Macro | When |
| ----- | ---- |
| `C166`, `__C166__`, `__c166__` | always |
| `__C166_EXT_INSTR__` | the `EXTend` instructions and `ATOMIC` are available |
| `__C166_MAC__` | the multiply-accumulate coprocessor is available |
| `__far` | always; expands to the address-space attribute |

Ask `__C166_MAC__` rather than guessing from the part number.

## Linking

There is no default linker script, and that is deliberate: the memory map
belongs to the board. `llvm/lib/Target/C166/startup` holds three to start
from, along with the startup code and a small runtime:

| File | For |
| ---- | --- |
| `c166.ld`, `crt0.S` | an XC164CM |
| `c167.ld`, `c167-crt0.S` | a C167 |
| `st10.ld` | an ST10, which uses the C167's startup file |

The driver puts the right startup object first — `c167-crt0.o` for the `c167`
and `st10` cores, `crt0.o` otherwise — and passes the script through with
`-T`, which is not optional:

```
clang --target=c166 -mmcu=st10f269 --sysroot=$SYSROOT \
      -T llvm/lib/Target/C166/startup/st10.ld program.c -o program.elf
```

What `-mmcu=` contributes is the sizes, passed to the linker as `--defsym`
symbols the script reads. A board that knows better can define any of them
itself; a script used with no part named falls back to the part it documents.

| Symbol | Meaning |
| ------ | ------- |
| `__c166_rom_length` | on-chip program memory |
| `__c166_iram_length` | internal RAM at `00'F600H` |
| `__c166_xram_length`, `__c166_xram_origin` | extension RAM, and where it starts |
| `__c166_xram2_length`, `__c166_xram2_origin` | a second extension RAM, ST10 only |
| `__c166_dsram_length`, `__c166_dpram_length`, `__c166_psram_length` | the XC16x's three RAMs |
| `__c166_farrom_length`, `__c166_farrom2_length` | program memory past the near window |
| `__c166_enable_xper` | whether startup sets `SYSCON.XPEN` |
| `__c166_xpercon`, `__c166_write_xpercon` | what startup writes to `XPERCON`, ST10 only |

One rule is worth knowing before writing a script of your own: **no region may
cross a segment boundary.** A far access carries one segment and a near
branch cannot leave one, so an object that straddled a boundary would be
reached with the wrong segment. The scripts here assert it rather than assume
it, and a script of your own should too.

## What is not there

Enough to be worth saying plainly:

- **Nothing has been run on silicon.** `llvm/utils/C166Sim` runs what comes
  out and is checked against the host, but a simulator is not a part.
- **No debugger knows this architecture.** DWARF is emitted and
  `llvm-dwarfdump` reads it; nothing puts a source-level front end on it.
- **Six of the coprocessor's instructions are selected.** The other 174
  assemble and disassemble but nothing generates them, and there are no
  builtins: reaching them from C means an asm statement, which the register
  names above make workable but do not make pleasant. Written by hand
  they take the repeat prefix — `repeat 3 times comac r2, [r3+]`, or
  `repeat mrw times` to take the count from the MAC repeat word — on the 89
  forms the manual marks repeatable, and the simulator runs those 89. The
  other 85 assemble and disassemble but stop it.
- **The ELF relocations are this backend's own invention.** LLD implements
  them and nothing else does.
- **The extended special function registers are reachable by address but not
  through a register field**: `mov syscon1, r2` works and `push syscon1` does
  not.

`llvm/lib/Target/C166/README.txt` is the implementer's document and is far
more detailed than this page, including on all of the above.

## Reference documents

The manuals this target was built against are listed under **C166** in
{doc}`CompilerWriterInfo`.
