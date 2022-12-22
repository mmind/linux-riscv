/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm-generic/export.h>
#include <asm/alternative-macros.h>
#include <asm/errata_list.h>

/* int strcmp(const char *cs, const char *ct) */
SYM_FUNC_START(strcmp)

	ALTERNATIVE_2("nop",
		      "j strcmp_zbb_unaligned", 0, 
			CPUFEATURE_ZBB | CPUFEATURE_FAST_UNALIGNED, 0,
			CONFIG_RISCV_ISA_ZBB,
		      "j strcmp_zbb", 0,
			CPUFEATURE_ZBB, CPUFEATURE_FAST_UNALIGNED,
			CONFIG_RISCV_ISA_ZBB)

	/*
	 * Returns
	 *   a0 - comparison result, value like strcmp
	 *
	 * Parameters
	 *   a0 - string1
	 *   a1 - string2
	 *
	 * Clobbers
	 *   t0, t1
	 */
1:
	lbu	t0, 0(a0)
	lbu	t1, 0(a1)
	addi	a0, a0, 1
	addi	a1, a1, 1
	bne	t0, t1, 2f
	bnez	t0, 1b
	li	a0, 0
	ret
2:
	/*
	 * strcmp only needs to return (< 0, 0, > 0) values
	 * not necessarily -1, 0, +1
	 */
	sub	a0, t0, t1
	ret

/*
 * Variant of strcmp using the ZBB extension if available
 */
#ifdef CONFIG_RISCV_ISA_ZBB
strcmp_zbb:

.option push
.option arch,+zbb

	/*
	 * Returns
	 *   a0 - comparison result, value like strcmp
	 *
	 * Parameters
	 *   a0 - string1
	 *   a1 - string2
	 *
	 * Clobbers
	 *   t0, t1, t2, t3, t4, t5
	 */

	or	t2, a0, a1
	li	t4, -1
	and	t2, t2, SZREG-1
	bnez	t2, 3f

	/* Main loop for aligned string.  */
	.p2align 3
1:
	REG_L	t0, 0(a0)
	REG_L	t1, 0(a1)
	orc.b	t3, t0
	bne	t3, t4, 2f
	addi	a0, a0, SZREG
	addi	a1, a1, SZREG
	beq	t0, t1, 1b

	/*
	 * Words don't match, and no null byte in the first
	 * word. Get bytes in big-endian order and compare.
	 */
#ifndef CONFIG_CPU_BIG_ENDIAN
	rev8	t0, t0
	rev8	t1, t1
#endif

	/* Synthesize (t0 >= t1) ? 1 : -1 in a branchless sequence. */
	sltu	a0, t0, t1
	neg	a0, a0
	ori	a0, a0, 1
	ret

2:
	/*
	 * Found a null byte.
	 * If words don't match, fall back to simple loop.
	 */
	bne	t0, t1, 3f

	/* Otherwise, strings are equal. */
	li	a0, 0
	ret

	/* Simple loop for misaligned strings. */
	.p2align 3
3:
	lbu	t0, 0(a0)
	lbu	t1, 0(a1)
	addi	a0, a0, 1
	addi	a1, a1, 1
	bne	t0, t1, 4f
	bnez	t0, 3b

4:
	sub	a0, t0, t1
	ret

strcmp_zbb_unaligned:

	/*
	 * Returns
	 *   a0 - comparison result, value like strcmp
	 *
	 * Parameters
	 *   a0 - string1
	 *   a1 - string2
	 *
	 * Clobbers
	 *   a3, a4, a5, a6, a7, t0, t1, t2, t3, t4, t5
	 */

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# error big endian is untested!
# define CZ	ctz
# define SHIFT	srl
# define SHIFT2	sll
#else
# define CZ	ctz
# define SHIFT	sll
# define SHIFT2	srl
#endif

	/* a3...delta from a0 to a1.  */
	sub	a3, a1, a0
	li	a4, -1
	andi	a7, a3, SZREG-1
	andi	a5, a0, SZREG-1
	bnez	a7, 7f
	bnez	a5, 6f

	.p2align 4
1:
	REG_L	t0, 0(a0)
	add	a7, a0, a3
	addi	a0, a0, SZREG
	REG_L	t1, 0(a7)

2:
	orc.b	t3, t0
	bne	t3, a4, 4f
	beq	t0, t1, 1b

	/* Words don't match, and no NUL byte in one word.
	   Get bytes in big-endian order and compare as words.  */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	rev8	t0, t0
	rev8	t1, t1
#endif
	/* Synthesize (t0 >= t1) ? 1 : -1 in a branchless sequence.  */
	sltu	a0, t0, t1
	neg	a0, a0
	ori	a0, a0, 1
	ret

3:
	orc.b	t3, t0
4:
	/* Words don't match or NUL byte in at least one word.
	   t3 holds orc.b value of t0.  */
	xor	a7, t0, t1
	orc.b	a7, a7

	orn	a7, a7, t3
	CZ	t5, a7

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	rev8	t0, t0
	rev8	t1, t1
#endif
	sll	t0, t0, t5
	sll	t1, t1, t5
	srl	t0, t0, SZREG*8-8
	srl	t1, t1, SZREG*8-8

5:
	sub	a0, t0, t1
	ret

	.p2align 4
6:
	/* Sources are mutually aligned, but are not currently at an
	   alignment boundary.  Round down the addresses and then mask a3
	   the bytes that precede the start point.  */
	andi	a0, a0, -SZREG
	add	a7, a0, a3
	REG_L	t0, 0(a0)
	addi	a0, a0, SZREG
	REG_L	t1, 0(a7)
	/* Get number of bits to mask.  */
	sll	t5, a1, 3
	/* Bits to mask are now 0, others are 1.  */
	SHIFT	a7, a4, t5
	/* Or with inverted value -> masked bits become 1.  */
	orn	t0, t0, a7
	orn	t1, t1, a7
	j	2b

7:
	/* Skip slow loop if a0 is aligned.  */
	beqz	a5, 9f
8:
	/* Align a0 to 8 bytes.  */
	lbu	t0, 0(a0)
	lbu	t1, 0(a1)
	beqz	t0, 5b
	bne	t0, t1, 5b
	addi	a0, a0, 1
	addi	a1, a1, 1
	andi	a5, a0, SZREG-1
	bnez	a5, 8b

9:
	/* a0 is aligned. Align a1 down and check for NUL there.
	 * If there is no NUL, we may read the next word from a1.
	 * If there is a NUL, we must not read a complete word from a1
	 * because we might cross a page boundary.  */
	/* Get number of bits to mask (upper bits are ignored by shifts).  */
	sll	t5, a1, 3
	/* a6 := align_down (a1)  */
	andi	a6, a1, -SZREG
	REG_L   t2, 0(a6)
	addi	a6, a6, SZREG

	/* Bits to mask are now 0, others are 1.  */
	SHIFT	a7, a4, t5
	/* Or with inverted value -> masked bits become 1.  */
	orn	t4, t2, a7
	/* Check for NUL in next aligned word.  */
	orc.b	t4, t4
	bne	t4, a4, 11f

	.p2align 4
10:
	/* Read the (aligned) t0 and the unaligned t1.  */
	REG_L	t0, 0(a0)
	addi	a0, a0, SZREG
	REG_L	t1, 0(a1)
	addi	a1, a1, SZREG
	orc.b	t3, t0
	bne	t3, a4, 4b
	bne	t0, t1, 4b

	/* Read the next aligned-down word.  */
	REG_L	t2, 0(a6)
	addi	a6, a6, SZREG
	orc.b	t4, t2
	beq	t4, a4, 10b

11:
	/* a0 points to unread word (only first bytes relevant).
	 * t2 holds next aligned-down word with NUL.
	 * Compare the first bytes of t0 with the last bytes of t2.  */
	REG_L	t0, 0(a0)
	/* Shift NUL bytes into t2 to become t1.  */
	SHIFT2	t1, t2, t5
	bne	t0, t1, 3b
	li	a0, 0
	ret

.option pop
#endif
SYM_FUNC_END(strcmp)
