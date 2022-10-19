/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm-generic/export.h>
#include <asm/alternative-macros.h>
#include <asm/errata_list.h>

/* int strcmp(const char *cs, const char *ct) */
SYM_FUNC_START(strcmp)

	ALTERNATIVE("nop", "j variant_zbb", 0, CPUFEATURE_ZBB, CONFIG_RISCV_ISA_ZBB)

	/*
	 * Returns
	 *   a0 - comparison result, value like strcmp
	 *
	 * Parameters
	 *   a0 - string1
	 *   a1 - string2
	 *
	 * Clobbers
	 *   t0, t1, t2
	 */
	mv	t2, a1
1:
	lbu	t1, 0(a0)
	lbu	t0, 0(a1)
	addi	a0, a0, 1
	addi	a1, a1, 1
	beq	t1, t0, 3f
	li	a0, 1
	bgeu	t1, t0, 2f
	li	a0, -1
2:
	mv	a1, t2
	ret
3:
	bnez	t1, 1b
	li	a0, 0
	j	2b

/*
 * Variant of strcmp using the ZBB extension if available
 */
#ifdef CONFIG_RISCV_ISA_ZBB
variant_zbb:
#define src1		a0
#define result		a0
#define src2		t5
#define data1		t0
#define data2		t1
#define align		t2
#define data1_orcb	t3
#define m1		t4

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
	mv	src2, a1

	or	align, src1, src2
	li	m1, -1
	and	align, align, SZREG-1
	bnez	align, 3f

	/* Main loop for aligned string.  */
	.p2align 3
1:
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

	/* Synthesize (data1 >= data2) ? 1 : -1 in a branchless sequence. */
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

	/* Otherwise, strings are equal. */
	li	result, 0
	ret

	/* Simple loop for misaligned strings. */
	.p2align 3
3:
	lbu	data1, 0(src1)
	lbu	data2, 0(src2)
	addi	src1, src1, 1
	addi	src2, src2, 1
	bne	data1, data2, 4f
	bnez	data1, 3b

4:
	sub	result, data1, data2
	ret

.option pop
#endif
SYM_FUNC_END(strcmp)
