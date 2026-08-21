; RUN: not llvm-mc -triple=c166 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

; A relative branch counts words, so it reaches 127 of them either way and no
; further.  Both checks only happen once the target is known, which is while
; the object is being written rather than while the text is being parsed.

; CHECK: [[@LINE+2]]:{{[0-9]+}}: error: branch target out of range
near_enough:
        jb      r5.3, near_enough + 300

; CHECK: [[@LINE+2]]:{{[0-9]+}}: error: branch target must be 2-byte aligned
odd:
        jnb     r5.3, odd + 1
