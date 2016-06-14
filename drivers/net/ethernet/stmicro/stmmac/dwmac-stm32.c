/*
 * dwmac-stm32.c - STM32 DWMAC specific glue layer
 *
 * Copyright (C) 2016 Ilyes Gouta <ilyes.gouta@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/stmmac.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "stmmac_platform.h"

#define MII_PHY_SEL_MASK	BIT(23)

struct stm32_priv_data {
	struct platform_device *pdev;
	struct clk *tx_clk;
	struct clk *rx_clk;
	struct regmap *regmap;
	s32 phy_sel;
};

static int stm32_dwmac_init(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_priv_data *dwmac = plat_dat->bsp_priv;
	u32 val;

	if (dwmac->phy_sel > 0) {
		val = (plat_dat->interface == PHY_INTERFACE_MODE_MII) ? 0 : 1;
//		regmap_update_bits(dwmac->regmap, dwmac->phy_sel, MII_PHY_SEL_MASK, val);
	}

	clk_prepare_enable(dwmac->tx_clk);
	clk_prepare_enable(dwmac->rx_clk);

	return 0;
}

static void stm32_dwmac_exit(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_priv_data *dwmac = plat_dat->bsp_priv;

	clk_disable_unprepare(dwmac->tx_clk);
	clk_disable_unprepare(dwmac->rx_clk);
}

static int stm32_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct stm32_priv_data *dwmac;
	struct device *dev = &pdev->dev;
	struct device_node *np;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	dwmac = devm_kzalloc(dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	dwmac->pdev = pdev;
	dwmac->tx_clk = devm_clk_get(dev, "tx-clk");
	dwmac->rx_clk = devm_clk_get(dev, "rx-clk");

	if (IS_ERR(dwmac->tx_clk)
	    || IS_ERR(dwmac->rx_clk)) {
		dev_err(dev, "could not get stmmaceth/tx-clk/rx-clk clocks\n");
		return -ENODEV;
	}

	dwmac->phy_sel = -1;
	np = dev->of_node;

	dwmac->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscon");
	if (IS_ERR(dwmac->regmap))
		dev_err(&pdev->dev, "can't get MII syscon\n");
	else {
		ret = of_property_read_u32_index(np, "st,syscon", 1, &dwmac->phy_sel);
		if (ret)
			dev_err(&pdev->dev, "can't get MII syscon offset\n");
	}

	plat_dat->bsp_priv = dwmac;

	ret = stm32_dwmac_init(plat_dat);
	if (ret)
		return ret;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		stm32_dwmac_exit(plat_dat);

	return ret;
}

static int stm32_dwmac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret = stmmac_dvr_remove(ndev);

	stm32_dwmac_exit(priv->plat->bsp_priv);

	return ret;
}

static const struct of_device_id stm32_dwmac_match[] = {
	{ .compatible = "st,stm32-dwmac" },
	{ }
};
MODULE_DEVICE_TABLE(of, stm32_dwmac_match);

static struct platform_driver stm32_dwmac_driver = {
	.probe  = stm32_dwmac_probe,
	.remove = stm32_dwmac_remove,
	.driver = {
		.name           = "stm32-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = stm32_dwmac_match,
	},
};
module_platform_driver(stm32_dwmac_driver);

MODULE_AUTHOR("Ilyes Gouta <ilyes.gouta@gmail.com>");
MODULE_DESCRIPTION("STM32 DWMAC specific glue layer");
MODULE_LICENSE("GPL");
