_Head:
	/* Set _Entry address */
	adr	x1, _Payload

	/* Jump to the real code */
	b	_Start

	/* Fake Linux kernel header */

_StackBase0: //has to be 0 on some devices
	/* Text Offset */
	.quad 0

_StackSize0:
	/* Image Size */
	.quad 0

	/* Flags */
	.quad 0

	/* Reserved */
	.quad 0

	/* Reserved */
	.quad 0

	/* Reserved */
	.quad 0

	/* ARM64 Magic */
	.ascii "ARM\x64"

	/* Reserved */
	.long 0

	.ascii "EDK2-MTK"

_StackBase:
	/* Text Offset */
	.quad UEFI_BASE

_StackSize:
	/* Image Size */
	.quad UEFI_SIZE

_Start:
#ifdef T1101_FB_TEST
	/* Early framebuffer proof for T1101 using the live LK fb_base. */
	movz	x7, #0xa000
	movk	x7, #0xfe41, lsl #16  /* 0xfe41a000 */
	movz	x8, #0xc000
	movk	x8, #0x1a, lsl #16    /* 0x001ac000 = 0x01ac0000 / 16 */
	mov	w9, #0x00FF0000       /* red */
	mov	w10, w9
.Lfb_red:
	stp	w9, w10, [x7], #16
	subs	x8, x8, #1
	b.ne	.Lfb_red

	/* Bright green 1200x240 block in the top-left corner. */
	movz	x7, #0xa000
	movk	x7, #0xfe41, lsl #16  /* 0xfe41a000 */
	mov	x11, #240
	mov	x12, #600             /* 1200 pixels / 2 */
	mov	w9, #0x0000FF00       /* green */
	mov	w10, w9
.Lfb_row:
	mov	x13, x12
.Lfb_col:
	stp	w9, w10, [x7], #16
	subs	x13, x13, #1
	b.ne	.Lfb_col
	subs	x11, x11, #1
	b.ne	.Lfb_row

	/* Push the painted pixels out to DRAM so the scanout engine sees them. */
	movz	x7, #0xa000
	movk	x7, #0xfe41, lsl #16  /* 0xfe41a000 */
	movz	x8, #0xb000
	movk	x8, #0x6, lsl #16     /* 0x0006b000 cache lines (64 B each) */
.Lfb_clean:
	dc	civac, x7
	add	x7, x7, #64
	subs	x8, x8, #1
	b.ne	.Lfb_clean
	dsb	sy
	isb	sy

	/* Give the pattern a moment before EDK2 takes over the console. */
	movz	x14, #0
	movk	x14, #0x400, lsl #16  /* 0x04000000 */
.Lfb_delay:
	subs	x14, x14, #1
	b.ne	.Lfb_delay
#endif
	mov	x4, x1
	ldr	x5, _StackBase
	cmp	x4, x5
	beq	_Entry
	ldr	x6,	_StackSize

_CopyLoop:
	ldp	x2, x3, [x4], #16
	stp	x2, x3, [x5], #16
	subs	x6, x6, #16
	b.ne	_CopyLoop
	ldr	x5, _StackBase

_Entry:
	br	x5

_Dead:
	/* We should never get here */
	b	_Dead

.text
.align 4

_Payload:
	/* Your code will get ran right after this binary */
