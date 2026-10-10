/*
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1995 - 1999 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Modified for DECStation and hence R3000 support by Paul M. Antoine
 * Further modifications by David S. Miller and Harald Koerfgen
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */

#include <asm/asm.h>
#include <asm/regdef.h>

	.set noreorder
	.cprestore
	LEAF(start)
start:
	/* Preserved header opcodes for kjump.ko validation */
	lui	t0, 0xA006
	ori	t0, t0, 0xC104

	/* Milestone 0x22222222: head.S entered! */
	lui	t0, 0xA006
	ori	t0, t0, 0x1938
	li	t1, 0x22222222
	sw	t1, 0(t0)
	sync

	/* Save boot rom start args */
	move	s0, a0
	move	s1, a1
	move	s2, a2
	move	s3, a3

	/* Complete 4-way L1 I-cache and D-cache Flush & Invalidate before cached operations */
	mtc0	zero, $28		/* CP0 TagLo = 0 */
	mtc0	zero, $29		/* CP0 TagHi = 0 */
	ehb
	li	t0, 0x80000000
	li	t1, 256			/* 256 sets x 32 bytes = 8KB per way */
flush_all_l1_caches:
	/* Invalidate all 4 ways of Instruction Cache (32KB total) */
	cache	0x08, 0x0000(t0)	/* Way 0 Index Store Tag I */
	cache	0x08, 0x2000(t0)	/* Way 1 Index Store Tag I */
	cache	0x08, 0x4000(t0)	/* Way 2 Index Store Tag I */
	cache	0x08, 0x6000(t0)	/* Way 3 Index Store Tag I */

	/* Writeback & Invalidate all 4 ways of Data Cache (32KB total) */
	cache	0x09, 0x0000(t0)	/* Way 0 Index Writeback Invalidate D */
	cache	0x09, 0x2000(t0)	/* Way 1 Index Writeback Invalidate D */
	cache	0x09, 0x4000(t0)	/* Way 2 Index Writeback Invalidate D */
	cache	0x09, 0x6000(t0)	/* Way 3 Index Writeback Invalidate D */

	addiu	t0, t0, 32
	addiu	t1, t1, -1
	bnez	t1, flush_all_l1_caches
	 nop
	sync
	ehb

	/* Milestone 0x25252525: before BSS clear */
	lui	t0, 0xA006
	ori	t0, t0, 0x1938
	li	t1, 0x25252525
	sw	t1, 0(t0)
	sync

	/* Clear BSS */
	PTR_LA	a0, _edata
	PTR_LA	a2, _end
1:	sw	zero, 0(a0)
	bne	a2, a0, 1b
	 addiu	a0, a0, 4

	/* Milestone 0x33333333: BSS cleared, setting stack & heap */
	lui	t0, 0xA006
	ori	t0, t0, 0x1938
	li	t1, 0x33333333
	sw	t1, 0(t0)
	sync

	PTR_LA	a0, (.heap)	     /* heap address */
	PTR_LA	sp, (.stack + 8192)  /* stack address */

	/* Milestone 0x34343434: calling decompress_kernel */
	li	t1, 0x34343434
	sw	t1, 0(t0)
	sync

	PTR_LA	ra, 2f
	PTR_LA	k0, decompress_kernel
	jr	k0
	 nop
2:
	/* Save DTB pointer returned in v0 from decompress_kernel */
	move	s4, v0

	/* Milestone 0x77777777: returned from decompress_kernel */
	lui	t0, 0xA006
	ori	t0, t0, 0x1938
	li	t1, 0x77777777
	sw	t1, 0(t0)
	sync

	/* 1. Flush dirty D-cache lines to DDR2 DRAM across decompressed kernel range (Hit_Writeback_Inv_D 0x15) */
	li	t0, 0x84000000
	li	t1, 0x84800000
flush_dhit:
	cache	0x15, 0(t0)
	addiu	t0, t0, 32
	bne	t0, t1, flush_dhit
	 nop
	sync
	ehb

	/* 2. Invalidate I-cache lines across decompressed kernel range (Hit_Invalidate_I 0x10) */
	li	t0, 0x84000000
	li	t1, 0x84800000
flush_ihit:
	cache	0x10, 0(t0)
	addiu	t0, t0, 32
	bne	t0, t1, flush_ihit
	 nop
	sync
	ehb

	/* 3. Complete Index D-cache Flush & Invalidate (all 4 ways, both waybit and low-bit encodings) */
	mtc0	zero, $28		/* CP0 TagLo = 0 */
	mtc0	zero, $29		/* CP0 TagHi = 0 */
	ehb
	li	t0, 0x80000000
	li	t1, 256
flush_dindex:
	cache	0x09, 0(t0)
	cache	0x09, 1(t0)
	cache	0x09, 2(t0)
	cache	0x09, 3(t0)
	cache	0x09, 0x0000(t0)
	cache	0x09, 0x2000(t0)
	cache	0x09, 0x4000(t0)
	cache	0x09, 0x6000(t0)
	addiu	t0, t0, 32
	addiu	t1, t1, -1
	bnez	t1, flush_dindex
	 nop
	sync
	ehb

	/* 4. Complete Index I-cache Invalidate (all 4 ways) */
	mtc0	zero, $28		/* CP0 TagLo = 0 */
	mtc0	zero, $29		/* CP0 TagHi = 0 */
	ehb
	li	t0, 0x80000000
	li	t1, 256
flush_iindex:
	cache	0x00, 0(t0)
	cache	0x00, 1(t0)
	cache	0x00, 2(t0)
	cache	0x00, 3(t0)
	cache	0x08, 0x0000(t0)
	cache	0x08, 0x2000(t0)
	cache	0x08, 0x4000(t0)
	cache	0x08, 0x6000(t0)
	addiu	t0, t0, 32
	addiu	t1, t1, -1
	bnez	t1, flush_iindex
	 nop
	sync
	ehb

	/* Setup kernel arguments: DTB in a1, a0 = -2 if DTB present */
	bnez	s4, pass_dtb
	 nop
	move	a0, s0
	move	a1, s1
	move	a2, s2
	move	a3, s3
	b	jump_kernel
	 nop

pass_dtb:
	li	a0, -2			/* Signals appended DTB to kernel_entry */
	move	a1, s4			/* Pointer to DTB */
	move	a2, zero
	move	a3, zero

jump_kernel:
	/* Milestone 0x7A7A7A7A: Jumping to KERNEL_ENTRY (0x843ABD00) */
	lui	t0, 0xA006
	ori	t0, t0, 0x1938
	li	t1, 0x7A7A7A7A
	sw	t1, 0(t0)
	sync

	PTR_LI	k0, KERNEL_ENTRY
	jr	k0
	 nop
3:
	b	3b
	 nop
	END(start)

	.comm .heap,BOOT_HEAP_SIZE,4
	.comm .stack,4096*2,4
