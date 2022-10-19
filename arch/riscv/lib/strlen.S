/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm-generic/export.h>
#include <asm/alternative-macros.h>
#include <asm/errata_list.h>

/* int strlen(const char *s) */
SYM_FUNC_START(strlen)

	ALTERNATIVE("nop", "j variant_zbb", 0, CPUFEATURE_ZBB, CONFIG_RISCV_ISA_ZBB)

	/*
	 * Returns
	 *   a0 - string length
	 *
	 * Parameters
	 *   a0 - String to measure
	 *
	 * Clobbers:
	 *   t0, t1
	 */
	mv	t1, a0
1:
	lbu	t0, 0(t1)
	bnez	t0, 2f
	sub	a0, t1, a0
	ret
2:
	addi	t1, t1, 1
	j	1b

/*
 * Variant of strlen using the ZBB extension if available
 */
#ifdef CONFIG_RISCV_ISA_ZBB
variant_zbb:

#define src		a0
#define result		a0
#define addr		t0
#define data		t1
#define offset		t2
#define offset_bits	t2
#define valid_bytes	t3
#define m1		t3

#ifdef CONFIG_CPU_BIG_ENDIAN
# define CZ	clz
# define SHIFT	sll
#else
# define CZ	ctz
# define SHIFT	srl
#endif

.option push
.option arch,+zbb

	/*
	 * Returns
	 *   a0 - string length
	 *
	 * Parameters
	 *   a0 - String to measure
	 *
	 * Clobbers
	 *   t0, t1, t2, t3
	 */

	/* Number of irrelevant bytes in the first word. */
	andi	offset, src, SZREG-1

	/* Align pointer. */
	andi	addr, src, -SZREG

	li	valid_bytes, SZREG
	sub	valid_bytes, valid_bytes, offset
	slli	offset_bits, offset, RISCV_LGPTR

	/* Get the first word.  */
	REG_L	data, 0(addr)

	/*
	 * Shift away the partial data we loaded to remove the irrelevant bytes
	 * preceding the string with the effect of adding NUL bytes at the
	 * end of the string.
	 */
	SHIFT	data, data, offset_bits

	/* Convert non-NUL into 0xff and NUL into 0x00. */
	orc.b	data, data

	/* Convert non-NUL into 0x00 and NUL into 0xff. */
	not	data, data

	/*
	 * Search for the first set bit (corresponding to a NUL byte in the
	 * original chunk).
	 */
	CZ	data, data

	/*
	 * The first chunk is special: commpare against the number
	 * of valid bytes in this chunk.
	 */
	srli	result, data, 3
	bgtu	valid_bytes, result, 3f

	/* Prepare for the word comparison loop. */
	addi	offset, addr, SZREG
	li	m1, -1

	/*
	 * Our critical loop is 4 instructions and processes data in
	 * 4 byte or 8 byte chunks.
	 */
	.p2align 3
1:
	REG_L	data, SZREG(addr)
	addi	addr, addr, SZREG
	orc.b	data, data
	beq	data, m1, 1b
2:
	not	data, data
	CZ	data, data

	/* Get number of processed words.  */
	sub	offset, addr, offset

	/* Add number of characters in the first word.  */
	add	result, result, offset
	srli	data, data, 3

	/* Add number of characters in the last word.  */
	add	result, result, data
3:
	ret

.option pop
#endif
SYM_FUNC_END(strlen)
