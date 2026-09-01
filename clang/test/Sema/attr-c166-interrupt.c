// RUN: %clang_cc1 -triple c166 -fsyntax-only -verify %s

// The optional argument is the trap number whose vector table slot gets a jump
// to the handler.  1 to 127: trap 0 is reset and the startup code owns it.
__attribute__((interrupt(1))) void isr_first_slot(void) {}   // no diagnostics
__attribute__((interrupt(127))) void isr_last_slot(void) {}  // no diagnostics

__attribute__((interrupt(0))) void isr_reset_slot(void) {} // expected-error {{'interrupt' attribute requires integer constant between 1 and 127 inclusive}}

__attribute__((interrupt(128))) void isr_past_the_end(void) {} // expected-error {{'interrupt' attribute requires integer constant between 1 and 127 inclusive}}

__attribute__((interrupt(-1))) void isr_negative(void) {} // expected-error {{'interrupt' attribute requires integer constant between 1 and 127 inclusive}}

__attribute__((interrupt("timer"))) void isr_not_a_number(void) {} // expected-error {{'interrupt' attribute requires parameter 0 to be an integer constant}}

__attribute__((interrupt(1, 2))) void isr_two_args(void) {} // expected-error {{'interrupt' attribute takes no more than 1 argument}}

__attribute__((interrupt)) void isr_takes_param(int a) {} // expected-warning {{C166 'interrupt' attribute only applies to functions that have no parameters}}

__attribute__((interrupt)) int isr_returns_int(void) { return 0; } // expected-warning {{C166 'interrupt' attribute only applies to functions that have a 'void' return type}}

__attribute__((interrupt)) int not_a_function; // expected-warning {{'interrupt' attribute only applies to functions}}

__attribute__((interrupt)) void isr_ok(void) {} // no diagnostics

// A bank of its own, which is only meaningful where nothing was passed in a
// register: an ordinary function's arguments arrive in R2 and up, which are in
// the bank it would have just left.
__attribute__((interrupt(40), c166_bank)) void isr_banked(void) {} // no diagnostics

__attribute__((interrupt)) __attribute__((c166_bank)) void isr_banked_no_slot(void) {} // no diagnostics

__attribute__((c166_bank)) void not_a_handler(void) {} // expected-error {{'c166_bank' attribute only applies to functions that also have the 'interrupt' attribute}}

// The order the two are written in must not matter, and this is the one that
// would if the check looked at the parsed attribute rather than the decl.
__attribute__((c166_bank, interrupt)) void bank_first(void) {} // expected-error {{'c166_bank' attribute only applies to functions that also have the 'interrupt' attribute}}

__attribute__((c166_bank)) int bank_not_a_function; // expected-warning {{'c166_bank' attribute only applies to functions}}

__attribute__((far)) void far_ok(void) {}

__attribute__((far)) int far_not_a_function; // expected-warning {{'far' attribute only applies to functions}}

// 'far' shares its parse kind with the MIPS long_call attribute, whose mutual
// exclusion with short_call must not follow it here: short_call does not exist
// on this target.
__attribute__((short_call)) void no_such_attribute(void); // expected-warning {{unknown attribute 'short_call' ignored}}

__attribute__((long_call, far)) void far_twice(void) {}
