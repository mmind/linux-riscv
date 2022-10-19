/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm-generic/export.h>
#include <asm/alternative-macros.h>
#include <asm/errata_list.h>

/* int strncmp(const char *cs, const char *ct, size_t count) */
SYM_FUNC_START(strncmp)

	ALTERNATIVE("nop", "j variant_zbb", 0, CPUFEATURE_ZBB, CONFIG_RISCV_ISA_ZBB)

	/*
	 * Returns
	 *   a0 - comparison result, value like strncmp
	 *
	 * Parameters
	 *   a0 - string1
	 *   a1 - string2
	 *   a2 - number of characters to compare
	 *
	 * Clobbers
	 *   t0, t1, t2
	 */
	li	t0, 0
1:
	beq	a2, t0, 4f
	add	t1, a0, t0
	add	t2, a1, t0
	lbu	t1, 0(t1)
	lbu	t2, 0(t2)
	beq	t1, t2, 3f
	li	a0, 1
	bgeu	t1, t2, 2f
	li	a0, -1
2:
	ret
3:
	addi	t0, t0, 1
	bnez	t1, 1b
4:
	li	a0, 0
	j	2b

/*
 * Variant of strncmp using the ZBB extension if available
 */
#ifdef CONFIG_RISCV_ISA_ZBB
variant_zbb:

#define src1		a0
#define result		a0
#define src2		t6
#define len		a2
#define data1		t0
#define data2		t1
#define align		t2
#define data1_orcb	t3
#define limit		t4
#define m1		t5

.option push
.option arch,+zbb

	/*
	 * Returns
	 *   a0 - comparison result, like strncmp
	 *
	 * Parameters
	 *   a0 - string1
	 *   a1 - string2
	 *   a2 - number of characters to compare
	 *
	 * Clobbers
	 *   t0, t1, t2, t3, t4, t5, t6
	 */
	mv	src2, a1

	or	align, src1, src2
	li	m1, -1
	and	align, align, SZREG-1
	add	limit, src1, len
	bnez	align, 4f

	/* Adjust limit for fast-path.  */
	addi	limit, limit, -SZREG

	/* Main loop for aligned string.  */
	.p2align 3
1:
	bgt	src1, limit, 3f
	REG_L	data1, 0(src1)
	REG_L	data2, 0(src2)
	orc.b	data1_orcb, data1
	bne	data1_orcb, m1, 2f
	addi	src1, src1, SZREG
	addi	src2, src2, SZREG
	beq	data1, data2, 1b

	/*
	 * Words don't match, and no null byte in the first
	 * word. Get bytes in big-endian order and compare.
	 */
#ifndef CONFIG_CPU_BIG_ENDIAN
	rev8	data1, data1
	rev8	data2, data2
#endif

	/* Synthesize (data1 >= data2) ? 1 : -1 in a branchless sequence.  */
	sltu	result, data1, data2
	neg	result, result
	ori	result, result, 1
	ret

2:
	/*
	 * Found a null byte.
	 * If words don't match, fall back to simple loop.
	 */
	bne	data1, data2, 3f

	/* Otherwise, strings are equal.  */
	li	result, 0
	ret

	/* Simple loop for misaligned strings.  */
3:
	/* Restore limit for slow-path.  */
	addi	limit, limit, SZREG
	.p2align 3
4:
	bge	src1, limit, 6f
	lbu	data1, 0(src1)
	lbu	data2, 0(src2)
	addi	src1, src1, 1
	addi	src2, src2, 1
	bne	data1, data2, 5f
	bnez	data1, 4b

5:
	sub	result, data1, data2
	ret

6:
	li	result, 0
	ret

.option pop
#endif
SYM_FUNC_END(strncmp)
