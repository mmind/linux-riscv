/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PGTABLE_64_H
#define _ASM_RISCV_PGTABLE_64_H

#include <linux/const.h>
#include <asm/errata_list.h>

#define PGDIR_SHIFT     30
/* Size of region mapped by a page global directory */
#define PGDIR_SIZE      (_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE - 1))

#define PMD_SHIFT       21
/* Size of region mapped by a page middle directory */
#define PMD_SIZE        (_AC(1, UL) << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE - 1))

/* Page Middle Directory entry */
typedef struct {
	unsigned long pmd;
} pmd_t;

#define pmd_val(x)      ((x).pmd)
#define __pmd(x)        ((pmd_t) { (x) })

#define PTRS_PER_PMD    (PAGE_SIZE / sizeof(pmd_t))

/*
 * rv64 PTE format:
 * | 63 | 62 61 | 60 54 | 53  10 | 9             8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *   N      MT     RSV    PFN      reserved for SW   D   A   G   U   X   W   R   V
 * [62:61] Memory Type definitions:
 *  00 - PMA    Normal Cacheable, No change to implied PMA memory type
 *  01 - NC     Non-cacheable, idempotent, weakly-ordered Main Memory
 *  10 - IO     Non-cacheable, non-idempotent, strongly-ordered I/O memory
 *  11 - Rsvd   Reserved for future standard use
 */
#define _PAGE_NOCACHE_SVPBMT	(1UL << 61)
#define _PAGE_IO_SVPBMT		(1UL << 62)
#define _PAGE_MTMASK_SVPBMT	(_PAGE_NOCACHE_SVPBMT | _PAGE_IO_SVPBMT)

/*
 * [63:59] T-Head Memory Type definitions:
 *
 * 00000 - NC   Weakly-ordered, Non-cacheable, Non-bufferable, Non-shareable, Non-trustable
 * 01110 - PMA  Weakly-ordered, Cacheable, Bufferable, Shareable, Non-trustable
 * 10000 - IO   Strongly-ordered, Non-cacheable, Non-bufferable, Non-shareable, Non-trustable
 */
#define _PAGE_PMA_THEAD		((1UL << 62) | (1UL << 61) | (1UL << 60))
#define _PAGE_NOCACHE_THEAD	0UL
#define _PAGE_IO_THEAD		(1UL << 63)
#define _PAGE_MTMASK_THEAD	(_PAGE_PMA_THEAD | _PAGE_IO_THEAD | (1UL << 59))

static inline u64 riscv_page_mtmask(void)
{
	u64 val;

	ALT_SVPBMT(val, _PAGE_MTMASK);
	return val;
}

static inline u64 riscv_page_nocache(void)
{
	u64 val;

	ALT_SVPBMT(val, _PAGE_NOCACHE);
	return val;
}

static inline u64 riscv_page_io(void)
{
	u64 val;

	ALT_SVPBMT(val, _PAGE_IO);
	return val;
}

#define _PAGE_NOCACHE		riscv_page_nocache()
#define _PAGE_IO		riscv_page_io()
#define _PAGE_MTMASK		riscv_page_mtmask()

/* Set of bits to preserve across pte_modify() */
#define _PAGE_CHG_MASK  (~(unsigned long)(_PAGE_PRESENT | _PAGE_READ |	\
					  _PAGE_WRITE | _PAGE_EXEC |	\
					  _PAGE_USER | _PAGE_GLOBAL |	\
					  _PAGE_MTMASK))

static inline int pud_present(pud_t pud)
{
	return (pud_val(pud) & _PAGE_PRESENT);
}

static inline int pud_none(pud_t pud)
{
	return (pud_val(pud) == 0);
}

static inline int pud_bad(pud_t pud)
{
	return !pud_present(pud);
}

#define pud_leaf	pud_leaf
static inline int pud_leaf(pud_t pud)
{
	return pud_present(pud) && (pud_val(pud) & _PAGE_LEAF);
}

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	*pudp = pud;
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

static inline pmd_t *pud_pgtable(pud_t pud)
{
	return (pmd_t *)pfn_to_virt((pud_val(pud) & _PAGE_CHG_MASK) >> _PAGE_PFN_SHIFT);
}

static inline struct page *pud_page(pud_t pud)
{
	return pfn_to_page((pud_val(pud) & _PAGE_CHG_MASK) >> _PAGE_PFN_SHIFT);
}

static inline pmd_t pfn_pmd(unsigned long pfn, pgprot_t prot)
{
	unsigned long prot_val = pgprot_val(prot);

	ALT_THEAD_PMA(prot_val);

	return __pmd((pfn << _PAGE_PFN_SHIFT) | prot_val);
}

static inline unsigned long _pmd_pfn(pmd_t pmd)
{
	return (pmd_val(pmd) & _PAGE_CHG_MASK) >> _PAGE_PFN_SHIFT;
}

#define mk_pmd(page, prot)    pfn_pmd(page_to_pfn(page), prot)

#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))

#endif /* _ASM_RISCV_PGTABLE_64_H */
