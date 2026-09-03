## A program that never loads R0.  It comes out of reset at zero, which is
## below any limit, and that is not an overflow - it is a program that does not
## use the ABI stack at all.  The check arms itself the first time R0 is seen
## above the limit, so this one runs to the end.

	.macro	emit ch
	movb	rl2, #\ch
	exts	#0xFF, #1
	movb	[r4], rl2
	.endm

	.text
	.globl _start
_start:
	mov	r4, #0x0000
	emit	'u'
	emit	'n'
	emit	's'
	emit	'e'
	emit	't'
	emit	10
## R2 is where the ABI returns a word, and it is what the simulator reports
## as the exit status, so a program that finishes says so.
	mov	r2, #0
	jmpa	cc_UC, __c166_exit
