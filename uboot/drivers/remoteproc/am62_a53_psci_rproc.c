// SPDX-License-Identifier: GPL-2.0+
/*
 * AM62x secondary A53 remoteproc facade using PSCI CPU_ON.
 */

#include <cpu_func.h>
#include <clk.h>
#include <dm.h>
#include <errno.h>
#include <remoteproc.h>
#include <asm/cache.h>
#include <dm/device_compat.h>
#include <limits.h>
#include <linux/kernel.h>
#include <linux/psci.h>

#define AM62_MAIN_TIMER1_RATE_HZ	25000000UL

struct am62_a53_psci_priv {
	u64 mpidr;
	ulong entry;
	ulong size;
	struct clk timer_clk;
	bool loaded;
};

static int am62_a53_psci_init(struct udevice *dev)
{
	struct am62_a53_psci_priv *priv = dev_get_priv(dev);
	struct udevice *psci;
	ulong rate;
	int ret;

	/* Probing the PSCI firmware device selects SMC or HVC from the DT. */
	ret = uclass_get_device_by_driver(UCLASS_FIRMWARE,
					  DM_DRIVER_GET(psci), &psci);
	if (ret)
		return ret;

	ret = clk_enable(&priv->timer_clk);
	if (ret) {
		dev_err(dev, "failed to enable main_timer1 clock: %d\n", ret);
		return ret;
	}

	rate = clk_get_rate(&priv->timer_clk);
	if (rate != AM62_MAIN_TIMER1_RATE_HZ) {
		dev_err(dev, "main_timer1 clock is %lu Hz, expected %lu Hz\n",
			rate, AM62_MAIN_TIMER1_RATE_HZ);
		return -EINVAL;
	}

	return 0;
}

static int am62_a53_psci_load(struct udevice *dev, ulong addr, ulong size)
{
	struct am62_a53_psci_priv *priv = dev_get_priv(dev);
	ulong start;
	ulong end;

	if (!addr || !size || (addr & 0x3) || size > ULONG_MAX - addr)
		return -EINVAL;

	start = ALIGN_DOWN(addr, ARCH_DMA_MINALIGN);
	end = ALIGN(addr + size, ARCH_DMA_MINALIGN);
	flush_dcache_range(start, end);
	asm volatile("dsb sy" ::: "memory");

	priv->entry = addr;
	priv->size = size;
	priv->loaded = true;

	return 0;
}

static int am62_a53_psci_start(struct udevice *dev)
{
	struct am62_a53_psci_priv *priv = dev_get_priv(dev);
	long ret;

	if (!priv->loaded)
		return -EINVAL;

	ret = (long)invoke_psci_fn(PSCI_0_2_FN64_CPU_ON, priv->mpidr,
				   priv->entry, 0);
	if (ret) {
		dev_err(dev, "PSCI CPU_ON for MPIDR 0x%llx failed: %ld\n",
			priv->mpidr, ret);
		return ret;
	}

	return 0;
}

static int am62_a53_psci_is_running(struct udevice *dev)
{
	struct am62_a53_psci_priv *priv = dev_get_priv(dev);
	long ret;

	ret = (long)invoke_psci_fn(PSCI_0_2_FN64_AFFINITY_INFO,
				   priv->mpidr, 0, 0);
	if (ret < 0)
		return ret;

	return ret == PSCI_0_2_AFFINITY_LEVEL_ON ? 0 : 1;
}

static const struct dm_rproc_ops am62_a53_psci_ops = {
	.init = am62_a53_psci_init,
	.load = am62_a53_psci_load,
	.start = am62_a53_psci_start,
	.is_running = am62_a53_psci_is_running,
};

static int am62_a53_psci_probe(struct udevice *dev)
{
	struct am62_a53_psci_priv *priv = dev_get_priv(dev);
	int ret;

	priv->mpidr = dev_read_u64_default(dev, "ti,mpidr", U64_MAX);
	if (priv->mpidr == U64_MAX)
		return -EINVAL;

	ret = clk_get_by_index(dev, 0, &priv->timer_clk);
	if (ret)
		dev_err(dev, "failed to get main_timer1 clock: %d\n", ret);

	return ret;
}

static const struct udevice_id am62_a53_psci_ids[] = {
	{ .compatible = "ti,am62-a53-psci-rproc" },
	{ }
};

U_BOOT_DRIVER(am62_a53_psci_rproc) = {
	.name = "am62_a53_psci_rproc",
	.of_match = am62_a53_psci_ids,
	.id = UCLASS_REMOTEPROC,
	.ops = &am62_a53_psci_ops,
	.probe = am62_a53_psci_probe,
	.priv_auto = sizeof(struct am62_a53_psci_priv),
};
