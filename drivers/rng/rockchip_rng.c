// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Fuzhou Rockchip Electronics Co., Ltd
 */
#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <rng.h>
#include <asm/arch-rockchip/hardware.h>
#include <asm/io.h>
#include <linux/iopoll.h>
#include <linux/string.h>

#define RK_HW_RNG_MAX 32U

#define	_SBF(s,	v)			((u32)((u32)(v) << (s)))
#define	_BIT(b)				_SBF(b,	1U)

/* start of CRYPTO V1 register define */
#define CRYPTO_V1_CTRL				0x0008U
#define CRYPTO_V1_RNG_START			_BIT(8)
#define CRYPTO_V1_RNG_FLUSH			_BIT(9)

#define CRYPTO_V1_TRNG_CTRL			0x0200U
#define CRYPTO_V1_OSC_ENABLE			_BIT(16)
#define CRYPTO_V1_TRNG_SAMPLE_PERIOD(x)		((u32)(x))

#define CRYPTO_V1_TRNG_DOUT_0			0x0204U
/* end of CRYPTO V1 register define */

/* start of CRYPTO V2 register define */
#define CRYPTO_V2_RNG_CTL			0x0400U
#define CRYPTO_V2_RNG_64_BIT_LEN		_SBF(4, 0x00)
#define CRYPTO_V2_RNG_128_BIT_LEN		_SBF(4, 0x01)
#define CRYPTO_V2_RNG_192_BIT_LEN		_SBF(4, 0x02)
#define CRYPTO_V2_RNG_256_BIT_LEN		_SBF(4, 0x03)
#define CRYPTO_V2_RNG_FATESY_SOC_RING		_SBF(2, 0x00)
#define CRYPTO_V2_RNG_SLOWER_SOC_RING_0		_SBF(2, 0x01)
#define CRYPTO_V2_RNG_SLOWER_SOC_RING_1		_SBF(2, 0x02)
#define CRYPTO_V2_RNG_SLOWEST_SOC_RING		_SBF(2, 0x03)
#define CRYPTO_V2_RNG_ENABLE			_BIT(1)
#define CRYPTO_V2_RNG_START			_BIT(0)
#define CRYPTO_V2_RNG_SAMPLE_CNT		0x0404U
#define CRYPTO_V2_RNG_DOUT_0			0x0410U
/* end of CRYPTO V2 register define */

/* start of TRNG V1 register define */
#define TRNG_V1_CTRL				0x0000U
#define TRNG_V1_CTRL_NOP			_SBF(0, 0x00)
#define TRNG_V1_CTRL_RAND			_SBF(0, 0x01)
#define TRNG_V1_CTRL_SEED			_SBF(0, 0x02)

#define TRNG_V1_MODE				0x0008U
#define TRNG_V1_MODE_128_BIT			_SBF(3, 0x00)
#define TRNG_V1_MODE_256_BIT			_SBF(3, 0x01)

#define TRNG_V1_IE				0x0010U
#define TRNG_V1_IE_GLBL_EN			_BIT(31)
#define TRNG_V1_IE_SEED_DONE_EN			_BIT(1)
#define TRNG_V1_IE_RAND_RDY_EN			_BIT(0)

#define TRNG_V1_ISTAT				0x0014U
#define TRNG_V1_ISTAT_RAND_RDY			_BIT(0)

/* RAND0 ~ RAND7 */
#define TRNG_V1_RAND0				0x0020U
#define TRNG_V1_RAND7				0x003CU

#define TRNG_V1_AUTO_RQSTS			0x0060U

#define TRNG_V1_VERSION				0x00F0U
#define TRNG_v1_VERSION_CODE			0x46BCU
/* end of TRNG V1 register define */

/* start of RKRNG register define */
#define RKRNG_CTRL				0x0010U
#define RKRNG_CTRL_INST_REQ			_BIT(0)
#define RKRNG_CTRL_RESEED_REQ			_BIT(1)
#define RKRNG_CTRL_TEST_REQ			_BIT(2)
#define RKRNG_CTRL_SW_DRNG_REQ			_BIT(3)
#define RKRNG_CTRL_SW_TRNG_REQ			_BIT(4)

#define RKRNG_STATE				0x0014U
#define RKRNG_STATE_INST_ACK			_BIT(0)
#define RKRNG_STATE_RESEED_ACK			_BIT(1)
#define RKRNG_STATE_TEST_ACK			_BIT(2)
#define RKRNG_STATE_SW_DRNG_ACK			_BIT(3)
#define RKRNG_STATE_SW_TRNG_ACK			_BIT(4)

/* DRNG_DATA_0 ~ DNG_DATA_7 */
#define RKRNG_DRNG_DATA_0			0x0070U
#define RKRNG_DRNG_DATA_7			0x008CU

/* end of RKRNG register define */

#define RK_RNG_TIME_OUT	50000  /* max 50ms */

struct rk_rng_soc_data {
	int (*rk_rng_init)(struct udevice *dev);
	int (*rk_rng_read)(struct udevice *dev, void *data, size_t len);
};

struct rk_rng_platdata {
	fdt_addr_t base;
	struct rk_rng_soc_data *soc_data;
	struct clk hclk;
};

static inline void trng_write(struct rk_rng_platdata *pdata, u32 pos, u32 val)
{
    writel((u32)val, (uintptr_t)pdata->base + pos);
}

static inline u32 trng_read(struct rk_rng_platdata *pdata, u32 pos)
{
    return readl((uintptr_t)pdata->base + pos);
}

static int rk_rng_do_enable_clk(struct udevice *dev, int enable)
{
	struct rk_rng_platdata *pdata = dev_get_priv(dev);
	int ret;

	if (!pdata->hclk.dev) {
		return 0;
	}

	ret = (enable != 0) ? clk_enable(&pdata->hclk) : clk_disable(&pdata->hclk);
	if (ret == -ENOSYS || ret == 0) {
		return 0;
	}

	printf("rk rng: failed to %s clk, ret=%d\n",
	       (enable != 0) ? "enable" : "disable", ret);

	return ret;
}

static int rk_rng_enable_clk(struct udevice *dev)
{
	return rk_rng_do_enable_clk(dev, 1);
}

static int rk_rng_disable_clk(struct udevice *dev)
{
	return rk_rng_do_enable_clk(dev, 0);
}

static int rk_rng_read_regs(fdt_addr_t addr, void *buf, size_t size)
{
	u32 count = RK_HW_RNG_MAX / (u32)sizeof(u32);
	u32 reg, tmp_len;

	if (size > RK_HW_RNG_MAX) {
		return -EINVAL;
	}

	while ((size > 0U) && (count > 0U)) {
		reg = readl(addr);
		tmp_len = (u32)((size < sizeof(u32)) ? size : sizeof(u32));
		memcpy((u8 *)buf, (u8 *)&reg, tmp_len);
		addr += sizeof(u32);
		buf += tmp_len;
		size -= tmp_len;
		count--;
	}

	return 0;
}

static int cryptov1_rng_read(struct udevice *dev, void *data, size_t len)
{
	struct rk_rng_platdata *pdata = dev_get_priv(dev);
	u32 reg = 0;
	int retval;

	if (len > RK_HW_RNG_MAX) {
		return -EINVAL;
	}

	/* enable osc_ring to get entropy, sample period is set as 100 */
	trng_write(pdata, CRYPTO_V1_TRNG_CTRL, CRYPTO_V1_OSC_ENABLE | CRYPTO_V1_TRNG_SAMPLE_PERIOD(100U));

	rk_clrsetreg(pdata->base + CRYPTO_V1_CTRL, CRYPTO_V1_RNG_START,
		     CRYPTO_V1_RNG_START);

	retval = readl_poll_timeout(pdata->base + CRYPTO_V1_CTRL, reg,
				    (reg & CRYPTO_V1_RNG_START) == 0U,
				    RK_RNG_TIME_OUT);
	if (retval != 0) {
		goto exit;
	}

	retval = rk_rng_read_regs(pdata->base + CRYPTO_V1_TRNG_DOUT_0, data, len);

exit:
	/* close TRNG */
	rk_clrreg(pdata->base + CRYPTO_V1_CTRL, CRYPTO_V1_RNG_START);

	return retval;
}

static int cryptov2_rng_read(struct udevice *dev, void *data, size_t len)
{
	struct rk_rng_platdata *pdata = dev_get_priv(dev);
	u32 reg = 0;
	int retval;

	if (len > RK_HW_RNG_MAX) {
		return -EINVAL;
	}

	/* enable osc_ring to get entropy, sample period is set as 100 */
	trng_write(pdata, CRYPTO_V2_RNG_SAMPLE_CNT, 100);

	reg |= CRYPTO_V2_RNG_256_BIT_LEN;
	reg |= CRYPTO_V2_RNG_SLOWER_SOC_RING_0;
	reg |= CRYPTO_V2_RNG_ENABLE;
	reg |= CRYPTO_V2_RNG_START;

	rk_clrsetreg(pdata->base + CRYPTO_V2_RNG_CTL, 0xffffU, reg);

	retval = readl_poll_timeout(pdata->base + CRYPTO_V2_RNG_CTL, reg,
				    (reg & CRYPTO_V2_RNG_START) == 0U,
				    RK_RNG_TIME_OUT);
	if (retval != 0) {
		goto exit;
	}

	retval = rk_rng_read_regs(pdata->base + CRYPTO_V2_RNG_DOUT_0, data, len);

exit:
	/* close TRNG */
	rk_clrreg(pdata->base + CRYPTO_V2_RNG_CTL, 0xffffU);

	return retval;
}

static int trngv1_init(struct udevice *dev)
{
	u32 status, version;
	u32 auto_reseed_cnt = 1000;
	struct rk_rng_platdata *pdata = dev_get_priv(dev);

	version = trng_read(pdata, TRNG_V1_VERSION);
	if (version != TRNG_v1_VERSION_CODE) {
		printf("wrong trng version, expected = %08x, actual = %08x",
		       TRNG_V1_VERSION, version);
		return -EFAULT;
	}

	/* wait in case of RND_RDY triggered at firs power on */
	readl_poll_timeout(pdata->base + TRNG_V1_ISTAT, status,
			   (status & TRNG_V1_ISTAT_RAND_RDY) != 0U,
			   RK_RNG_TIME_OUT);

	/* clear RAND_RDY flag for first power on */
	trng_write(pdata, TRNG_V1_ISTAT, status);

	/* auto reseed after (auto_reseed_cnt * 16) byte rand generate */
	trng_write(pdata, TRNG_V1_AUTO_RQSTS, auto_reseed_cnt);

	return 0;
}

static int trngv1_rng_read(struct udevice *dev, void *data, size_t len)
{
	struct rk_rng_platdata *pdata = dev_get_priv(dev);
	u32 reg = 0;
	int retval;

	if (len > RK_HW_RNG_MAX) {
		return -EINVAL;
	}


	trng_write(pdata, TRNG_V1_MODE, TRNG_V1_MODE_256_BIT);
	trng_write(pdata, TRNG_V1_CTRL, TRNG_V1_CTRL_RAND);

	retval = readl_poll_timeout(pdata->base + TRNG_V1_ISTAT, reg,
				    (reg & TRNG_V1_ISTAT_RAND_RDY) != 0U,
				    RK_RNG_TIME_OUT);
	/* clear ISTAT */
	trng_write(pdata, TRNG_V1_ISTAT, reg);

	if (retval != 0) {
		goto exit;
	}

	retval = rk_rng_read_regs(pdata->base + TRNG_V1_RAND0, data, len);

exit:
	/* close TRNG */
	trng_write(pdata, TRNG_V1_CTRL, TRNG_V1_CTRL_NOP);

	return retval;
}

static int rkrng_init(struct udevice *dev)
{
	struct rk_rng_platdata *pdata = dev_get_priv(dev);
	u32 reg;

	rk_clrreg(pdata->base + RKRNG_CTRL, 0xffffU);

	reg = trng_read(pdata, RKRNG_STATE);
	trng_write(pdata, RKRNG_STATE, reg);

	return 0;
}

static int rkrng_rng_read(struct udevice *dev, void *data, size_t len)
{
	struct rk_rng_platdata *pdata = dev_get_priv(dev);
	u32 reg;
	int retval;

	if (len > RK_HW_RNG_MAX) {
		return -EINVAL;
	}

	rk_rng_enable_clk(dev);

	reg = RKRNG_CTRL_SW_DRNG_REQ;

	rk_clrsetreg(pdata->base + RKRNG_CTRL, 0xffffU, reg);

	retval = readl_poll_timeout(pdata->base + RKRNG_STATE, reg,
				    (reg & RKRNG_STATE_SW_DRNG_ACK) != 0U,
				    RK_RNG_TIME_OUT);
	if (retval != 0) {
		goto exit;
	}

	trng_write(pdata, RKRNG_STATE, reg);

	retval = rk_rng_read_regs(pdata->base + RKRNG_DRNG_DATA_0, data, len);

exit:
	/* close TRNG */
	rk_clrreg(pdata->base + RKRNG_CTRL, 0xffffU);

	rk_rng_disable_clk(dev);

	return retval;
}

static int rockchip_rng_read(struct udevice *dev, void *data, size_t len)
{
	unsigned char *buf = data;
	unsigned int i;
	int ret;

	struct rk_rng_platdata *pdata = dev_get_priv(dev);

	if (len == 0U) {
		return 0;
	}

	if (!pdata->soc_data || pdata->soc_data->rk_rng_read == NULL) {
		return -EINVAL;
	}

	for (i = 0; i < len / RK_HW_RNG_MAX; i++) {
		ret = pdata->soc_data->rk_rng_read(dev, buf, RK_HW_RNG_MAX);
		if (ret != 0) {
			goto exit;
		}

		buf += RK_HW_RNG_MAX;
	}

	if ((len % RK_HW_RNG_MAX) != 0U) {
		ret = pdata->soc_data->rk_rng_read(dev, buf,
						   len % RK_HW_RNG_MAX);
	}

exit:
	return ret;
}

static int rockchip_rng_ofdata_to_platdata(struct udevice *dev)
{
	struct rk_rng_platdata *pdata = dev_get_priv(dev);

	memset(pdata, 0x00, sizeof(*pdata));

	pdata->base = dev_read_addr(dev);
	if (pdata->base == FDT_ADDR_T_NONE) {
		return -ENOMEM;
	}

	clk_get_by_index(dev, 0, &pdata->hclk);

	return 0;
}

static int rockchip_rng_probe(struct udevice *dev)
{
	struct rk_rng_platdata *pdata = dev_get_priv(dev);
	int ret = 0;

	pdata->soc_data = (struct rk_rng_soc_data *)dev_get_driver_data(dev);

	if (pdata->soc_data->rk_rng_init != NULL) {
		ret = pdata->soc_data->rk_rng_init(dev);
	}

	return ret;
}

static const struct rk_rng_soc_data cryptov1_soc_data = {
	.rk_rng_read = cryptov1_rng_read,
};

static const struct rk_rng_soc_data cryptov2_soc_data = {
	.rk_rng_read = cryptov2_rng_read,
};

static const struct rk_rng_soc_data trngv1_soc_data = {
	.rk_rng_init = trngv1_init,
	.rk_rng_read = trngv1_rng_read,
};

static const struct rk_rng_soc_data rkrng_soc_data = {
	.rk_rng_init = rkrng_init,
	.rk_rng_read = rkrng_rng_read,
};

static const struct dm_rng_ops rockchip_rng_ops = {
	.read = rockchip_rng_read,
};

static const struct udevice_id rockchip_rng_match[] = {
	{
		.compatible = "rockchip,cryptov1-rng",
		.data = (ulong)&cryptov1_soc_data,
	},
	{
		.compatible = "rockchip,cryptov2-rng",
		.data = (ulong)&cryptov2_soc_data,
	},
	{
		.compatible = "rockchip,trngv1",
		.data = (ulong)&trngv1_soc_data,
	},
	{
		.compatible = "rockchip,rkrng",
		.data = (ulong)&rkrng_soc_data,
	},
	{},
};

U_BOOT_DRIVER(rockchip_rng) = {
	.name = "rockchip-rng",
	.id = UCLASS_RNG,
	.of_match = rockchip_rng_match,
	.ops = &rockchip_rng_ops,
	.probe = rockchip_rng_probe,
	.ofdata_to_platdata = rockchip_rng_ofdata_to_platdata,
	.priv_auto_alloc_size = (int)sizeof(struct rk_rng_platdata),
};
