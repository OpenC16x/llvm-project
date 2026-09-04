## Set the ABI stack pointer just above the limit and then step it below,
## which is the smallest overflow there is.
##
## The console is a byte written to FF0000H, and the characters go out one at a
## time rather than from a string, as in console.s next door: this program is
## linked with -Ttext and nothing else, so there is no data page pointer
## arrangement that would make a near load reach its own .rodata.

	.macro	emit ch
	movb	rl2, #\ch
	exts	#0xFF, #1
	movb	[r4], rl2
	.endm

	.text
	.globl _start
_start:
	mov	r0, #0xF602
	mov	r4, #0x0000
	emit	'p'
	emit	'a'
	emit	's'
	emit	't'
	emit	10
## F602H less one word is F600H, the limit itself, which is still inside
## the stack; the word below that is not.
	sub	r0, #2
	sub	r0, #2
## R2 is where the ABI returns a word, and it is what the simulator reports
## as the exit status, so a program that finishes says so.
	mov	r2, #0
	jmpa	cc_UC, __c166_exit
