// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 Samuel Holland <samuel@sholland.org>
//

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include "ccu_common.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mux.h"

#include "ccu-sun6i-rtc.h"

#define IOSC_ACCURACY			300000000 /* 30% */
#define IOSC_RATE			16000000

#define LOSC_RATE			32768
#define LOSC_RATE_SHIFT			15

#define LOSC_CTRL_REG			0x0
#define LOSC_CTRL_KEY			0x16aa0000

#define IOSC_32K_CLK_DIV_REG		0x8
#define IOSC_32K_CLK_DIV		GENMASK(4, 0)
#define IOSC_32K_PRE_DIV		32

#define IOSC_CLK_CALI_REG		0xc
#define IOSC_CLK_CALI_DIV_ONES		22
#define IOSC_CLK_CALI_EN		BIT(1)
#define IOSC_CLK_CALI_SRC_SEL		BIT(0)

#define LOSC_OUT_GATING_REG		0x60

#define DCXO_CTRL_REG			0x160
#define DCXO_CTRL_CLK16M_RC_EN		BIT(0)

struct sun6i_rtc_match_data {
	bool				have_ext_osc32k		: 1;
	bool				have_iosc_calibration	: 1;
	bool				rtc_32k_single_parent	: 1;
	const struct clk_parent_data	*osc32k_fanout_parents;
	u8				osc32k_fanout_nparents;
};

static bool have_iosc_calibration;

static int ccu_iosc_enable(struct clk_hw *hw)
{
	struct ccu_common *cm = hw_to_ccu_common(hw);

	return ccu_gate_helper_enable(cm, DCXO_CTRL_CLK16M_RC_EN);
}

static void ccu_iosc_disable(struct clk_hw *hw)
{
	struct ccu_common *cm = hw_to_ccu_common(hw);

	return ccu_gate_helper_disable(cm, DCXO_CTRL_CLK16M_RC_EN);
}

static int ccu_iosc_is_enabled(struct clk_hw *hw)
{
	struct ccu_common *cm = hw_to_ccu_common(hw);

	return ccu_gate_helper_is_enabled(cm, DCXO_CTRL_CLK16M_RC_EN);
}

static unsigned long ccu_iosc_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct ccu_common *cm = hw_to_ccu_common(hw);

	if (have_iosc_calibration) {
		u32 reg = readl(cm->base + IOSC_CLK_CALI_REG);

		/*
		 * Recover the IOSC frequency by shifting the ones place of
		 * (fixed-point divider * 32768) into bit zero.
		 */
		if (reg & IOSC_CLK_CALI_EN)
			return reg >> (IOSC_CLK_CALI_DIV_ONES - LOSC_RATE_SHIFT);
	}

	return IOSC_RATE;
}

static unsigned long ccu_iosc_recalc_accuracy(struct clk_hw *hw,
					      unsigned long parent_accuracy)
{
	return IOSC_ACCURACY;
}

static const struct clk_ops ccu_iosc_ops = {
	.enable			= ccu_iosc_enable,
	.disable		= ccu_iosc_disable,
	.is_enabled		= ccu_iosc_is_enabled,
	.recalc_rate		= ccu_iosc_recalc_rate,
	.recalc_accuracy	= ccu_iosc_recalc_accuracy,
};

static struct ccu_common iosc_clk = {
	.reg		= DCXO_CTRL_REG,
	.hw.init	= CLK_HW_INIT_NO_PARENT("iosc", &ccu_iosc_ops,
						CLK_GET_RATE_NOCACHE),
};

static int ccu_iosc_32k_prepare(struct clk_hw *hw)
{
	struct ccu_common *cm = hw_to_ccu_common(hw);
	u32 val;

	if (!have_iosc_calibration)
		return 0;

	val = readl(cm->base + IOSC_CLK_CALI_REG);
	writel(val | IOSC_CLK_CALI_EN | IOSC_CLK_CALI_SRC_SEL,
	       cm->base + IOSC_CLK_CALI_REG);

	return 0;
}

static void ccu_iosc_32k_unprepare(struct clk_hw *hw)
{
	struct ccu_common *cm = hw_to_ccu_common(hw);
	u32 val;

	if (!have_iosc_calibration)
		return;

	val = readl(cm->base + IOSC_CLK_CALI_REG);
	writel(val & ~(IOSC_CLK_CALI_EN | IOSC_CLK_CALI_SRC_SEL),
	       cm->base + IOSC_CLK_CALI_REG);
}

static unsigned long ccu_iosc_32k_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct ccu_common *cm = hw_to_ccu_common(hw);
	u32 val;

	if (have_iosc_calibration) {
		val = readl(cm->base + IOSC_CLK_CALI_REG);

		/* Assume the calibrated 32k clock is accurate. */
		if (val & IOSC_CLK_CALI_SRC_SEL)
			return LOSC_RATE;
	}

	val = readl(cm->base + IOSC_32K_CLK_DIV_REG) & IOSC_32K_CLK_DIV;

	return parent_rate / IOSC_32K_PRE_DIV / (val + 1);
}

static unsigned long ccu_iosc_32k_recalc_accuracy(struct clk_hw *hw,
						  unsigned long parent_accuracy)
{
	struct ccu_common *cm = hw_to_ccu_common(hw);
	u32 val;

	if (have_iosc_calibration) {
		val = readl(cm->base + IOSC_CLK_CALI_REG);

		/* Assume the calibrated 32k clock is accurate. */
		if (val & IOSC_CLK_CALI_SRC_SEL)
			return 0;
	}

	return parent_accuracy;
}

static const struct clk_ops ccu_iosc_32k_ops = {
	.prepare		= ccu_iosc_32k_prepare,
	.unprepare		= ccu_iosc_32k_unprepare,
	.recalc_rate		= ccu_iosc_32k_recalc_rate,
	.recalc_accuracy	= ccu_iosc_32k_recalc_accuracy,
};

static struct ccu_common iosc_32k_clk = {
	.hw.init	= CLK_HW_INIT_HW("iosc-32k", &iosc_clk.hw,
					 &ccu_iosc_32k_ops,
					 CLK_GET_RATE_NOCACHE),
};

/* .fw_name will be cleared if the clock-names property is missing. */
static struct clk_parent_data ext_osc32k[] = {
	{ .fw_name = "ext-osc32k", .index = 0 }
};

static SUNXI_CCU_GATE_DATA(ext_osc32k_gate_clk, "ext-osc32k-gate",
			   ext_osc32k, 0x0, BIT(4), 0);

static const struct clk_hw *osc32k_parents[] = {
	&iosc_32k_clk.hw,
	&ext_osc32k_gate_clk.common.hw
};

static struct clk_init_data osc32k_init_data = {
	.name		= "osc32k",
	.ops		= &ccu_mux_ops,
	.parent_hws	= osc32k_parents,
	.num_parents	= ARRAY_SIZE(osc32k_parents), /* updated during probe */
};

static struct ccu_mux osc32k_clk = {
	.mux	= _SUNXI_CCU_MUX(0, 1),
	.common	= {
		.reg		= LOSC_CTRL_REG,
		.features	= CCU_FEATURE_KEY_FIELD,
		.hw.init	= &osc32k_init_data,
	},
};

/* This falls back to the global name for fwnodes without a named reference. */
static const struct clk_parent_data osc24M[] = {
	{ .fw_name = "hosc", .name = "osc24M" }
};

static struct ccu_gate osc24M_32k_clk = {
	.enable	= BIT(16),
	.common	= {
		.reg		= LOSC_OUT_GATING_REG,
		.prediv		= 750,
		.features	= CCU_FEATURE_ALL_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("osc24M-32k", osc24M,
							   &ccu_gate_ops, 0),
	},
};

static const struct clk_hw *rtc_32k_parents[] = {
	&osc32k_clk.common.hw,
	&osc24M_32k_clk.common.hw
};

static struct clk_init_data rtc_32k_init_data = {
	.name		= "rtc-32k",
	.ops		= &ccu_mux_ops,
	.parent_hws	= rtc_32k_parents,
	.num_parents	= ARRAY_SIZE(rtc_32k_parents), /* updated during probe */
};

static struct ccu_mux rtc_32k_clk = {
	.mux	= _SUNXI_CCU_MUX(1, 1),
	.common	= {
		.reg		= LOSC_CTRL_REG,
		.features	= CCU_FEATURE_KEY_FIELD,
		.hw.init	= &rtc_32k_init_data,
	},
};

static struct clk_init_data osc32k_fanout_init_data = {
	.name		= "osc32k-fanout",
	.ops		= &ccu_mux_ops,
	/* parents are set during probe */
};

static struct ccu_mux osc32k_fanout_clk = {
	.enable	= BIT(0),
	.mux	= _SUNXI_CCU_MUX(1, 2),
	.common	= {
		.reg		= LOSC_OUT_GATING_REG,
		.hw.init	= &osc32k_fanout_init_data,
	},
};

static struct ccu_common *sun6i_rtc_ccu_clks[] = {
	&iosc_clk,
	&iosc_32k_clk,
	&ext_osc32k_gate_clk.common, /* updated during probe */
	&osc32k_clk.common,
	&osc24M_32k_clk.common,
	&rtc_32k_clk.common,
	&osc32k_fanout_clk.common,
};

static struct clk_hw_onecell_data sun6i_rtc_ccu_hw_clks = {
	.num = CLK_NUMBER,
	.hws = {
		[CLK_OSC32K]		= &osc32k_clk.common.hw,
		[CLK_OSC32K_FANOUT]	= &osc32k_fanout_clk.common.hw,
		[CLK_IOSC]		= &iosc_clk.hw,
	},
};

static const struct sunxi_ccu_desc sun6i_rtc_ccu_desc = {
	.ccu_clks	= sun6i_rtc_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun6i_rtc_ccu_clks),

	.hw_clks	= &sun6i_rtc_ccu_hw_clks,
};

static const struct clk_parent_data sun50i_h6_osc32k_fanout_parents[] = {
	{ .hw = &osc32k_clk.common.hw },
};

static const struct clk_parent_data sun50i_h616_osc32k_fanout_parents[] = {
	{ .hw = &osc32k_clk.common.hw },
	{ .fw_name = "pll-32k" },
	{ .hw = &osc24M_32k_clk.common.hw }
};

static const struct clk_parent_data sun50i_r329_osc32k_fanout_parents[] = {
	{ .hw = &osc32k_clk.common.hw },
	{ .hw = &ext_osc32k_gate_clk.common.hw },
	{ .hw = &osc24M_32k_clk.common.hw }
};

static const struct sun6i_rtc_match_data sun50i_h6_rtc_ccu_data = {
	.have_ext_osc32k	= true,
	.have_iosc_calibration	= true,
	.osc32k_fanout_parents	= sun50i_h6_osc32k_fanout_parents,
	.osc32k_fanout_nparents	= ARRAY_SIZE(sun50i_h6_osc32k_fanout_parents),
};

static const struct sun6i_rtc_match_data sun50i_h616_rtc_ccu_data = {
	.have_iosc_calibration	= true,
	.rtc_32k_single_parent	= true,
	.osc32k_fanout_parents	= sun50i_h616_osc32k_fanout_parents,
	.osc32k_fanout_nparents	= ARRAY_SIZE(sun50i_h616_osc32k_fanout_parents),
};

static const struct sun6i_rtc_match_data sun50i_r329_rtc_ccu_data = {
	.have_ext_osc32k	= true,
	.osc32k_fanout_parents	= sun50i_r329_osc32k_fanout_parents,
	.osc32k_fanout_nparents	= ARRAY_SIZE(sun50i_r329_osc32k_fanout_parents),
};

static const struct of_device_id sun6i_rtc_ccu_match[] = {
	{
		.compatible	= "allwinner,sun50i-h6-rtc",
		.data		= &sun50i_h6_rtc_ccu_data,
	},
	{
		.compatible	= "allwinner,sun50i-h616-rtc",
		.data		= &sun50i_h616_rtc_ccu_data,
	},
	{
		.compatible	= "allwinner,sun50i-r329-rtc",
		.data		= &sun50i_r329_rtc_ccu_data,
	},
};

int sun6i_rtc_ccu_probe(struct device *dev, void __iomem *reg)
{
	struct device_node *node = dev->of_node;
	const struct sun6i_rtc_match_data *data;
	const struct of_device_id *match;

	match = of_match_device(sun6i_rtc_ccu_match, dev);
	if (!match)
		return 0;

	data = match->data;
	have_iosc_calibration = data->have_iosc_calibration;

	if (data->have_ext_osc32k) {
		/* ext-osc32k was the only input clock in the old binding. */
		if (!of_property_read_bool(node, "clock-names"))
			ext_osc32k->fw_name = NULL;
	} else {
		/* Do not register the orphan ext-osc32k-gate clock. */
		sun6i_rtc_ccu_clks[2] = NULL;

		osc32k_init_data.num_parents = 1;
	}

	if (data->rtc_32k_single_parent)
		rtc_32k_init_data.num_parents = 1;

	osc32k_fanout_init_data.parent_data = data->osc32k_fanout_parents;
	osc32k_fanout_init_data.num_parents = data->osc32k_fanout_nparents;

	return devm_sunxi_ccu_probe(dev, reg, &sun6i_rtc_ccu_desc);
}

MODULE_IMPORT_NS(SUNXI_CCU);
MODULE_LICENSE("GPL");
