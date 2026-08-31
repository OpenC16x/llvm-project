// Naming a part is what selects its memory map, its core, and the macros that
// say which one it is.  The map goes to the linker as symbols the script reads,
// so that nobody edits a memory map by hand to move between parts.

// RUN: %clang -### --target=c166 -mmcu=xc164cm-8f %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CM8F
//
// 64 KByte of program memory: 48 of it under the data page pointers and the
// remaining 16 reachable only by a far access, all in one segment.
// CM8F: "-target-cpu" "xc16x"
// CM8F: "--defsym=__c166_rom_length=49152"
// CM8F-SAME: "--defsym=__c166_farrom_length=16384"
// CM8F-SAME: "--defsym=__c166_farrom2_length=0"
// CM8F-SAME: "--defsym=__c166_dsram_length=2048"
// CM8F-SAME: "--defsym=__c166_dpram_length=2048"
// CM8F-SAME: "--defsym=__c166_psram_length=2048"

// RUN: %clang -### --target=c166 -mmcu=xc164cm-4f %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CM4F
//
// 32 KByte, which is less than the data page pointers cover, so there is no
// far region at all - and no data SRAM, which is what the derivative table
// says and is the case the linker script has to put static data elsewhere for.
// CM4F: "--defsym=__c166_rom_length=32768"
// CM4F-SAME: "--defsym=__c166_farrom_length=0"
// CM4F-SAME: "--defsym=__c166_farrom2_length=0"
// CM4F-SAME: "--defsym=__c166_dsram_length=0"

// RUN: %clang -### --target=c166 -mmcu=xc164cs-16f %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CS16F
//
// 128 KByte is two segments.  A far access carries one segment and a near
// branch cannot leave one, so the second is a region of its own rather than
// more of the first.
// CS16F: "--defsym=__c166_rom_length=49152"
// CS16F-SAME: "--defsym=__c166_farrom_length=16384"
// CS16F-SAME: "--defsym=__c166_farrom2_length=65536"

// -mcpu= still wins, for building for a core rather than for a board.
// RUN: %clang -### --target=c166 -mmcu=xc164cm-8f -mcpu=c167 -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CPUWINS
// CPUWINS: "-target-cpu" "c167"

// The part says which one it is, so that code can ask.
// RUN: %clang -### --target=c166 -mmcu=xc164cs-16r -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFS
// DEFS: "-D" "__XC164CS_16R__"
// DEFS-SAME: "-D" "__C166_PROGRAM_SIZE__=131072"
// DEFS-SAME: "-D" "__C166_XRAM_SIZE__=2048"
// DEFS-SAME: "-D" "__C166_IRAM_SIZE__=2048"
// DEFS-SAME: "-D" "__C166_PSRAM_SIZE__=2048"
// DEFS-SAME: "-D" "__C166_PROGRAM_IS_ROM__"

// A misspelled part is a diagnostic rather than a link that will not run, and
// it says what it would have accepted.  Compiling is enough to get it: the
// name is wrong whether or not anything is being linked.
// RUN: not %clang --target=c166 -mmcu=xc164 -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNKNOWN
// UNKNOWN: unknown part 'xc164' for '-mmcu='; known parts are: {{.*}}xc164cm-8f{{.*}}xc164cs-8r

int main(void) { return 0; }

// A C167's map is a different shape, so it gets different symbols: its program
// memory is at the bottom of a segment rather than in one of its own, and the
// only RAM is the internal RAM window and the extension RAM.  The startup file
// is the core's too - a C167 has no PLL to program.

// RUN: %clang -### --target=c166 -mmcu=c167cr-16rm %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=C167CR16
//
// C167CR16: "-target-cpu" "c167"
// C167CR16: "c167-crt0.o"
// C167CR16: "--defsym=__c166_rom_length=131072"
// C167CR16-SAME: "--defsym=__c166_iram_length=2048"
// C167CR16-SAME: "--defsym=__c166_xram_length=2048"
// C167CR16-NOT: "--defsym=__c166_psram_length
// C167CR16-NOT: "--defsym=__c166_dsram_length

// A romless part has no on-chip program memory at all; what is out there is
// the board's, and its script says so.

// RUN: %clang -### --target=c166 -mmcu=c167sr-lm %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=C167SR
//
// C167SR: "--defsym=__c166_rom_length=0"
// C167SR-SAME: "--defsym=__c166_iram_length=2048"

// An XC16x part keeps the startup file it had.
// RUN: %clang -### --target=c166 -mmcu=xc164cm-8f %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CM8FCRT
// CM8FCRT: "{{[^"]*}}crt0.o"
// CM8FCRT-NOT: c167-crt0.o

// The startup file follows the core, so naming the core alone is enough - a
// program built for a C167 with no part named is still built for a C167.
// RUN: %clang -### --target=c166 -mcpu=c167 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=C167CPU
// C167CPU: "c167-crt0.o"

// The ST10 parts, where the extension RAM is the one memory whose address the
// row carries rather than the core.  Two of these have the same core and the
// same 256 KByte of Flash and put that RAM in different places, which is why
// the table can say.

// RUN: %clang -### --target=c166 -mmcu=st10f269 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ST10F269
//
// An ST10F269's XRAM2 runs up to 00'DFFFH and its XRAM1 starts at 00'E000H, so
// the two adjoin: 10 KByte in one region at 00'C000H, and no second one.
// ST10F269: "-target-cpu" "st10"
// ST10F269: "c167-crt0.o"
// ST10F269: "--defsym=__c166_rom_length=262144"
// ST10F269-SAME: "--defsym=__c166_iram_length=2048"
// ST10F269-SAME: "--defsym=__c166_xram_length=10240"
// ST10F269-SAME: "--defsym=__c166_xram_origin=49152"
// ST10F269-NOT: "--defsym=__c166_xram2_length
// ST10F269-NOT: "--defsym=__c166_psram_length
//
// Its 10 KByte is XRAM1's 2 and XRAM2's 8, and XRAM2 is off at reset - so
// startup has to write XPERCON, in the one window before SYSCON.XPEN.  0DH is
// the reset value 05H with XRAM2EN added.  A region larger than XRAM1's 2
// KByte is the only sign this part gives that XRAM2 is in use, which is why
// the test is here: get it wrong and 8 of its 10 KByte stay switched off, and
// nothing else would notice.
// RUN: %clang -### --target=c166 -mmcu=st10f269 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ST10F269X
// ST10F269X: "--defsym=__c166_enable_xper=1"
// ST10F269X-SAME: "--defsym=__c166_xpercon=13"
// ST10F269X-SAME: "--defsym=__c166_write_xpercon=1"

// RUN: %clang -### --target=c166 -mmcu=st10f272e %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ST10F272E
//
// An ST10F272E has the C167's 2 KByte at 00'E000H and a second 16 KByte at
// 09'0000H, which no data page pointer here holds - so that one is a far
// region rather than more of the same region.
// ST10F272E: "--defsym=__c166_rom_length=262144"
// ST10F272E-SAME: "--defsym=__c166_xram_length=2048"
// ST10F272E-SAME: "--defsym=__c166_xram_origin=57344"
// ST10F272E-SAME: "--defsym=__c166_xram2_length=16384"
// ST10F272E-SAME: "--defsym=__c166_xram2_origin=589824"
// ST10F272E-SAME: "--defsym=__c166_enable_xper=1"
// ST10F272E-SAME: "--defsym=__c166_xpercon=13"
// ST10F272E-SAME: "--defsym=__c166_write_xpercon=1"

// The B has the same map with half the second RAM, which is the whole of the
// difference between the two.
// RUN: %clang -### --target=c166 -mmcu=st10f272b %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ST10F272B
// ST10F272B: "--defsym=__c166_xram_length=2048"
// ST10F272B-SAME: "--defsym=__c166_xram_origin=57344"
// ST10F272B-SAME: "--defsym=__c166_xram2_length=8192"
// ST10F272B-SAME: "--defsym=__c166_xram2_origin=589824"

// An ST10 takes the C167's startup, and naming the core alone is enough.
// RUN: %clang -### --target=c166 -mcpu=st10 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ST10CPU
// ST10CPU: "c167-crt0.o"

// A C167 writes no XPERCON: its one extension RAM is already selected at
// reset, so the shared startup branches over that write.
// RUN: %clang -### --target=c166 -mmcu=c167cr-16rm %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=C167XPER
// C167XPER: "--defsym=__c166_enable_xper=1"
// C167XPER-NOT: "--defsym=__c166_xpercon
// C167XPER-NOT: "--defsym=__c166_write_xpercon
