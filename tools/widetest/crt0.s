@ GBA DS widescreen test ROM: start-up (ARM). The header (0x04-0xBF) is
@ filled in by build.py.
	.section .crt0, "ax"
	.arm
	.global _start
_start:
	b	reset
	.fill	188, 1, 0
reset:
	ldr	sp, =0x03007F00
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	mov	r2, #0
1:	cmp	r0, r1
	strlo	r2, [r0], #4
	blo	1b
	ldr	r0, =main
	mov	lr, pc
	bx	r0
2:	b	2b
	.ltorg
