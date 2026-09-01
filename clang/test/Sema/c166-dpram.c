// RUN: %clang_cc1 -triple c166 -target-cpu xc16x -fsyntax-only -verify %s

// __dpram places an object in the internal dual-port RAM, which is the only
// memory the coprocessor's IDX0 and IDX1 reach.
__dpram short delay[32];
__dpram short seeded[4] = {1, 2, 3, 4};
static __dpram short internal;
extern __dpram short elsewhere;

void ok(void) {
  static __dpram short kept;
  kept = delay[0];
}

void bad(void) {
  __dpram short automatic;                 // expected-error {{'c166_dpram' attribute only applies to variables with static storage duration}}
  (void)automatic;
}

__dpram void notavariable(void);           // expected-warning {{'c166_dpram' attribute only applies to variables}}

// A field is not a variable at all, so the generic subject check catches it
// before the storage duration one does.
struct s {
  __dpram short member;                    // expected-warning {{'c166_dpram' attribute only applies to variables}}
};

void param(__dpram short p);               // expected-error {{'c166_dpram' attribute only applies to variables with static storage duration}}
