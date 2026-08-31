// RUN: %clang_cc1 -triple c166 -fsyntax-only -verify %s

// __bitaddr places a variable in the bit-addressable RAM, which is the only
// memory a bit instruction can name.
__bitaddr unsigned short flags;
__bitaddr unsigned char byteflags;
__bitaddr unsigned short seeded = 5;
static __bitaddr unsigned short internal;
extern __bitaddr unsigned short elsewhere;

void ok(void) {
  static __bitaddr unsigned short kept;
  kept = flags;
}

void bad(void) {
  __bitaddr unsigned short automatic;    // expected-error {{'c166_bitaddr' attribute only applies to variables with static storage duration}}
  (void)automatic;
}

__bitaddr void notavariable(void);       // expected-warning {{'c166_bitaddr' attribute only applies to variables}}

struct s {
  __bitaddr unsigned short member;       // expected-warning {{'c166_bitaddr' attribute only applies to variables}}
};

void param(__bitaddr unsigned short p);  // expected-error {{'c166_bitaddr' attribute only applies to variables with static storage duration}}

// Nothing stops a variable being in both kinds of special memory being asked
// for, and the two are different memories, so it is refused where it would be
// silently resolved one way.
__bitaddr __dpram unsigned short both;   // expected-error {{'c166_dpram' and 'c166_bitaddr' attributes are not compatible}} \
                                         // expected-note {{conflicting attribute is here}}
