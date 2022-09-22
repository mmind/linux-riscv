/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm-generic/export.h>

/* int strncmp(const char *cs, const char *ct, size_t count) */
SYM_FUNC_START(strncmp)
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
SYM_FUNC_END(strncmp)
