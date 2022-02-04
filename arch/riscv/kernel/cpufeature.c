// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */

#include <linux/bitmap.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <asm/alternative.h>
#include <asm/errata_list.h>
#include <asm/hwcap.h>
#include <asm/patch.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/smp.h>
#include <asm/switch_to.h>

unsigned long elf_hwcap __read_mostly;

/* Host ISA bitmap */
static DECLARE_BITMAP(riscv_isa, RISCV_ISA_EXT_MAX) __read_mostly;

#ifdef CONFIG_FPU
__ro_after_init DEFINE_STATIC_KEY_FALSE(cpu_hwcap_fpu);
#endif

/**
 * riscv_isa_extension_base() - Get base extension word
 *
 * @isa_bitmap: ISA bitmap to use
 * Return: base extension word as unsigned long value
 *
 * NOTE: If isa_bitmap is NULL then Host ISA bitmap will be used.
 */
unsigned long riscv_isa_extension_base(const unsigned long *isa_bitmap)
{
	if (!isa_bitmap)
		return riscv_isa[0];
	return isa_bitmap[0];
}
EXPORT_SYMBOL_GPL(riscv_isa_extension_base);

/**
 * __riscv_isa_extension_available() - Check whether given extension
 * is available or not
 *
 * @isa_bitmap: ISA bitmap to use
 * @bit: bit position of the desired extension
 * Return: true or false
 *
 * NOTE: If isa_bitmap is NULL then Host ISA bitmap will be used.
 */
bool __riscv_isa_extension_available(const unsigned long *isa_bitmap, int bit)
{
	const unsigned long *bmap = (isa_bitmap) ? isa_bitmap : riscv_isa;

	if (bit >= RISCV_ISA_EXT_MAX)
		return false;

	return test_bit(bit, bmap) ? true : false;
}
EXPORT_SYMBOL_GPL(__riscv_isa_extension_available);

void __init riscv_fill_hwcap(void)
{
	struct device_node *node;
	const char *isa;
	char print_str[BITS_PER_LONG + 1];
	size_t i, j, isa_len;
	static unsigned long isa2hwcap[256] = {0};

	isa2hwcap['i'] = isa2hwcap['I'] = COMPAT_HWCAP_ISA_I;
	isa2hwcap['m'] = isa2hwcap['M'] = COMPAT_HWCAP_ISA_M;
	isa2hwcap['a'] = isa2hwcap['A'] = COMPAT_HWCAP_ISA_A;
	isa2hwcap['f'] = isa2hwcap['F'] = COMPAT_HWCAP_ISA_F;
	isa2hwcap['d'] = isa2hwcap['D'] = COMPAT_HWCAP_ISA_D;
	isa2hwcap['c'] = isa2hwcap['C'] = COMPAT_HWCAP_ISA_C;

	elf_hwcap = 0;

	bitmap_zero(riscv_isa, RISCV_ISA_EXT_MAX);

	for_each_of_cpu_node(node) {
		unsigned long this_hwcap = 0;
		unsigned long this_isa = 0;

		if (riscv_of_processor_hartid(node) < 0)
			continue;

		if (of_property_read_string(node, "riscv,isa", &isa)) {
			pr_warn("Unable to find \"riscv,isa\" devicetree entry\n");
			continue;
		}

		i = 0;
		isa_len = strlen(isa);
#if IS_ENABLED(CONFIG_32BIT)
		if (!strncmp(isa, "rv32", 4))
			i += 4;
#elif IS_ENABLED(CONFIG_64BIT)
		if (!strncmp(isa, "rv64", 4))
			i += 4;
#endif
		for (; i < isa_len; ++i) {
			this_hwcap |= isa2hwcap[(unsigned char)(isa[i])];
			/*
			 * TODO: X, Y and Z extension parsing for Host ISA
			 * bitmap will be added in-future.
			 */
			if ('a' <= isa[i] && isa[i] < 'x')
				this_isa |= (1UL << (isa[i] - 'a'));
		}

		/*
		 * All "okay" hart should have same isa. Set HWCAP based on
		 * common capabilities of every "okay" hart, in case they don't
		 * have.
		 */
		if (elf_hwcap)
			elf_hwcap &= this_hwcap;
		else
			elf_hwcap = this_hwcap;

		if (riscv_isa[0])
			riscv_isa[0] &= this_isa;
		else
			riscv_isa[0] = this_isa;
	}

	/* We don't support systems with F but without D, so mask those out
	 * here. */
	if ((elf_hwcap & COMPAT_HWCAP_ISA_F) && !(elf_hwcap & COMPAT_HWCAP_ISA_D)) {
		pr_info("This kernel does not support systems with F but not D\n");
		elf_hwcap &= ~COMPAT_HWCAP_ISA_F;
	}

	memset(print_str, 0, sizeof(print_str));
	for (i = 0, j = 0; i < BITS_PER_LONG; i++)
		if (riscv_isa[0] & BIT_MASK(i))
			print_str[j++] = (char)('a' + i);
	pr_info("riscv: ISA extensions %s\n", print_str);

	memset(print_str, 0, sizeof(print_str));
	for (i = 0, j = 0; i < BITS_PER_LONG; i++)
		if (elf_hwcap & BIT_MASK(i))
			print_str[j++] = (char)('a' + i);
	pr_info("riscv: ELF capabilities %s\n", print_str);

#ifdef CONFIG_FPU
	if (elf_hwcap & (COMPAT_HWCAP_ISA_F | COMPAT_HWCAP_ISA_D))
		static_branch_enable(&cpu_hwcap_fpu);
#endif
}

struct cpufeature_info {
	char name[ERRATA_STRING_LENGTH_MAX];
	bool (*check_func)(unsigned int stage);
};

#if defined(CONFIG_MMU) && defined(CONFIG_64BIT)
static bool cpufeature_svpbmt_check_fdt(void)
{
	const void *fdt = dtb_early_va;
	const char *str;
	int offset;

	offset = fdt_path_offset(fdt, "/cpus");
	if (offset < 0)
		return false;

	for (offset = fdt_next_node(fdt, offset, NULL); offset >= 0;
	     offset = fdt_next_node(fdt, offset, NULL)) {
		str = fdt_getprop(fdt, offset, "device_type", NULL);
		if (!str || strcmp(str, "cpu"))
			break;

		str = fdt_getprop(fdt, offset, "mmu-type", NULL);
		if (!str)
			continue;

		if (!strncmp(str + 6, "none", 4))
			continue;

		str = fdt_getprop(fdt, offset, "mmu", NULL);
		if (!str)
			continue;

		if (!strncmp(str + 6, "svpbmt", 6))
			return true;
	}

	return false;
}

static bool cpufeature_svpbmt_check_of(void)
{
	struct device_node *node;
	const char *str;

	for_each_of_cpu_node(node) {
		if (of_property_read_string(node, "mmu-type", &str))
			continue;

		if (!strncmp(str + 6, "none", 4))
			continue;

		if (of_property_read_string(node, "mmu", &str))
			continue;

		if (!strncmp(str + 6, "svpbmt", 6))
			return true;
	}

	return false;
}
#endif

static bool cpufeature_svpbmt_check_func(unsigned int stage)
{
	bool ret = false;

#if defined(CONFIG_MMU) && defined(CONFIG_64BIT)
	switch (stage) {
	case RISCV_ALTERNATIVES_EARLY_BOOT:
		return false;
	case RISCV_ALTERNATIVES_BOOT:
		return cpufeature_svpbmt_check_fdt();
	default:
		return cpufeature_svpbmt_check_of();
	}
#endif

	return ret;
}

static bool cpufeature_cmo_check_func(unsigned int stage)
{
	bool ret = false;

	switch (stage) {
	case RISCV_ALTERNATIVES_EARLY_BOOT:
		return false;
	case RISCV_ALTERNATIVES_BOOT:
//		return cpufeature_svpbmt_check_fdt();
	default:
return false;
//		return cpufeature_svpbmt_check_of();
	}

	return ret;
}

static const struct cpufeature_info cpufeature_list[CPUFEATURE_NUMBER] = {
	{
		.name = "svpbmt",
		.check_func = cpufeature_svpbmt_check_func
	},
	{
		.name = "cmo",
		.check_func = cpufeature_cmo_check_func
	},
};

static u32 __init cpufeature_probe(unsigned int stage)
{
	const struct cpufeature_info *info;
	u32 cpu_req_feature = 0;
	int idx;

	for (idx = 0; idx < CPUFEATURE_NUMBER; idx++) {
		info = &cpufeature_list[idx];

		if (info->check_func(stage))
			cpu_req_feature |= (1U << idx);
	}

	return cpu_req_feature;
}

void riscv_cpufeature_patch_func(struct alt_entry *begin, struct alt_entry *end,
				 unsigned int stage)
{
	u32 cpu_req_feature = cpufeature_probe(stage);
	u32 cpu_apply_feature = 0;
	struct alt_entry *alt;
	u32 tmp;

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != 0)
			continue;
		if (alt->errata_id >= CPUFEATURE_NUMBER) {
			WARN(1, "This feature id:%d is not in kernel cpufeature list",
				alt->errata_id);
			continue;
		}

		tmp = (1U << alt->errata_id);
		if (cpu_req_feature & tmp) {
			patch_text_nosync(alt->old_ptr, alt->alt_ptr, alt->alt_len);
			cpu_apply_feature |= tmp;
		}
	}
}
