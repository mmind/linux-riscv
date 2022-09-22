/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm-generic/export.h>

/* int strcmp(const char *cs, const char *ct) */
SYM_FUNC_START(strcmp)
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
SYM_FUNC_END(strcmp)
