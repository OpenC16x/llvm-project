// RUN: %clang_cc1 -triple c166 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple c166 -O2 -S -o - %s | FileCheck %s --check-prefix=ASM

// The attribute reaches the backend as a string attribute on the global, and
// with word alignment: a bit instruction names a word, so an object at an odd
// address has no bit address at all.  A char would otherwise be aligned to 1.
// CHECK: @seeded = global i16 -23101, align 2 #[[BA:[0-9]+]]
// CHECK: @flags = global i16 0, align 2 #[[BA]]
// CHECK: @byteflags = global i8 0, align 2 #[[BA]]
// CHECK: @ordinary = global i16 0, align 2{{$}}
// CHECK: attributes #[[BA]] = { "c166-bitaddr" }
__bitaddr volatile unsigned short flags;
__bitaddr volatile unsigned char byteflags;
__bitaddr unsigned short seeded = 0xA5C3;
unsigned short ordinary;

extern void act(void);

// Setting or clearing a named bit is one instruction against the variable,
// with the word number left to the linker.
// ASM-LABEL: arm:
// ASM: bset flags.3
void arm(void) { flags |= 8; }

// ASM-LABEL: disarm:
// ASM: bclr flags.3
void disarm(void) { flags &= ~8; }

// Testing one is a branch that reads the word itself.  Bit 11 is in the high
// byte, which is the half a word access gets narrowed to.
// ASM-LABEL: poll:
// ASM: jnb flags.11
void poll(void) { if (flags & 0x800) act(); }

// A variable that is a byte to begin with names the same word and the bit it
// says.
// ASM-LABEL: pollbyte:
// ASM: jnb byteflags.5
void pollbyte(void) { if (byteflags & 0x20) act(); }

// The two sections, which the data lands in after the code.
// ASM: .section .bitdata,"aw",@progbits
// ASM: seeded:
// ASM: .section .bitbss,"aw",@nobits
// ASM: flags:
