//===-- C166InstrMAC.td - C166 MAC unit instructions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The multiply-accumulate coprocessor of the C166S V2 core, which the XC164CM
// has.  Every instruction here is four bytes and the layout is the same shape
// throughout, taken from the Format line the C166S V2 User's Manual gives each
// instruction in its detailed description chapter:
//
//     A3 nm CA rrr0:0000        CoABS Rwn, Rwm
//     83 nm CA rrr0:0qqq        CoABS Rwn, [Rwm*]
//     93 Xm CA rrr0:0qqq        CoABS [IDXi*], [Rwm*]
//
// So the first byte selects the operand form, the second holds the register
// numbers or the pointer control, the third is the operation, and the fourth
// carries the repeat control and what happens to the general purpose register
// pointer.  That puts rrr at bits 31 to 29, qqq at 26 to 24 and the IDX
// control at 15 to 12, which is what the manual says of all three separately.
//
// rrr is always 000 here, the plain form.  The other values it defines are a
// repeat prefix written "- USR0 CoXXX", which puts two tokens in front of the
// mnemonic; the matcher keys on the mnemonic being first, so those forms are
// not accepted.  The encoding for them is in the manual if they are ever
// wanted.
//
// Nothing selects any of this.  It is here to be written by hand, which is
// what a MAC unit is for on a part whose compiler has no DSP patterns.
//
// This file is generated from the manual's Format lines; see
// llvm/lib/Target/C166/README.txt.
//
//===----------------------------------------------------------------------===//

// A MAC instruction: four bytes, with the repeat control left at the plain
// form and the two bits above the pointer control always zero.
class CoInst<bits<8> op, bits<8> ext, dag ins, string asmstr>
    : C166Inst<(outs), ins, 4, asmstr, []> {
  let Inst{7-0} = op;
  let Inst{23-16} = ext;
  let Inst{28-27} = 0b00;
  let Inst{31-29} = 0b000;
  let hasSideEffects = 1;
}

// A3 nm EE rrr0:0000 - two general purpose registers.
class CoRR<bits<8> ext, string asmstr>
    : CoInst<0xA3, ext, (ins GR16:$n, GR16:$m), asmstr> {
  bits<4> n;
  bits<4> m;
  let Inst{15-12} = n;
  let Inst{11-8} = m;
  let Inst{26-24} = 0;
}

// A3 nn EE rrr0:0000 - one register, repeated in both nibbles.
class CoR<bits<8> ext, string asmstr>
    : CoInst<0xA3, ext, (ins GR16:$n), asmstr> {
  bits<4> n;
  let Inst{15-12} = n;
  let Inst{11-8} = n;
  let Inst{26-24} = 0;
}

// A3 00 EE rrr#:# - a five bit shift count in the last byte.
class CoI<bits<8> ext, string asmstr>
    : CoInst<0xA3, ext, (ins imm5:$s), asmstr> {
  bits<5> s;
  let Inst{15-8} = 0;
  let Inst{28-24} = s;
}

// A3 00 EE rrr0:0000 - no operands at all.
class CoN<bits<8> ext, string asmstr>
    : CoInst<0xA3, ext, (ins), asmstr> {
  let Inst{15-8} = 0;
  let Inst{26-24} = 0;
}

// 83 nm EE rrr0:0qqq - a register and a pointer.
class CoRP<bits<8> ext, string asmstr>
    : CoInst<0x83, ext, (ins GR16:$n, coptr:$m), asmstr> {
  bits<4> n;
  bits<7> m;
  let Inst{15-12} = n;
  let Inst{11-8} = m{3-0};
  let Inst{26-24} = m{6-4};
}

// 83 mm EE rrr0:0qqq - a pointer alone, its register in both nibbles.
class CoP<bits<8> ext, string asmstr>
    : CoInst<0x83, ext, (ins coptr:$m), asmstr> {
  bits<7> m;
  let Inst{15-12} = m{3-0};
  let Inst{11-8} = m{3-0};
  let Inst{26-24} = m{6-4};
}

// 93 Xm EE rrr0:0qqq - both operands pointers, the first a MAC unit one.
class CoXP<bits<8> op, bits<8> ext, string asmstr>
    : CoInst<op, ext, (ins coidx:$x, coptr:$m), asmstr> {
  bits<4> x;
  bits<7> m;
  let Inst{15-12} = x;
  let Inst{11-8} = m{3-0};
  let Inst{26-24} = m{6-4};
}

// 93 X0 EE rrr0:0001 - a MAC unit pointer alone.
class CoX<bits<8> ext, string asmstr>
    : CoInst<0x93, ext, (ins coidx:$x), asmstr> {
  bits<4> x;
  let Inst{15-12} = x;
  let Inst{11-8} = 0;
  let Inst{26-24} = 0b001;
}

// 93 1m EE rrr0:0qqq - a general purpose pointer alone, in the form that
// otherwise takes two.
class CoQ<bits<8> ext, string asmstr>
    : CoInst<0x93, ext, (ins coptr:$m), asmstr> {
  bits<7> m;
  let Inst{15-12} = 0b0001;
  let Inst{11-8} = m{3-0};
  let Inst{26-24} = m{6-4};
}

// B3 nn wwww:w000 rrr0:0qqq - storing a MAC register through a pointer.  The
// register to store is named by a five bit code rather than by an address,
// which is what the manual calls the CoREG addressing mode.  Table 2-9 of that
// manual puts the code at bits 31 to 27, where the repeat control already is;
// the Format lines put it here, and they agree with each other.
class CoStoreP<string asmstr>
    : CoInst<0xB3, 0, (ins coptr:$n, coreg:$w), asmstr> {
  bits<7> n;
  bits<5> w;
  let Inst{15-12} = n{3-0};
  let Inst{11-8} = n{3-0};
  let Inst{23-19} = w;
  let Inst{18-16} = 0;
  let Inst{26-24} = n{6-4};
}

// C3 nn wwww:w000 rrr0:0000 - the same into a register.
class CoStoreR<string asmstr>
    : CoInst<0xC3, 0, (ins GR16:$n, coreg:$w), asmstr> {
  bits<4> n;
  bits<5> w;
  let Inst{15-12} = n;
  let Inst{11-8} = n;
  let Inst{23-19} = w;
  let Inst{18-16} = 0;
  let Inst{26-24} = 0;
}


def CoABS_n            : CoN<0x1A, "coabs">;
def CoABS_rr           : CoRR<0xCA, "coabs\t$n, $m">;
def CoABS_rp           : CoRP<0xCA, "coabs\t$n, $m">;
def CoABS_xp           : CoXP<0x93, 0xCA, "coabs\t$x, $m">;
def CoADD_rr           : CoRR<0x02, "coadd\t$n, $m">;
def CoADD_rp           : CoRP<0x02, "coadd\t$n, $m">;
def CoADD_xp           : CoXP<0x93, 0x02, "coadd\t$x, $m">;
def CoADD2_rr          : CoRR<0x42, "coadd2\t$n, $m">;
def CoADD2_rp          : CoRP<0x42, "coadd2\t$n, $m">;
def CoADD2_xp          : CoXP<0x93, 0x42, "coadd2\t$x, $m">;
def CoASHR_i_rnd       : CoI<0xB2, "coashr\t$s, rnd">;
def CoASHR_r_rnd       : CoR<0xBA, "coashr\t$n, rnd">;
def CoASHR_p_rnd       : CoP<0xBA, "coashr\t$m, rnd">;
def CoASHR_i           : CoI<0xA2, "coashr\t$s">;
def CoASHR_r           : CoR<0xAA, "coashr\t$n">;
def CoASHR_p           : CoP<0xAA, "coashr\t$m">;
def CoCMP_rr           : CoRR<0xC2, "cocmp\t$n, $m">;
def CoCMP_rp           : CoRP<0xC2, "cocmp\t$n, $m">;
def CoCMP_xp           : CoXP<0x93, 0xC2, "cocmp\t$x, $m">;
def CoLOAD_rr          : CoRR<0x22, "coload\t$n, $m">;
def CoLOAD_rp          : CoRP<0x22, "coload\t$n, $m">;
def CoLOAD_xp          : CoXP<0x93, 0x22, "coload\t$x, $m">;
def CoLOADN_rr         : CoRR<0x2A, "coload-\t$n, $m">;
def CoLOADN_rp         : CoRP<0x2A, "coload-\t$n, $m">;
def CoLOADN_xp         : CoXP<0x93, 0x2A, "coload-\t$x, $m">;
def CoLOAD2_rr         : CoRR<0x62, "coload2\t$n, $m">;
def CoLOAD2_rp         : CoRP<0x62, "coload2\t$n, $m">;
def CoLOAD2_xp         : CoXP<0x93, 0x62, "coload2\t$x, $m">;
def CoLOAD2N_rr        : CoRR<0x6A, "coload2-\t$n, $m">;
def CoLOAD2N_rp        : CoRP<0x6A, "coload2-\t$n, $m">;
def CoLOAD2N_xp        : CoXP<0x93, 0x6A, "coload2-\t$x, $m">;
def CoMAC_rr_rnd       : CoRR<0xD1, "comac\t$n, $m, rnd">;
def CoMAC_rp_rnd       : CoRP<0xD1, "comac\t$n, $m, rnd">;
def CoMAC_xp_rnd       : CoXP<0x93, 0xD1, "comac\t$x, $m, rnd">;
def CoMAC_rr           : CoRR<0xD0, "comac\t$n, $m">;
def CoMAC_rp           : CoRP<0xD0, "comac\t$n, $m">;
def CoMAC_xp           : CoXP<0x93, 0xD0, "comac\t$x, $m">;
def CoMACN_rr          : CoRR<0xE0, "comac-\t$n, $m">;
def CoMACN_rp          : CoRP<0xE0, "comac-\t$n, $m">;
def CoMACN_xp          : CoXP<0x93, 0xE0, "comac-\t$x, $m">;
def CoMACM_xp_rnd      : CoXP<0x93, 0xD9, "comacm\t$x, $m, rnd">;
def CoMACM_xp          : CoXP<0x93, 0xD8, "comacm\t$x, $m">;
def CoMACMN_xp         : CoXP<0x93, 0xE8, "comacm-\t$x, $m">;
def CoMACMR_xp_rnd     : CoXP<0x93, 0xF9, "comacmr\t$x, $m, rnd">;
def CoMACMR_xp         : CoXP<0x93, 0xF8, "comacmr\t$x, $m">;
def CoMACMRsu_xp_rnd   : CoXP<0x93, 0x79, "comacmrsu\t$x, $m, rnd">;
def CoMACMRsu_xp       : CoXP<0x93, 0x78, "comacmrsu\t$x, $m">;
def CoMACMRu_xp_rnd    : CoXP<0x93, 0x39, "comacmru\t$x, $m, rnd">;
def CoMACMRu_xp        : CoXP<0x93, 0x38, "comacmru\t$x, $m">;
def CoMACMRus_xp_rnd   : CoXP<0x93, 0xB9, "comacmrus\t$x, $m, rnd">;
def CoMACMRus_xp       : CoXP<0x93, 0xB8, "comacmrus\t$x, $m">;
def CoMACMsu_xp_rnd    : CoXP<0x93, 0x59, "comacmsu\t$x, $m, rnd">;
def CoMACMsu_xp        : CoXP<0x93, 0x58, "comacmsu\t$x, $m">;
def CoMACMsuN_xp       : CoXP<0x93, 0x68, "comacmsu-\t$x, $m">;
def CoMACMu_xp_rnd     : CoXP<0x93, 0x19, "comacmu\t$x, $m, rnd">;
def CoMACMu_xp         : CoXP<0x93, 0x18, "comacmu\t$x, $m">;
def CoMACMuN_xp        : CoXP<0x93, 0x28, "comacmu-\t$x, $m">;
def CoMACMus_xp_rnd    : CoXP<0x93, 0x99, "comacmus\t$x, $m, rnd">;
def CoMACMus_xp        : CoXP<0x93, 0x98, "comacmus\t$x, $m">;
def CoMACMusN_xp       : CoXP<0x93, 0xA8, "comacmus-\t$x, $m">;
def CoMACR_rr_rnd      : CoRR<0xF1, "comacr\t$n, $m, rnd">;
def CoMACR_rp_rnd      : CoRP<0xF1, "comacr\t$n, $m, rnd">;
def CoMACR_xp_rnd      : CoXP<0x93, 0xF1, "comacr\t$x, $m, rnd">;
def CoMACR_rr          : CoRR<0xF0, "comacr\t$n, $m">;
def CoMACR_rp          : CoRP<0xF0, "comacr\t$n, $m">;
def CoMACR_xp          : CoXP<0x93, 0xF0, "comacr\t$x, $m">;
def CoMACRsu_rr_rnd    : CoRR<0x71, "comacrsu\t$n, $m, rnd">;
def CoMACRsu_rp_rnd    : CoRP<0x71, "comacrsu\t$n, $m, rnd">;
def CoMACRsu_xp_rnd    : CoXP<0x93, 0x71, "comacrsu\t$x, $m, rnd">;
def CoMACRsu_rr        : CoRR<0x70, "comacrsu\t$n, $m">;
def CoMACRsu_rp        : CoRP<0x70, "comacrsu\t$n, $m">;
def CoMACRsu_xp        : CoXP<0x93, 0x70, "comacrsu\t$x, $m">;
def CoMACRu_rr_rnd     : CoRR<0x31, "comacru\t$n, $m, rnd">;
def CoMACRu_rp_rnd     : CoRP<0x31, "comacru\t$n, $m, rnd">;
def CoMACRu_xp_rnd     : CoXP<0x93, 0x31, "comacru\t$x, $m, rnd">;
def CoMACRu_rr         : CoRR<0x30, "comacru\t$n, $m">;
def CoMACRu_rp         : CoRP<0x30, "comacru\t$n, $m">;
def CoMACRu_xp         : CoXP<0x93, 0x30, "comacru\t$x, $m">;
def CoMACRus_rr_rnd    : CoRR<0xB1, "comacrus\t$n, $m, rnd">;
def CoMACRus_rp_rnd    : CoRP<0xB1, "comacrus\t$n, $m, rnd">;
def CoMACRus_xp_rnd    : CoXP<0x93, 0xB1, "comacrus\t$x, $m, rnd">;
def CoMACRus_rr        : CoRR<0xB0, "comacrus\t$n, $m">;
def CoMACRus_rp        : CoRP<0xB0, "comacrus\t$n, $m">;
def CoMACRus_xp        : CoXP<0x93, 0xB0, "comacrus\t$x, $m">;
def CoMACsu_rr_rnd     : CoRR<0x51, "comacsu\t$n, $m, rnd">;
def CoMACsu_rp_rnd     : CoRP<0x51, "comacsu\t$n, $m, rnd">;
def CoMACsu_xp_rnd     : CoXP<0x93, 0x51, "comacsu\t$x, $m, rnd">;
def CoMACsu_rr         : CoRR<0x50, "comacsu\t$n, $m">;
def CoMACsu_rp         : CoRP<0x50, "comacsu\t$n, $m">;
def CoMACsu_xp         : CoXP<0x93, 0x50, "comacsu\t$x, $m">;
def CoMACsuN_rr        : CoRR<0x60, "comacsu-\t$n, $m">;
def CoMACsuN_rp        : CoRP<0x60, "comacsu-\t$n, $m">;
def CoMACsuN_xp        : CoXP<0x93, 0x60, "comacsu-\t$x, $m">;
def CoMACu_rr_rnd      : CoRR<0x11, "comacu\t$n, $m, rnd">;
def CoMACu_rp_rnd      : CoRP<0x11, "comacu\t$n, $m, rnd">;
def CoMACu_xp_rnd      : CoXP<0x93, 0x11, "comacu\t$x, $m, rnd">;
def CoMACu_rr          : CoRR<0x10, "comacu\t$n, $m">;
def CoMACu_rp          : CoRP<0x10, "comacu\t$n, $m">;
def CoMACu_xp          : CoXP<0x93, 0x10, "comacu\t$x, $m">;
def CoMACuN_rr         : CoRR<0x20, "comacu-\t$n, $m">;
def CoMACuN_rp         : CoRP<0x20, "comacu-\t$n, $m">;
def CoMACuN_xp         : CoXP<0x93, 0x20, "comacu-\t$x, $m">;
def CoMACus_rr_rnd     : CoRR<0x91, "comacus\t$n, $m, rnd">;
def CoMACus_rp_rnd     : CoRP<0x91, "comacus\t$n, $m, rnd">;
def CoMACus_xp_rnd     : CoXP<0x93, 0x91, "comacus\t$x, $m, rnd">;
def CoMACus_rr         : CoRR<0x90, "comacus\t$n, $m">;
def CoMACus_rp         : CoRP<0x90, "comacus\t$n, $m">;
def CoMACus_xp         : CoXP<0x93, 0x90, "comacus\t$x, $m">;
def CoMACusN_rr        : CoRR<0xA0, "comacus-\t$n, $m">;
def CoMACusN_rp        : CoRP<0xA0, "comacus-\t$n, $m">;
def CoMACusN_xp        : CoXP<0x93, 0xA0, "comacus-\t$x, $m">;
def CoMAX_rr           : CoRR<0x3A, "comax\t$n, $m">;
def CoMAX_rp           : CoRP<0x3A, "comax\t$n, $m">;
def CoMAX_xp           : CoXP<0x93, 0x3A, "comax\t$x, $m">;
def CoMIN_rr           : CoRR<0x7A, "comin\t$n, $m">;
def CoMIN_rp           : CoRP<0x7A, "comin\t$n, $m">;
def CoMIN_xp           : CoXP<0x93, 0x7A, "comin\t$x, $m">;
def CoMOV_xp           : CoXP<0xD3, 0x00, "comov\t$x, $m">;
def CoMUL_rr_rnd       : CoRR<0xC1, "comul\t$n, $m, rnd">;
def CoMUL_rp_rnd       : CoRP<0xC1, "comul\t$n, $m, rnd">;
def CoMUL_xp_rnd       : CoXP<0x93, 0xC1, "comul\t$x, $m, rnd">;
def CoMUL_rr           : CoRR<0xC0, "comul\t$n, $m">;
def CoMUL_rp           : CoRP<0xC0, "comul\t$n, $m">;
def CoMUL_xp           : CoXP<0x93, 0xC0, "comul\t$x, $m">;
def CoMULN_rr          : CoRR<0xC8, "comul-\t$n, $m">;
def CoMULN_rp          : CoRP<0xC8, "comul-\t$n, $m">;
def CoMULN_xp          : CoXP<0x93, 0xC8, "comul-\t$x, $m">;
def CoMULsu_rr_rnd     : CoRR<0x41, "comulsu\t$n, $m, rnd">;
def CoMULsu_rp_rnd     : CoRP<0x41, "comulsu\t$n, $m, rnd">;
def CoMULsu_xp_rnd     : CoXP<0x93, 0x41, "comulsu\t$x, $m, rnd">;
def CoMULsu_rr         : CoRR<0x40, "comulsu\t$n, $m">;
def CoMULsu_rp         : CoRP<0x40, "comulsu\t$n, $m">;
def CoMULsu_xp         : CoXP<0x93, 0x40, "comulsu\t$x, $m">;
def CoMULsuN_rr        : CoRR<0x48, "comulsu-\t$n, $m">;
def CoMULsuN_rp        : CoRP<0x48, "comulsu-\t$n, $m">;
def CoMULsuN_xp        : CoXP<0x93, 0x48, "comulsu-\t$x, $m">;
def CoMULu_rr_rnd      : CoRR<0x01, "comulu\t$n, $m, rnd">;
def CoMULu_rp_rnd      : CoRP<0x01, "comulu\t$n, $m, rnd">;
def CoMULu_xp_rnd      : CoXP<0x93, 0x01, "comulu\t$x, $m, rnd">;
def CoMULu_rr          : CoRR<0x00, "comulu\t$n, $m">;
def CoMULu_rp          : CoRP<0x00, "comulu\t$n, $m">;
def CoMULu_xp          : CoXP<0x93, 0x00, "comulu\t$x, $m">;
def CoMULuN_rr         : CoRR<0x08, "comulu-\t$n, $m">;
def CoMULuN_rp         : CoRP<0x08, "comulu-\t$n, $m">;
def CoMULuN_xp         : CoXP<0x93, 0x08, "comulu-\t$x, $m">;
def CoMULus_rr_rnd     : CoRR<0x81, "comulus\t$n, $m, rnd">;
def CoMULus_rp_rnd     : CoRP<0x81, "comulus\t$n, $m, rnd">;
def CoMULus_xp_rnd     : CoXP<0x93, 0x81, "comulus\t$x, $m, rnd">;
def CoMULus_rr         : CoRR<0x80, "comulus\t$n, $m">;
def CoMULus_rp         : CoRP<0x80, "comulus\t$n, $m">;
def CoMULus_xp         : CoXP<0x93, 0x80, "comulus\t$x, $m">;
def CoMULusN_rr        : CoRR<0x88, "comulus-\t$n, $m">;
def CoMULusN_rp        : CoRP<0x88, "comulus-\t$n, $m">;
def CoMULusN_xp        : CoXP<0x93, 0x88, "comulus-\t$x, $m">;
def CoNEG_n            : CoN<0x32, "coneg">;
def CoNEG_n_rnd        : CoN<0x72, "coneg\trnd">;
def CoNOP_xp           : CoXP<0x93, 0x5A, "conop\t$x, $m">;
def CoNOP_x            : CoX<0x5A, "conop\t$x">;
def CoNOP_q            : CoQ<0x5A, "conop\t$m">;
def CoRND_n            : CoN<0xB2, "cornd">;
def CoSHL_i            : CoI<0x82, "coshl\t$s">;
def CoSHL_r            : CoR<0x8A, "coshl\t$n">;
def CoSHL_p            : CoP<0x8A, "coshl\t$m">;
def CoSHR_i            : CoI<0x92, "coshr\t$s">;
def CoSHR_r            : CoR<0x9A, "coshr\t$n">;
def CoSHR_p            : CoP<0x9A, "coshr\t$m">;
def CoSTORE_sr         : CoStoreR<"costore\t$n, $w">;
def CoSTORE_sp         : CoStoreP<"costore\t$n, $w">;
def CoSUB_rr           : CoRR<0x0A, "cosub\t$n, $m">;
def CoSUB_rp           : CoRP<0x0A, "cosub\t$n, $m">;
def CoSUB_xp           : CoXP<0x93, 0x0A, "cosub\t$x, $m">;
def CoSUB2_rr          : CoRR<0x4A, "cosub2\t$n, $m">;
def CoSUB2_rp          : CoRP<0x4A, "cosub2\t$n, $m">;
def CoSUB2_xp          : CoXP<0x93, 0x4A, "cosub2\t$x, $m">;
def CoSUB2R_rr         : CoRR<0x52, "cosub2r\t$n, $m">;
def CoSUB2R_rp         : CoRP<0x52, "cosub2r\t$n, $m">;
def CoSUB2R_xp         : CoXP<0x93, 0x52, "cosub2r\t$x, $m">;
def CoSUBR_rr          : CoRR<0x12, "cosubr\t$n, $m">;
def CoSUBR_rp          : CoRP<0x12, "cosubr\t$n, $m">;
def CoSUBR_xp          : CoXP<0x93, 0x12, "cosubr\t$x, $m">;
