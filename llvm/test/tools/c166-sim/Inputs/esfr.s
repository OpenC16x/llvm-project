## Written by address, read back through a "reg" field under an EXTR - which is
## what "push qx0" is, and the only way to push one of these at all.
	.text
	.globl _start
_start:
	mov	r4, #0x0000
	mov	r5, #0x7000
	mov	0xF000, r5
	mov	r5, #0x9000
	mov	0xF002, r5
	push	qx0
	push	qx1
	pop	r7
	pop	r6
	shr	r6, #12
	shr	r7, #12
	add	r6, #0x30
	add	r7, #0x30
	movb	rl2, rl6
	exts	#0xFF, #1
	movb	[r4], rl2
	movb	rl2, rl7
	exts	#0xFF, #1
	movb	[r4], rl2
	movb	rl2, #10
	exts	#0xFF, #1
	movb	[r4], rl2
	mov	r2, #0
	jmpa	cc_UC, __c166_exit
