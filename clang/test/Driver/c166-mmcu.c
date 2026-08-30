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
