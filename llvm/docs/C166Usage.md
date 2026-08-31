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
implies it. Elsewhere it has to be asked for, and at present the only
spelling the driver accepts is the general one:

```
clang --target=c166 -mcpu=st10 -Xclang -target-feature -Xclang +mac ...
```

There is no `-mmac`; that is a rough edge rather than a design.

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

## Interrupt handlers

```c
__attribute__((interrupt)) void timer0(void) { ... }
```

The handler saves and restores every register it touches and returns with
`RETI`. It must take no parameters and return `void`, cannot be called from
C, and is never inlined. Putting it in the vector table is the linker
script's business or a hand-written vector's, exactly as with the vendor
toolchains.

Both attributes are described in Clang's attribute reference alongside every
other target's.

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
- **Four of the coprocessor's instructions are selected.** The other 176
  assemble and disassemble but nothing generates them, and there are no
  builtins or inline-asm constraints to reach them from C.
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
