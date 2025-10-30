// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip USBDP Combo PHY with Samsung IP block driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd
 */

// PRQA S 5124 ++
// PRQA S 5118 ++
#include <config.h>
#include <common.h>
#include <errno.h>
#include <malloc.h>
#include <asm/unaligned.h>
#include <asm/io.h>
#include <clk.h>
#include <dm/device.h>
#include <dm/of_access.h>
#include <dm/lists.h>
#include <dm/read.h>
#include <generic-phy.h>
#include <linux/bitfield.h>
#include <linux/hdmi.h>
#include <linux/media-bus-format.h>
#include <linux/list.h>
#include <asm/gpio.h>
#include <generic-phy.h>
#include <regmap.h>
#include <reset.h>
#include <drm/drm_dp_helper.h>

#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"

#ifdef GENMASK
#undef GENMASK
#endif
#define GENMASK(h, l) dw_dp_genmask(h, l)

#ifdef FIELD_PREP
#undef FIELD_PREP
#endif
#define FIELD_PREP(_mask, _val) (dw_dp_field_prep(_mask, _val))

#ifdef FIELD_GET
#undef FIELD_GET
#endif
#define FIELD_GET(_mask, _reg) (dw_dp_field_get(_mask, _reg))

#define DPTX_VERSION_NUMBER			0x0000
#define DPTX_VERSION_TYPE			0x0004
#define DPTX_ID					0x0008

#define DPTX_CONFIG_REG1			0x0100
#define DPTX_CONFIG_REG2			0x0104
#define DPTX_CONFIG_REG3			0x0108

#define DPTX_CCTL				0x0200
#define FORCE_HPD				BIT(4)
#define DEFAULT_FAST_LINK_TRAIN_EN		BIT(2)
#define ENHANCE_FRAMING_EN			BIT(1)
#define SCRAMBLE_DIS				BIT(0)
#define DPTX_SOFT_RESET_CTRL			0x0204
#define VIDEO_RESET				BIT(5)
#define AUX_RESET				BIT(4)
#define AUDIO_SAMPLER_RESET			BIT(3)
#define PHY_SOFT_RESET				BIT(1)
#define CONTROLLER_RESET			BIT(0)

#define DPTX_VSAMPLE_CTRL			0x0300
#define PIXEL_MODE_SELECT			GENMASK(22U, 21U)
#define VIDEO_MAPPING				GENMASK(20U, 16U)
#define VIDEO_STREAM_ENABLE			BIT(5)
#define DPTX_VSAMPLE_STUFF_CTRL1		0x0304
#define DPTX_VSAMPLE_STUFF_CTRL2		0x0308
#define DPTX_VINPUT_POLARITY_CTRL		0x030c
#define DE_IN_POLARITY				BIT(2)
#define HSYNC_IN_POLARITY			BIT(1)
#define VSYNC_IN_POLARITY			BIT(0)
#define DPTX_VIDEO_CONFIG1			0x0310
#define HACTIVE					GENMASK(31U, 16U)
#define HBLANK					GENMASK(15U, 2U)
#define I_P					BIT(1)
#define R_V_BLANK_IN_OSC			BIT(0)
#define DPTX_VIDEO_CONFIG2			0x0314
#define VBLANK					GENMASK(31U, 16U)
#define VACTIVE					GENMASK(15U, 0U)
#define DPTX_VIDEO_CONFIG3			0x0318
#define H_SYNC_WIDTH				GENMASK(31U, 16U)
#define H_FRONT_PORCH				GENMASK(15U, 0U)
#define DPTX_VIDEO_CONFIG4			0x031c
#define V_SYNC_WIDTH				GENMASK(31U, 16U)
#define V_FRONT_PORCH				GENMASK(15U, 0U)
#define DPTX_VIDEO_CONFIG5			0x0320
#define INIT_THRESHOLD_HI			GENMASK(22U, 21U)
#define AVERAGE_BYTES_PER_TU_FRAC		GENMASK(19U, 16U)
#define INIT_THRESHOLD				GENMASK(13U, 7U)
#define AVERAGE_BYTES_PER_TU			GENMASK(6U, 0U)
#define DPTX_VIDEO_MSA1				0x0324
#define VSTART					GENMASK(31U, 16U)
#define HSTART					GENMASK(15U, 0U)
#define DPTX_VIDEO_MSA2				0x0328
#define MISC0					GENMASK(31U, 24U)
#define DPTX_VIDEO_MSA3				0x032c
#define MISC1					GENMASK(31U, 24U)
#define DPTX_VIDEO_HBLANK_INTERVAL		0x0330
#define HBLANK_INTERVAL_EN			BIT(16)
#define HBLANK_INTERVAL				GENMASK(15U, 0U)

#define DPTX_AUD_CONFIG1			0x0400
#define AUDIO_TIMESTAMP_VERSION_NUM		GENMASK(29U, 24U)
#define AUDIO_PACKET_ID				GENMASK(23U, 16U)
#define AUDIO_MUTE				BIT(15)
#define NUM_CHANNELS				GENMASK(14U, 12U)
#define HBR_MODE_ENABLE				BIT(10)
#define AUDIO_DATA_WIDTH			GENMASK(9U, 5U)
#define AUDIO_DATA_IN_EN			GENMASK(4U, 1U)
#define AUDIO_INF_SELECT			BIT(0)

#define DPTX_SDP_VERTICAL_CTRL			0x0500
#define EN_VERTICAL_SDP				BIT(2)
#define EN_AUDIO_STREAM_SDP			BIT(1)
#define EN_AUDIO_TIMESTAMP_SDP			BIT(0)
#define DPTX_SDP_HORIZONTAL_CTRL		0x0504
#define EN_HORIZONTAL_SDP			BIT(2)
#define DPTX_SDP_STATUS_REGISTER		0x0508
#define DPTX_SDP_MANUAL_CTRL			0x050c
#define DPTX_SDP_STATUS_EN			0x0510

#define DPTX_SDP_REGISTER_BANK			0x0600
#define SDP_REGS				GENMASK(31U, 0U)

#define DPTX_PHYIF_CTRL				0x0a00
#define PHY_WIDTH				BIT(25)
#define PHY_POWERDOWN				GENMASK(20U, 17U)
#define PHY_BUSY				GENMASK(15U, 12U)
#define SSC_DIS					BIT(16)
#define XMIT_ENABLE				GENMASK(11U, 8U)
#define PHY_LANES				GENMASK(7U, 6U)
#define PHY_RATE				GENMASK(5U, 4U)
#define TPS_SEL					GENMASK(3U, 0U)
#define DPTX_PHY_TX_EQ				0x0a04
#define DPTX_CUSTOMPAT0				0x0a08
#define DPTX_CUSTOMPAT1				0x0a0c
#define DPTX_CUSTOMPAT2				0x0a10
#define DPTX_HBR2_COMPLIANCE_SCRAMBLER_RESET	0x0a14
#define DPTX_PHYIF_PWRDOWN_CTRL			0x0a18

#define DPTX_AUX_CMD				0x0b00
#define AUX_CMD_TYPE				GENMASK(31U, 28U)
#define AUX_ADDR				GENMASK(27U, 8U)
#define I2C_ADDR_ONLY				BIT(4)
#define AUX_LEN_REQ				GENMASK(3U, 0U)
#define DPTX_AUX_STATUS				0x0b04
#define AUX_TIMEOUT				BIT(17)
#define AUX_BYTES_READ				GENMASK(23U, 19U)
#define AUX_STATUS				GENMASK(7U, 4U)
#define DPTX_AUX_DATA0				0x0b08
#define DPTX_AUX_DATA1				0x0b0c
#define DPTX_AUX_DATA2				0x0b10
#define DPTX_AUX_DATA3				0x0b14

#define DPTX_GENERAL_INTERRUPT			0x0d00
#define VIDEO_FIFO_OVERFLOW_STREAM0		BIT(6)
#define AUDIO_FIFO_OVERFLOW_STREAM0		BIT(5)
#define SDP_EVENT_STREAM0			BIT(4)
#define AUX_CMD_INVALID				BIT(3)
#define AUX_REPLY_EVENT				BIT(1)
#define HPD_EVENT				BIT(0)
#define DPTX_GENERAL_INTERRUPT_ENABLE		0x0d04
#define AUX_REPLY_EVENT_EN			BIT(1)
#define HPD_EVENT_EN				BIT(0)
#define DPTX_HPD_STATUS				0x0d08
#define HPD_STATE				GENMASK(11U, 9U)
#define HPD_STATUS				BIT(8)
#define HPD_HOT_UNPLUG				BIT(2)
#define HPD_HOT_PLUG				BIT(1)
#define HPD_IRQ					BIT(0)
#define DPTX_HPD_INTERRUPT_ENABLE		0x0d0c
#define HPD_UNPLUG_ERR_EN			BIT(3)
#define HPD_UNPLUG_EN				BIT(2)
#define HPD_PLUG_EN				BIT(1)
#define HPD_IRQ_EN				BIT(0)

#define DPTX_MAX_REGISTER			DPTX_HPD_INTERRUPT_ENABLE

#define SDP_REG_BANK_SIZE			16

struct drm_dp_link_caps {
	bool enhanced_framing;
	bool tps3_supported;
	bool tps4_supported;
	bool channel_coding;
	bool ssc;
};

struct drm_dp_link_train_set {
	unsigned int voltage_swing[4];
	unsigned int pre_emphasis[4];
};

struct drm_dp_link_train {
	struct drm_dp_link_train_set request;
	struct drm_dp_link_train_set adjust;
	bool clock_recovered;
	bool channel_equalized;
};

struct dw_dp_link {
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	unsigned char revision;
	unsigned int rate;
	unsigned int lanes;
	struct drm_dp_link_caps caps;
	struct drm_dp_link_train train;
	u8 sink_count;
	u8 vsc_sdp_extension_for_colorimetry_supported;
};

struct dw_dp_video {
	struct drm_display_mode mode;
	u32 bus_format;
	u8 video_mapping;
	u8 pixel_mode;
	u8 color_format;
	u8 bpc;
	u8 bpp;
};

struct dw_dp_sdp {
	struct dp_sdp_header header;
	u8 db[32];
	unsigned long flags;
};

struct dw_dp_chip_data {
	int pixel_mode;
};

struct dw_dp {
	struct rockchip_connector connector;
	struct udevice *dev;
	struct regmap *regmap;
	struct phy phy;
	struct reset_ctl reset;
	int id;

	struct gpio_desc hpd_gpio;
	struct drm_dp_aux aux;
	struct dw_dp_link link;
	struct dw_dp_video video;

	bool force_hpd;
	bool force_output;
	u32 max_link_rate;
};

enum {
	SOURCE_STATE_IDLE,
	SOURCE_STATE_UNPLUG,
	SOURCE_STATE_HPD_TIMEOUT = 4,
	SOURCE_STATE_PLUG = 7
};

enum {
	DPTX_VM_RGB_6BIT,
	DPTX_VM_RGB_8BIT,
	DPTX_VM_RGB_10BIT,
	DPTX_VM_RGB_12BIT,
	DPTX_VM_RGB_16BIT,
	DPTX_VM_YCBCR444_8BIT,
	DPTX_VM_YCBCR444_10BIT,
	DPTX_VM_YCBCR444_12BIT,
	DPTX_VM_YCBCR444_16BIT,
	DPTX_VM_YCBCR422_8BIT,
	DPTX_VM_YCBCR422_10BIT,
	DPTX_VM_YCBCR422_12BIT,
	DPTX_VM_YCBCR422_16BIT,
	DPTX_VM_YCBCR420_8BIT,
	DPTX_VM_YCBCR420_10BIT,
	DPTX_VM_YCBCR420_12BIT,
	DPTX_VM_YCBCR420_16BIT,
};

enum {
	DPTX_MP_SINGLE_PIXEL,
	DPTX_MP_DUAL_PIXEL,
	DPTX_MP_QUAD_PIXEL,
};

enum {
	DPTX_SDP_VERTICAL_INTERVAL = BIT(0),
	DPTX_SDP_HORIZONTAL_INTERVAL = BIT(1),
};

enum {
	DPTX_PHY_PATTERN_NONE,
	DPTX_PHY_PATTERN_TPS_1,
	DPTX_PHY_PATTERN_TPS_2,
	DPTX_PHY_PATTERN_TPS_3,
	DPTX_PHY_PATTERN_TPS_4,
	DPTX_PHY_PATTERN_SERM,
	DPTX_PHY_PATTERN_PBRS7,
	DPTX_PHY_PATTERN_CUSTOM_80BIT,
	DPTX_PHY_PATTERN_CP2520_1,
	DPTX_PHY_PATTERN_CP2520_2,
};

enum {
	DPTX_PHYRATE_RBR,
	DPTX_PHYRATE_HBR,
	DPTX_PHYRATE_HBR2,
	DPTX_PHYRATE_HBR3,
};

struct dw_dp_output_format {
	u32 bus_format;
	u32 color_format;
	u8 video_mapping;
	u8 bpc;
	u8 bpp;
};

static const struct dw_dp_output_format possible_output_fmts[] = {
	{ MEDIA_BUS_FMT_RGB101010_1X30, (u32)DRM_COLOR_FORMAT_RGB444,
	  DPTX_VM_RGB_10BIT, 10, 30 },
	{ MEDIA_BUS_FMT_RGB888_1X24, (u32)DRM_COLOR_FORMAT_RGB444,
	  DPTX_VM_RGB_8BIT, 8, 24 },
	{ MEDIA_BUS_FMT_YUV10_1X30, (u32)DRM_COLOR_FORMAT_YCRCB444,
	  DPTX_VM_YCBCR444_10BIT, 10, 30 },
	{ MEDIA_BUS_FMT_YUV8_1X24, (u32)DRM_COLOR_FORMAT_YCRCB444,
	  DPTX_VM_YCBCR444_8BIT, 8, 24},
	{ MEDIA_BUS_FMT_YUYV10_1X20, (u32)DRM_COLOR_FORMAT_YCRCB422,
	  DPTX_VM_YCBCR422_10BIT, 10, 20 },
	{ MEDIA_BUS_FMT_YUYV8_1X16, (u32)DRM_COLOR_FORMAT_YCRCB422,
	  DPTX_VM_YCBCR422_8BIT, 8, 16 },
	{ MEDIA_BUS_FMT_UYYVYY10_0_5X30, (u32)DRM_COLOR_FORMAT_YCRCB420,
	  DPTX_VM_YCBCR420_10BIT, 10, 15 },
	{ MEDIA_BUS_FMT_UYYVYY8_0_5X24, (u32)DRM_COLOR_FORMAT_YCRCB420,
	  DPTX_VM_YCBCR420_8BIT, 8, 12 },
	{ MEDIA_BUS_FMT_RGB666_1X24_CPADHI, (u32)DRM_COLOR_FORMAT_RGB444,
	  DPTX_VM_RGB_6BIT, 6, 18 },
};

static u32 dw_dp_field_prep(u32 mask, u32 val) {
	u32 shift;

	shift = (u32)__builtin_ffsll((long long)mask);
	if (shift != 0U) {
		shift = shift - 1U;
	}
	if (shift >= 32U) {
		shift = 31U;
	}

	return ((val << shift) & 0xffffffffU) & mask;
}

static u32 dw_dp_field_get(u32 mask, u32 reg)
{
	u32 shift;

	shift = (u32)__builtin_ffsll((long long)mask);
	if (shift != 0U) {
		shift = shift - 1U;
	}
	if (shift >= 32U) {
		shift = 31U;
	}

	return reg & (mask >> shift);
}

static u32 dw_dp_genmask(u32 h, u32 l)
{
	u32 left_shift = (l > 31U) ? 31U : l;
	u32 right_shift = (h > 31U) ? 0U : 31U - h;

	return ((u32)~0U << left_shift) & ((u32)~0U >> right_shift); /* PRQA S 2922 */
}

static int dw_dp_aux_write_data(struct dw_dp *dp, const u8 *buffer, size_t size)
{
	size_t i, j;

	for (i = 0; (int)i < DIV_ROUND_UP((int)size, 4); i++) {
		size_t num = min_t(size_t, size - i * 4U, 4U);
		u32 value = 0;

		for (j = 0; j < num; j++) {
			value |= (u32)buffer[i * 4U + j] << (j * 8U);
		}

		(void)regmap_write(dp->regmap, (u32)DPTX_AUX_DATA0 + (u32)i * (u32)4, value);
	}

	return (int)size;
}

static int dw_dp_aux_read_data(struct dw_dp *dp, u8 *buffer, size_t size)
{
	size_t i, j;

	for (i = 0; (int)i < DIV_ROUND_UP((int)size, 4); i++) {
		size_t num = min_t(size_t, size - i * 4U, 4U);
		u32 value;

		(void)regmap_read(dp->regmap, (u32)DPTX_AUX_DATA0 + (u32)i * 4U, &value);

		for (j = 0; j < num; j++) {
			buffer[i * 4U + j] = (u8)(value >> (j * 8U));
		}
	}

	return (int)size;
}

static ssize_t dw_dp_aux_transfer(struct drm_dp_aux *aux,
				  struct drm_dp_aux_msg *msg)
{
	u32 status, value;
	ssize_t ret = 0;
	int timeout;
	struct dw_dp *dp = dev_get_priv(aux->dev);

	if (msg->size > 16U) {
		return -E2BIG;
	}

	switch (msg->request & ~(u8)DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		ret = dw_dp_aux_write_data(dp, msg->buffer, msg->size);
		if (ret < 0) {
			return ret;
		}
		break;
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret < 0) {
		return ret;
	}

	if (msg->size > 0U) {
		value = FIELD_PREP(AUX_LEN_REQ, (u32)msg->size - 1U);
	} else {
		value = FIELD_PREP((u32)I2C_ADDR_ONLY, 1U);
	}

	value |= FIELD_PREP(AUX_CMD_TYPE, msg->request);
	value |= FIELD_PREP(AUX_ADDR, msg->address);
	(void)regmap_write(dp->regmap, DPTX_AUX_CMD, value);

	timeout = regmap_read_poll_timeout(dp->regmap, (u32)DPTX_GENERAL_INTERRUPT,
					   status, (status & (u32)AUX_REPLY_EVENT) != 0U,
					   200U, 10U);

	if (timeout != 0) {
		(void)printf("timeout waiting for AUX reply\n");
		return -ETIMEDOUT;
	}
	(void)regmap_write(dp->regmap, DPTX_GENERAL_INTERRUPT, (u32)AUX_REPLY_EVENT);

	(void)regmap_read(dp->regmap, DPTX_AUX_STATUS, &value);
	if ((value & AUX_TIMEOUT) != 0U) {
		(void)printf("aux timeout\n");
		return -ETIMEDOUT;
	}

	msg->reply = (u8)FIELD_GET(AUX_STATUS, value);

	if (msg->size > 0UL && msg->reply == DP_AUX_NATIVE_REPLY_ACK) {
		if ((msg->request & (u8)DP_AUX_I2C_READ) != 0U) {
			size_t count = (size_t)FIELD_GET(AUX_BYTES_READ, value) - 1UL;

			if (count != msg->size) {
				(void)printf("aux fail to read %lu bytes\n", count);
				return -EBUSY;
			}

			ret = dw_dp_aux_read_data(dp, msg->buffer, count);
			if (ret < 0) {
				return ret;
			}
		}
	}

	return ret;
}

static bool dw_dp_bandwidth_ok(struct dw_dp *dp,
			       const struct drm_display_mode *mode, u32 bpp,
			       unsigned int lanes, unsigned int rate)
{
	u32 max_bw, req_bw;

	req_bw = (u32)mode->clock * bpp / 8U;
	max_bw = lanes * rate;
	if (req_bw > max_bw) {
		return false;
	}

	return true;
}

static void dw_dp_hpd_init(struct dw_dp *dp)
{
	if (dm_gpio_is_valid(&dp->hpd_gpio) || dp->force_hpd) {
		(void)regmap_update_bits(dp->regmap, DPTX_CCTL, (u32)FORCE_HPD,
				   FIELD_PREP((u32)FORCE_HPD, 1U));
		return;
	}

	/* Enable all HPD interrupts */
	(void)regmap_update_bits(dp->regmap, DPTX_HPD_INTERRUPT_ENABLE,
			   (u32)HPD_UNPLUG_EN | (u32)HPD_PLUG_EN | (u32)HPD_IRQ_EN,
			   FIELD_PREP((u32)HPD_UNPLUG_EN, 1U) |
			   FIELD_PREP((u32)HPD_PLUG_EN, 1U) |
			   FIELD_PREP((u32)HPD_IRQ_EN, 1U));

	/* Enable all top-level interrupts */
	(void)regmap_update_bits(dp->regmap, DPTX_GENERAL_INTERRUPT_ENABLE,
			   (u32)HPD_EVENT_EN, FIELD_PREP((u32)HPD_EVENT_EN, 1U));
}

static void dw_dp_aux_init(struct dw_dp *dp)
{
	(void)regmap_update_bits(dp->regmap, DPTX_SOFT_RESET_CTRL, (u32)AUX_RESET,
			   FIELD_PREP((u32)AUX_RESET, 1U));
	udelay(10);
	(void)regmap_update_bits(dp->regmap, DPTX_SOFT_RESET_CTRL, (u32)AUX_RESET,
			   FIELD_PREP((u32)AUX_RESET, 0U));

	(void)regmap_update_bits(dp->regmap, DPTX_GENERAL_INTERRUPT_ENABLE,
			   (u32)AUX_REPLY_EVENT_EN,
			   FIELD_PREP((u32)AUX_REPLY_EVENT_EN, 1U));
}

static void dw_dp_init(struct dw_dp *dp)
{
	(void)regmap_update_bits(dp->regmap, DPTX_SOFT_RESET_CTRL, (u32)CONTROLLER_RESET,
			   FIELD_PREP((u32)CONTROLLER_RESET, 1U));
	udelay(10);
	(void)regmap_update_bits(dp->regmap, DPTX_SOFT_RESET_CTRL, (u32)CONTROLLER_RESET,
			   FIELD_PREP((u32)CONTROLLER_RESET, 0U));

	(void)regmap_update_bits(dp->regmap, DPTX_SOFT_RESET_CTRL, (u32)PHY_SOFT_RESET,
			   FIELD_PREP((u32)PHY_SOFT_RESET, 1U));
	udelay(10);
	(void)regmap_update_bits(dp->regmap, DPTX_SOFT_RESET_CTRL, (u32)PHY_SOFT_RESET,
			   FIELD_PREP((u32)PHY_SOFT_RESET, 0U));

	(void)regmap_update_bits(dp->regmap, DPTX_CCTL, (u32)DEFAULT_FAST_LINK_TRAIN_EN,
			   FIELD_PREP((u32)DEFAULT_FAST_LINK_TRAIN_EN, 0U));

	dw_dp_hpd_init(dp);
	dw_dp_aux_init(dp);
}

static void dw_dp_phy_set_pattern(struct dw_dp *dp, u32 pattern)
{
	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, TPS_SEL,
			   FIELD_PREP(TPS_SEL, pattern));
}

static void dw_dp_phy_xmit_enable(struct dw_dp *dp, u32 lanes)
{
	u32 xmit_enable;

	switch (lanes) {
	case 4:
	case 2:
	case 1:
		xmit_enable = GENMASK(lanes - 1U, 0U);
		break;
	case 0:
	default:
		xmit_enable = 0;
		break;
	}

	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, XMIT_ENABLE,
			   FIELD_PREP(XMIT_ENABLE, xmit_enable));
}

static int dw_dp_link_power_up(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	u8 value;
	int ret;

	if (link->revision < 0x11U) {
		return 0;
	}

	ret = (int)drm_dp_dpcd_readb(&dp->aux, DP_SET_POWER, &value);
	if (ret < 0) {
		return ret;
	}

	value &= ~(u8)DP_SET_POWER_MASK;
	value |= (u8)DP_SET_POWER_D0;

	ret = (int)drm_dp_dpcd_writeb(&dp->aux, DP_SET_POWER, value);
	if (ret < 0) {
		return ret;
	}

	udelay(1000);
	return 0;
}

static int dw_dp_link_probe(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	u8 dpcd;
	int ret;

	(void)drm_dp_dpcd_writeb(&dp->aux, DP_MSTM_CTRL, 0);
	ret = drm_dp_read_dpcd_caps(&dp->aux, link->dpcd);
	if (ret < 0) {
		return ret;
	}

	ret = (int)drm_dp_dpcd_readb(&dp->aux, DP_DPRX_FEATURE_ENUMERATION_LIST,
				&dpcd);
	if (ret < 0) {
		return ret;
	}

	link->vsc_sdp_extension_for_colorimetry_supported =
		(dpcd & DP_VSC_SDP_EXT_FOR_COLORIMETRY_SUPPORTED) != 0U ? 1U: 0U;

	link->revision = link->dpcd[DP_DPCD_REV];
	link->rate = min_t(u32, min_t(u32, dp->max_link_rate, dp->phy.attrs.max_link_rate * 100U),
			   (u32)drm_dp_max_link_rate(link->dpcd));
	link->lanes = min_t(u32, (u32)dp->phy.attrs.bus_width,
			    (u32)drm_dp_max_lane_count(link->dpcd));

	link->caps.enhanced_framing = drm_dp_enhanced_frame_cap(link->dpcd);
	link->caps.tps3_supported = drm_dp_tps3_supported(link->dpcd);
	link->caps.tps4_supported = drm_dp_tps4_supported(link->dpcd);
	link->caps.channel_coding = drm_dp_channel_coding_supported(link->dpcd);
	link->caps.ssc = (link->dpcd[DP_MAX_DOWNSPREAD] &
			    DP_MAX_DOWNSPREAD_0_5) != 0U;

	return 0;
}

static int dw_dp_link_train_update_vs_emph(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	struct drm_dp_link_train_set *request = &link->train.request;
	union phy_configure_opts phy_cfg;
	unsigned int lanes = link->lanes, *vs, *pe;
	u8 buf[4];
	int i, ret;

	vs = request->voltage_swing;
	pe = request->pre_emphasis;
	lanes = lanes > 4U ? 4U : lanes;
	for (i = 0; i < (int)lanes; i++) {
		phy_cfg.dp.voltage[i] = vs[i];
		phy_cfg.dp.pre[i] = pe[i];
	}
	phy_cfg.dp.lanes = lanes;
	phy_cfg.dp.link_rate = link->rate / 100U;
	phy_cfg.dp.set_lanes = 0U;
	phy_cfg.dp.set_rate = 0U;
	phy_cfg.dp.set_voltages = 1U;
	ret = generic_phy_configure(&dp->phy, &phy_cfg);
	if (ret != 0) {
		return ret;
	}

	for (i = 0; i < (int)lanes; i++) {
		buf[i] = ((u8)vs[i] << (u8)DP_TRAIN_VOLTAGE_SWING_SHIFT) |
			 ((u8)pe[i] << (u8)DP_TRAIN_PRE_EMPHASIS_SHIFT);
	}
	ret = (int)drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET, buf, lanes);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int dw_dp_link_configure(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	union phy_configure_opts phy_cfg;
	u8 buf[2];
	int ret, phy_rate;

	/* Move PHY to P3 */
	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, PHY_POWERDOWN,
			   FIELD_PREP(PHY_POWERDOWN, 0x3));

	phy_cfg.dp.lanes = link->lanes;
	phy_cfg.dp.link_rate = link->rate / 100U;
	phy_cfg.dp.ssc = link->caps.ssc ? 1U : 0U;
	phy_cfg.dp.set_lanes = 1U;
	phy_cfg.dp.set_rate = 1U;
	phy_cfg.dp.set_voltages = 1U;
	ret = generic_phy_configure(&dp->phy, &phy_cfg);
	if (ret != 0) {
		return ret;
	}

	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, PHY_LANES,
			   FIELD_PREP(PHY_LANES, link->lanes / 2U));

	switch (link->rate) {
	case 810000:
		phy_rate = DPTX_PHYRATE_HBR3;
		break;
	case 540000:
		phy_rate = DPTX_PHYRATE_HBR2;
		break;
	case 270000:
		phy_rate = DPTX_PHYRATE_HBR;
		break;
	case 162000:
	default:
		phy_rate = DPTX_PHYRATE_RBR;
		break;
	}
	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, PHY_RATE,
			   FIELD_PREP(PHY_RATE, (u32)phy_rate));

	/* Move PHY to P0 */
	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, PHY_POWERDOWN,
			   FIELD_PREP(PHY_POWERDOWN, 0x0));

	dw_dp_phy_xmit_enable(dp, link->lanes);

	buf[0] = drm_dp_link_rate_to_bw_code((int)link->rate);
	buf[1] = (u8)link->lanes;

	if (link->caps.enhanced_framing) {
		buf[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
		(void)regmap_update_bits(dp->regmap, DPTX_CCTL, (u32)ENHANCE_FRAMING_EN,
				   FIELD_PREP((u32)ENHANCE_FRAMING_EN, 1));
	} else {
		(void)regmap_update_bits(dp->regmap, DPTX_CCTL, (u32)ENHANCE_FRAMING_EN,
				   FIELD_PREP((u32)ENHANCE_FRAMING_EN, 0));
	}

	ret = (int)drm_dp_dpcd_write(&dp->aux, DP_LINK_BW_SET, buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	buf[0] = link->caps.ssc ? (u8)DP_SPREAD_AMP_0_5 : 0U;
	buf[1] = link->caps.channel_coding ? (u8)DP_SET_ANSI_8B10B : 0U;

	ret = (int)drm_dp_dpcd_write(&dp->aux, DP_DOWNSPREAD_CTRL, buf,
				sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static void dw_dp_link_train_init(struct drm_dp_link_train *train)
{
	struct drm_dp_link_train_set *request = &train->request;
	struct drm_dp_link_train_set *adjust = &train->adjust;
	unsigned int i;

	for (i = 0; i < 4U; i++) {
		request->voltage_swing[i] = 0;
		adjust->voltage_swing[i] = 0;

		request->pre_emphasis[i] = 0;
		adjust->pre_emphasis[i] = 0;
	}

	train->clock_recovered = false;
	train->channel_equalized = false;
}

static int dw_dp_link_train_set_pattern(struct dw_dp *dp, u32 pattern)
{
	u8 buf = 0;
	int ret = 0;

	if (pattern != 0U && pattern != (u32)DP_TRAINING_PATTERN_4) {
		buf |= (u8)DP_LINK_SCRAMBLING_DISABLE;

		(void)regmap_update_bits(dp->regmap, DPTX_CCTL, (u32)SCRAMBLE_DIS,
				   FIELD_PREP((u32)SCRAMBLE_DIS, 1));
	} else {
		(void)regmap_update_bits(dp->regmap, DPTX_CCTL, (u32)SCRAMBLE_DIS,
				   FIELD_PREP((u32)SCRAMBLE_DIS, 0));
	}

	switch (pattern) {
	case DP_TRAINING_PATTERN_DISABLE:
		dw_dp_phy_set_pattern(dp, DPTX_PHY_PATTERN_NONE);
		break;
	case DP_TRAINING_PATTERN_1:
		dw_dp_phy_set_pattern(dp, DPTX_PHY_PATTERN_TPS_1);
		break;
	case DP_TRAINING_PATTERN_2:
		dw_dp_phy_set_pattern(dp, DPTX_PHY_PATTERN_TPS_2);
		break;
	case DP_TRAINING_PATTERN_3:
		dw_dp_phy_set_pattern(dp, DPTX_PHY_PATTERN_TPS_3);
		break;
	case DP_TRAINING_PATTERN_4:
		dw_dp_phy_set_pattern(dp, DPTX_PHY_PATTERN_TPS_4);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret < 0) {
		return ret;
	}

	ret = (int)drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 buf | (u8)pattern);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static void dw_dp_link_get_adjustments(struct dw_dp_link *link,
				       u8 status[DP_LINK_STATUS_SIZE])
{
	struct drm_dp_link_train_set *adjust = &link->train.adjust;
	unsigned int i, lanes;

	lanes = link->lanes > 4U ? 4U : link->lanes;
	for (i = 0; i < lanes; i++) {
		adjust->voltage_swing[i] =
			(u32)drm_dp_get_adjust_request_voltage(status, (int)i) >>
				(u32)DP_TRAIN_VOLTAGE_SWING_SHIFT;

		adjust->pre_emphasis[i] =
			(u32)drm_dp_get_adjust_request_pre_emphasis(status, (int)i) >>
				(u32)DP_TRAIN_PRE_EMPHASIS_SHIFT;
	}
}

static void dw_dp_link_train_adjust(struct drm_dp_link_train *train)
{
	struct drm_dp_link_train_set *request = &train->request;
	struct drm_dp_link_train_set *adjust = &train->adjust;
	unsigned int i;

	for (i = 0; i < 4U; i++) {
		if (request->voltage_swing[i] != adjust->voltage_swing[i]) {
			request->voltage_swing[i] = adjust->voltage_swing[i];
		}
	}

	for (i = 0; i < 4U; i++) {
		if (request->pre_emphasis[i] != adjust->pre_emphasis[i]) {
			request->pre_emphasis[i] = adjust->pre_emphasis[i];
		}
	}
}

static int dw_dp_link_clock_recovery(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	u8 status[DP_LINK_STATUS_SIZE];
	unsigned int tries = 0;
	int ret;

	ret = dw_dp_link_train_set_pattern(dp, DP_TRAINING_PATTERN_1);
	if (ret != 0) {
		return ret;
	}

	for (;;) {
		ret = dw_dp_link_train_update_vs_emph(dp);
		if (ret != 0) {
			return ret;
		}

		drm_dp_link_train_clock_recovery_delay(link->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, status);
		if (ret < 0) {
			(void)printf("failed to read link status: %d\n",
				ret);
			return ret;
		}

		if (drm_dp_clock_recovery_ok(status, (int)link->lanes)) {
			link->train.clock_recovered = true;
			break;
		}

		dw_dp_link_get_adjustments(link, status);

		if (link->train.request.voltage_swing[0] ==
		    link->train.adjust.voltage_swing[0]) {
			tries++;
		} else {
			tries = 0;
		}

		if (tries == 5U) {
			break;
		}

		dw_dp_link_train_adjust(&link->train);
	}

	return 0;
}

static int dw_dp_link_channel_equalization(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	u8 status[DP_LINK_STATUS_SIZE], pattern;
	unsigned int tries;
	int ret;

	if (link->caps.tps4_supported) {
		pattern = DP_TRAINING_PATTERN_4;
	} else if (link->caps.tps3_supported) {
		pattern = DP_TRAINING_PATTERN_3;
	} else {
		pattern = DP_TRAINING_PATTERN_2;
	}
	ret = dw_dp_link_train_set_pattern(dp, pattern);
	if (ret != 0) {
		return ret;
	}

	for (tries = 1U; tries < 5U; tries++) {
		ret = dw_dp_link_train_update_vs_emph(dp);
		if (ret != 0) {
			return ret;
		}

		drm_dp_link_train_channel_eq_delay(link->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, status);
		if (ret < 0) {
			return ret;
		}

		if (!drm_dp_clock_recovery_ok(status, (int)link->lanes)) {
			(void)printf("clock recovery lost while eq\n");
			link->train.clock_recovered = false;
			break;
		}

		if (drm_dp_channel_eq_ok(status, (int)link->lanes)) {
			link->train.channel_equalized = true;
			break;
		}

		dw_dp_link_get_adjustments(link, status);
		dw_dp_link_train_adjust(&link->train);
	}

	return 0;
}

static int dw_dp_link_downgrade(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	struct dw_dp_video *video = &dp->video;
	int ret = 0;

	switch (link->rate) {
	case 162000:
		ret = -EINVAL;
		break;
	case 270000:
		link->rate = 162000;
		break;
	case 540000:
		link->rate = 270000;
		break;
	case 810000:
		link->rate = 540000;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret < 0) {
		return ret;
	}

	if (!dw_dp_bandwidth_ok(dp, &video->mode, video->bpp, link->lanes,
				link->rate)) {
		return -E2BIG;
	}

	return 0;
}

static int dw_dp_link_train(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	int ret;

	while(true) {
		dw_dp_link_train_init(&link->train);

		(void)printf("training link: %u lane%s at %u MHz\n",
		       link->lanes, (link->lanes > 1U) ? "s" : "", link->rate / 100U);

		ret = dw_dp_link_configure(dp);
		if (ret < 0) {
			(void)printf("failed to configure DP link: %d\n", ret);
			return ret;
		}

		ret = dw_dp_link_clock_recovery(dp);
		if (ret < 0) {
			(void)printf("clock recovery failed: %d\n", ret);
			goto out;
		}

		if (!link->train.clock_recovered) {
			(void)printf("clock recovery failed, downgrading link\n");

			ret = dw_dp_link_downgrade(dp);
			if (ret < 0) {
				goto out;
			} else {
				continue;
			}
		}

		(void)printf("clock recovery succeeded\n");

		ret = dw_dp_link_channel_equalization(dp);
		if (ret < 0) {
			(void)printf("channel equalization failed: %d\n", ret);
			goto out;
		}

		if (!link->train.channel_equalized) {
			(void)printf("channel equalization failed, downgrading link\n");

			ret = dw_dp_link_downgrade(dp);
			if (ret < 0) {
				goto out;
			} else {
				continue;
			}
		}

		(void)printf("channel equalization succeeded\n");
		break;
	}

out:
	(void)dw_dp_link_train_set_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
	return ret;
}

static int dw_dp_link_enable(struct dw_dp *dp)
{
	int ret;

	ret = dw_dp_link_power_up(dp);
	if (ret < 0) {
		return ret;
	}

	ret = dw_dp_link_train(dp);
	if (ret < 0) {
		(void)printf("link training failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int dw_dp_set_phy_default_config(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	union phy_configure_opts phy_cfg;
	int ret, i, phy_rate, lanes;

	link->vsc_sdp_extension_for_colorimetry_supported = 1U;
	link->rate = 270000;
	link->lanes = dp->phy.attrs.bus_width;

	link->caps.enhanced_framing = true;
	link->caps.channel_coding = true;
	link->caps.ssc = true;

	/* Move PHY to P3 */
	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, PHY_POWERDOWN,
			   FIELD_PREP(PHY_POWERDOWN, 0x3));

	lanes = link->lanes > 4U ? 4 : (int)link->lanes;
	for (i = 0; i < lanes; i++) {
		phy_cfg.dp.voltage[i] = 3;
		phy_cfg.dp.pre[i] = 0;
	}
	phy_cfg.dp.lanes = link->lanes;
	phy_cfg.dp.link_rate = link->rate / 100U;
	phy_cfg.dp.ssc = 1U;
	phy_cfg.dp.set_lanes = 1U;
	phy_cfg.dp.set_rate = 1U;
	phy_cfg.dp.set_voltages = 1U;
	ret = generic_phy_configure(&dp->phy, &phy_cfg);
	if (ret != 0) {
		return ret;
	}

	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, PHY_LANES,
			   FIELD_PREP(PHY_LANES, link->lanes / 2U));

	phy_rate = DPTX_PHYRATE_HBR;

	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, PHY_RATE,
			   FIELD_PREP(PHY_RATE, (u32)phy_rate));

	/* Move PHY to P0 */
	(void)regmap_update_bits(dp->regmap, DPTX_PHYIF_CTRL, PHY_POWERDOWN,
			   FIELD_PREP(PHY_POWERDOWN, 0x0));

	dw_dp_phy_xmit_enable(dp, link->lanes);

	(void)regmap_update_bits(dp->regmap, DPTX_CCTL, (u32)ENHANCE_FRAMING_EN,
			   FIELD_PREP((u32)ENHANCE_FRAMING_EN, 1));

	dw_dp_phy_set_pattern(dp, DPTX_PHY_PATTERN_NONE);
	return 0;
}

static int dw_dp_send_sdp(struct dw_dp *dp, struct dw_dp_sdp *sdp)
{
	const u8 *payload = sdp->db;
	u32 reg;
	int i, nr = 0;

	reg = (u32)DPTX_SDP_REGISTER_BANK + (u32)nr * 9U * 4U;

	/* SDP header */
	(void)regmap_write(dp->regmap, reg, get_unaligned_le32(&sdp->header));

	/* SDP data payload */
	for (i = 1; i < 9; i++) {
		(void)regmap_write(dp->regmap, reg + (u32)i * 4U,
			     FIELD_PREP(SDP_REGS, get_unaligned_le32(payload)));
		payload += 4;
	}

	if ((sdp->flags & (unsigned long)DPTX_SDP_VERTICAL_INTERVAL) != 0UL) {
		(void)regmap_update_bits(dp->regmap, DPTX_SDP_VERTICAL_CTRL,
				   (u32)EN_VERTICAL_SDP << (u32)nr,
				   (u32)EN_VERTICAL_SDP << (u32)nr);
	}

	if ((sdp->flags & (unsigned long)DPTX_SDP_HORIZONTAL_INTERVAL) != 0UL) {
		(void)regmap_update_bits(dp->regmap, DPTX_SDP_HORIZONTAL_CTRL,
				   (u32)EN_HORIZONTAL_SDP << (u32)nr,
				   (u32)EN_HORIZONTAL_SDP << (u32)nr);
	}

	return 0;
}

static void dw_dp_vsc_sdp_pack(const struct drm_dp_vsc_sdp *vsc,
			       struct dw_dp_sdp *sdp)
{
	sdp->header.HB0 = 0;
	sdp->header.HB1 = DP_SDP_VSC;
	sdp->header.HB2 = vsc->revision;
	sdp->header.HB3 = vsc->length;

	sdp->db[16] = ((u8)vsc->pixelformat & 0xfU) << 4U;
	sdp->db[16] |= (u8)vsc->colorimetry & 0xfU;

	switch (vsc->bpc) {
	case 8:
		sdp->db[17] = 0x1;
		break;
	case 10:
		sdp->db[17] = 0x2;
		break;
	case 12:
		sdp->db[17] = 0x3;
		break;
	case 16:
		sdp->db[17] = 0x4;
		break;
	case 6:
	default:
		(void)0;
		break;
	}

	if (vsc->dynamic_range == DP_DYNAMIC_RANGE_CTA) {
		sdp->db[17] |= 0x80U;
	}

	sdp->db[18] = (u8)vsc->content_type & 0x7U;

	sdp->flags |= (unsigned long)DPTX_SDP_VERTICAL_INTERVAL;
}

static int dw_dp_send_vsc_sdp(struct dw_dp *dp)
{
	struct dw_dp_video *video = &dp->video;
	struct drm_dp_vsc_sdp vsc = {};
	struct dw_dp_sdp sdp = {};

	vsc.revision = 0x5;
	vsc.length = 0x13;

	switch (video->color_format) {
	case (u8)DRM_COLOR_FORMAT_YCRCB444:
		vsc.pixelformat = DP_PIXELFORMAT_YUV444;
		break;
	case (u8)DRM_COLOR_FORMAT_YCRCB420:
		vsc.pixelformat = DP_PIXELFORMAT_YUV420;
		break;
	case (u8)DRM_COLOR_FORMAT_YCRCB422:
		vsc.pixelformat = DP_PIXELFORMAT_YUV422;
		break;
	case (u8)DRM_COLOR_FORMAT_RGB444:
	default:
		vsc.pixelformat = DP_PIXELFORMAT_RGB;
		break;
	}

	if (video->color_format == DRM_COLOR_FORMAT_RGB444) {
		vsc.colorimetry = DP_COLORIMETRY_DEFAULT;
	} else {
		vsc.colorimetry = DP_COLORIMETRY_BT709_YCC;
	}

	vsc.bpc = (int)video->bpc;
	vsc.dynamic_range = DP_DYNAMIC_RANGE_CTA;
	vsc.content_type = DP_CONTENT_TYPE_NOT_DEFINED;

	dw_dp_vsc_sdp_pack(&vsc, &sdp);

	return dw_dp_send_sdp(dp, &sdp);
}

static int dw_dp_video_set_pixel_mode(struct dw_dp *dp, u8 pixel_mode)
{
	int ret = 0;

	switch (pixel_mode) {
	case DPTX_MP_SINGLE_PIXEL:
	case DPTX_MP_DUAL_PIXEL:
	case DPTX_MP_QUAD_PIXEL:
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret < 0) {
		return ret;
	}

	(void)regmap_update_bits(dp->regmap, DPTX_VSAMPLE_CTRL, PIXEL_MODE_SELECT,
			   FIELD_PREP(PIXEL_MODE_SELECT, pixel_mode));

	return 0;
}

static int dw_dp_video_set_msa(struct dw_dp *dp, u8 color_format, u8 bpc,
			       u16 vstart, u16 hstart)
{
	struct dw_dp_link *link = &dp->link;
	u32 misc = 0;
	int ret = 0;

	if (link->vsc_sdp_extension_for_colorimetry_supported != 0U) {
		misc = misc | DP_MSA_MISC_COLOR_VSC_SDP;
	}

	switch (color_format) {
	case (u8)DRM_COLOR_FORMAT_RGB444:
		misc |= DP_MSA_MISC_COLOR_RGB;
		break;
	case (u8)DRM_COLOR_FORMAT_YCRCB444:
		misc |= DP_MSA_MISC_COLOR_YCBCR_444_BT709;
		break;
	case (u8)DRM_COLOR_FORMAT_YCRCB422:
		misc |= DP_MSA_MISC_COLOR_YCBCR_422_BT709;
		break;
	case (u8)DRM_COLOR_FORMAT_YCRCB420:
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret < 0) {
		return ret;
	}
	switch (bpc) {
	case 6:
		misc |= DP_MSA_MISC_6_BPC;
		break;
	case 8:
		misc |= DP_MSA_MISC_8_BPC;
		break;
	case 10:
		misc |= DP_MSA_MISC_10_BPC;
		break;
	case 12:
		misc |= DP_MSA_MISC_12_BPC;
		break;
	case 16:
		misc |= DP_MSA_MISC_16_BPC;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret < 0) {
		return ret;
	}
	(void)regmap_write(dp->regmap, DPTX_VIDEO_MSA1,
		     FIELD_PREP(VSTART, vstart) | FIELD_PREP(HSTART, hstart));
	(void)regmap_write(dp->regmap, DPTX_VIDEO_MSA2, FIELD_PREP(MISC0, (u32)misc));
	(void)regmap_write(dp->regmap, DPTX_VIDEO_MSA3, FIELD_PREP(MISC1, (u32)misc >> 8U));

	return 0;
}

static int dw_dp_video_enable(struct dw_dp *dp)
{
	struct dw_dp_video *video = &dp->video;
	struct dw_dp_link *link = &dp->link;
	struct drm_display_mode *mode = &video->mode;
	u8 color_format = video->color_format;
	u8 bpc = video->bpc;
	u8 pixel_mode = video->pixel_mode;
	u8 bpp = video->bpp, init_threshold, vic;
	u32 hactive, hblank, h_sync_width, h_front_porch;
	u32 vactive, vblank, v_sync_width, v_front_porch;
	u32 vstart = (u32)mode->vtotal - (u32)mode->vsync_start;
	u32 hstart = (u32)mode->htotal - (u32)mode->hsync_start;
	u32 peak_stream_bandwidth, link_bandwidth;
	u32 average_bytes_per_tu, average_bytes_per_tu_frac;
	u32 ts, hblank_interval;
	u32 value;
	int ret;

	ret = dw_dp_video_set_pixel_mode(dp, pixel_mode);
	if (ret != 0) {
		return ret;
	}

	ret = dw_dp_video_set_msa(dp, color_format, bpc, (u16)vstart, (u16)hstart);
	if (ret != 0) {
		return ret;
	}

	(void)regmap_update_bits(dp->regmap, DPTX_VSAMPLE_CTRL, VIDEO_MAPPING,
			   FIELD_PREP(VIDEO_MAPPING, video->video_mapping));

	/* Configure DPTX_VINPUT_POLARITY_CTRL register */
	value = 0;
	if ((mode->flags & DRM_MODE_FLAG_PHSYNC) != 0U) {
		value |= FIELD_PREP((u32)HSYNC_IN_POLARITY, 1);
	}
	if ((mode->flags & DRM_MODE_FLAG_PVSYNC) != 0U) {
		value |= FIELD_PREP((u32)VSYNC_IN_POLARITY, 1);
	}
	(void)regmap_write(dp->regmap, DPTX_VINPUT_POLARITY_CTRL, value);

	/* Configure DPTX_VIDEO_CONFIG1 register */
	hactive = (unsigned int)mode->hdisplay;
	hblank = (unsigned int)mode->htotal - (unsigned int)mode->hdisplay;
	value = FIELD_PREP(HACTIVE, hactive) | FIELD_PREP(HBLANK, hblank);
	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) != 0U) {
		value |= FIELD_PREP((u32)I_P, 1);
	}
	vic = drm_match_cea_mode(mode);
	if (vic == (u8)5 || vic == (u8)6 || vic == (u8)7 ||
	    vic == (u8)10 || vic == (u8)11 || vic == (u8)20 ||
	    vic == (u8)21 || vic == (u8)22 || vic == (u8)39 ||
	    vic == (u8)25 || vic == (u8)26 || vic == (u8)40 ||
	    vic == (u8)44 || vic == (u8)45 || vic == (u8)46 ||
	    vic == (u8)50 || vic == (u8)51 || vic == (u8)54 ||
	    vic == (u8)55 || vic == (u8)58 || vic  == (u8)59) {
		value |= (u32)R_V_BLANK_IN_OSC;
	}
	(void)regmap_write(dp->regmap, DPTX_VIDEO_CONFIG1, value);

	/* Configure DPTX_VIDEO_CONFIG2 register */
	vblank = (u32)mode->vtotal - (u32)mode->vdisplay;
	vactive = (u32)mode->vdisplay;
	(void)regmap_write(dp->regmap, DPTX_VIDEO_CONFIG2,
		     FIELD_PREP(VBLANK, vblank) | FIELD_PREP(VACTIVE, vactive));

	/* Configure DPTX_VIDEO_CONFIG3 register */
	h_sync_width = (u32)mode->hsync_end - (u32)mode->hsync_start;
	h_front_porch = (u32)mode->hsync_start - (u32)mode->hdisplay;
	(void)regmap_write(dp->regmap, DPTX_VIDEO_CONFIG3,
		     FIELD_PREP(H_SYNC_WIDTH, h_sync_width) |
		     FIELD_PREP(H_FRONT_PORCH, h_front_porch));

	/* Configure DPTX_VIDEO_CONFIG4 register */
	v_sync_width = (u32)mode->vsync_end - (u32)mode->vsync_start;
	v_front_porch = (u32)mode->vsync_start - (u32)mode->vdisplay;
	(void)regmap_write(dp->regmap, DPTX_VIDEO_CONFIG4,
		     FIELD_PREP(V_SYNC_WIDTH, v_sync_width) |
		     FIELD_PREP(V_FRONT_PORCH, v_front_porch));

	/* Configure DPTX_VIDEO_CONFIG5 register */
	peak_stream_bandwidth = (u32)mode->clock * (u32)bpp / 8U;
	link_bandwidth = (link->rate / 1000U) * link->lanes;
	ts = peak_stream_bandwidth * 64U / link_bandwidth;
	average_bytes_per_tu = ts / 1000U;
	average_bytes_per_tu_frac = ts / 100U - average_bytes_per_tu * 10U;
	if (pixel_mode == (u8)DPTX_MP_SINGLE_PIXEL) {
		if (average_bytes_per_tu < 6U) {
			init_threshold = 32;
		} else if (hblank <= 80U &&
			 color_format != DRM_COLOR_FORMAT_YCRCB420) {
			init_threshold = 12;
		/* hblank <= 40 && color_format == DRM_COLOR_FORMAT_YCRCB420) */
		} else if (hblank <= 40U) {
			init_threshold = 3;
		} else {
			init_threshold = 16;
		}
	} else {
		u32 t1, t2, t3;

		switch (bpc) {
		case 6:
			t1 = (4U * 1000U / 9U) * link->lanes;
			break;
		case 8:
			if (color_format == DRM_COLOR_FORMAT_YCRCB422) {
				t1 = (1000U / 2U) * link->lanes;
			} else {
				if (pixel_mode == (u8)DPTX_MP_DUAL_PIXEL) {
					t1 = (1000U / 3U) * link->lanes;
				} else {
					t1 = (3000U / 16U) * link->lanes;
				}
			}
			break;
		case 10:
			if (color_format == DRM_COLOR_FORMAT_YCRCB422) {
				t1 = (2000U / 5U) * link->lanes;
			} else {
				t1 = (4000U / 15U) * link->lanes;
			}
			break;
		case 12:
			if (color_format == DRM_COLOR_FORMAT_YCRCB422) {
				if (pixel_mode == (u8)DPTX_MP_DUAL_PIXEL) {
					t1 = (1000U / 6U) * link->lanes;
				} else {
					t1 = (1000U / 3U) * link->lanes;
				}
			} else {
				t1 = (2000U / 9U) * link->lanes;
			}
			break;
		case 16:
			if (color_format != DRM_COLOR_FORMAT_YCRCB422 &&
			    pixel_mode == (u8)DPTX_MP_DUAL_PIXEL) {
				t1 = (1000U / 6U) * link->lanes;
			} else {
				t1 = (1000U / 4U) * link->lanes;
			}
			break;
		default:
			ret = -EINVAL;
			break;
		}
		if (ret < 0) {
			return ret;
		}
		if (color_format == DRM_COLOR_FORMAT_YCRCB420) {
			t2 = (link->rate / 4U) * 1000U / ((u32)mode->clock / 2U);
		} else {
			t2 = (link->rate / 4U) * 1000U / (u32)mode->clock;
		}

		if (average_bytes_per_tu_frac != 0U) {
			t3 = average_bytes_per_tu + 1U;
		} else {
			t3 = average_bytes_per_tu;
		}
		init_threshold = (u8)(t1 * t2 * t3 / (1000U * 1000U));
		if (init_threshold <= 16U || average_bytes_per_tu < 10U) {
			init_threshold = 40;
		}
	}

	(void)regmap_write(dp->regmap, DPTX_VIDEO_CONFIG5,
		     FIELD_PREP(INIT_THRESHOLD_HI, (u32)init_threshold >> 6U) |
		     FIELD_PREP(AVERAGE_BYTES_PER_TU_FRAC,
				average_bytes_per_tu_frac) |
		     FIELD_PREP(INIT_THRESHOLD, init_threshold) |
		     FIELD_PREP(AVERAGE_BYTES_PER_TU, average_bytes_per_tu));

	/* Configure DPTX_VIDEO_HBLANK_INTERVAL register */
	hblank_interval = hblank * (link->rate / 4U) / (u32)mode->clock;
	(void)regmap_write(dp->regmap, DPTX_VIDEO_HBLANK_INTERVAL,
		     FIELD_PREP((u32)HBLANK_INTERVAL_EN, 1) |
		     FIELD_PREP((u32)HBLANK_INTERVAL, hblank_interval));

	/* Video stream enable */
	(void)regmap_update_bits(dp->regmap, DPTX_VSAMPLE_CTRL, (u32)VIDEO_STREAM_ENABLE,
			   FIELD_PREP((u32)VIDEO_STREAM_ENABLE, 1));

	if (link->vsc_sdp_extension_for_colorimetry_supported != 0U) {
		(void)dw_dp_send_vsc_sdp(dp);
	}

	return 0;
}

static bool dw_dp_detect(struct dw_dp *dp)
{
	u32 value;

	if (dm_gpio_is_valid(&dp->hpd_gpio)) {
		return dm_gpio_get_value(&dp->hpd_gpio) > 0 ? true : false;
	}

	(void)regmap_read(dp->regmap, DPTX_HPD_STATUS, &value);
	if (FIELD_GET(HPD_STATE, value) == (u32)SOURCE_STATE_PLUG) {
		(void)regmap_write(dp->regmap, DPTX_HPD_STATUS, (u32)HPD_HOT_PLUG);
		return true;
	}

	return false;
}

static struct dw_dp *connector_to_dw_dp(struct rockchip_connector *conn)
{
	struct dw_dp *dp;

	if (dev_get_priv(conn->dev)) {
		dp = dev_get_priv(conn->dev);
	} else {
		dp = dev_get_priv(conn->dev->parent);
	}

	return dp;
}

static int dw_dp_connector_init(struct rockchip_connector *conn, struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct dw_dp *dp;
	u32 output_if;
	int ret;

	if (dev_get_priv(conn->dev)) {
		dp  = dev_get_priv(conn->dev);
	} else {
		dp = dev_get_priv(conn->dev->parent);
	}
	output_if = dp->id != 0 ? (u32)VOP_OUTPUT_IF_DP1 : (u32)VOP_OUTPUT_IF_DP0;
	output_if = output_if | (u32)conn_state->output_if;
	conn_state->output_if = (int)output_if;
	conn_state->output_mode = ROCKCHIP_OUT_MODE_AAAA;
	conn_state->color_encoding = DRM_COLOR_YCBCR_BT709;

	(void)clk_set_defaults(dp->dev);

	(void)reset_assert(&dp->reset);
	udelay(20);
	(void)reset_deassert(&dp->reset);

	conn_state->disp_info  = rockchip_get_disp_info(conn_state->type,
							dp->id);
	dw_dp_init(dp);
	ret = generic_phy_power_on(&dp->phy);

	return ret;
}

static int dw_dp_connector_get_edid(struct rockchip_connector *conn, struct display_state *state)
{
	int ret;
	struct connector_state *conn_state = &state->conn_state;
	struct dw_dp *dp = connector_to_dw_dp(conn);

	ret = drm_do_get_edid(&dp->aux.ddc, conn_state->edid);

	return ret;
}

static int dw_dp_get_output_fmts_index(u32 bus_format)
{
	int i;

	for (i = 0; (unsigned long)i < ARRAY_SIZE(possible_output_fmts); i++) {
		const struct dw_dp_output_format *fmt = &possible_output_fmts[i];

		if (fmt->bus_format == bus_format) {
			break;
		}
	}

	if ((unsigned long)i == ARRAY_SIZE(possible_output_fmts)) {
		return 1;
	}

	return i;
}

static int dw_dp_connector_prepare(struct rockchip_connector *conn, struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct dw_dp *dp = connector_to_dw_dp(conn);
	struct dw_dp_video *video = &dp->video;
	int bus_fmt;

	bus_fmt = dw_dp_get_output_fmts_index((u32)conn_state->bus_format);
	video->video_mapping = possible_output_fmts[bus_fmt].video_mapping; /* PRQA S 2844 */
	video->color_format = (u8)possible_output_fmts[bus_fmt].color_format;
	video->bus_format = possible_output_fmts[bus_fmt].bus_format;
	video->bpc = possible_output_fmts[bus_fmt].bpc;
	video->bpp = possible_output_fmts[bus_fmt].bpp;

	return 0;
}

static int dw_dp_connector_enable(struct rockchip_connector *conn, struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;
	struct dw_dp *dp = connector_to_dw_dp(conn);
	struct dw_dp_video *video = &dp->video;
	int ret;

	(void)memcpy(&video->mode, mode, sizeof(video->mode));

	if (dp->force_output) {
		ret = dw_dp_set_phy_default_config(dp);
		if (ret < 0) {
			(void)printf("failed to set phy_default config: %d\n", ret);
		}
	} else {
		ret = dw_dp_link_enable(dp);
		if (ret < 0) {
			(void)printf("failed to enable link: %d\n", ret);
			return ret;
		}
	}

	ret = dw_dp_video_enable(dp);
	if (ret < 0) {
		(void)printf("failed to enable video: %d\n", ret);
		return ret;
	}

	return 0;
}

static int dw_dp_connector_disable(struct rockchip_connector *conn, struct display_state *state)
{
	/* TODO */

	return 0;
}

static int dw_dp_connector_detect(struct rockchip_connector *conn, struct display_state *state)
{
	struct dw_dp *dp = connector_to_dw_dp(conn);
	int status, tries, ret;

	for (tries = 0; tries < 200; tries++) {
		status = dw_dp_detect(dp) ? 1 : 0;
		if (status != 0) {
			break;
		}
		mdelay(2);
	}

	if (state->force_output && status == 0) {
		dp->force_output = true;
	}

	if (status == 0 && !dp->force_output) {
		(void)generic_phy_power_off(&dp->phy);
	}

	if (status != 0 && !dp->force_output) {
		ret = dw_dp_link_probe(dp);
		if (ret != 0) {
			(void)printf("failed to probe DP link: %d\n", ret);
		}
	}

	return status;
}

static int dw_dp_mode_valid(struct dw_dp *dp, struct hdmi_edid_data *edid_data)
{
	struct dw_dp_link *link = &dp->link;
	struct drm_display_info *di = &edid_data->display_info;
	u32 min_bpp;
	int i;

	if ((di->color_formats & DRM_COLOR_FORMAT_YCRCB420) != 0U &&
	    link->vsc_sdp_extension_for_colorimetry_supported != 0U) {
		min_bpp = 12;
	} else if ((di->color_formats & DRM_COLOR_FORMAT_YCRCB422) != 0U) {
		min_bpp = 16;
	} else if ((di->color_formats & DRM_COLOR_FORMAT_RGB444) != 0U) {
		min_bpp = 18;
	} else {
		min_bpp = 24;
	}

	for (i = 0; i < edid_data->modes; i++) {
		if (!dw_dp_bandwidth_ok(dp, &edid_data->mode_buf[i], min_bpp, link->lanes,
					link->rate)) {
			edid_data->mode_buf[i].invalid = true;
		}
	}

	return 0;
}

static u32 dw_dp_get_output_bus_fmts(struct dw_dp *dp, struct hdmi_edid_data *edid_data)
{
	struct dw_dp_link *link = &dp->link;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(possible_output_fmts); i++) {
		const struct dw_dp_output_format *fmt = &possible_output_fmts[i];

		if (fmt->bpc > edid_data->display_info.bpc) {
			continue;
		}

		if ((edid_data->display_info.color_formats & fmt->color_format) == 0U) {
			continue;
		}

		if (fmt->color_format == DRM_COLOR_FORMAT_YCRCB420 &&
		    link->vsc_sdp_extension_for_colorimetry_supported == 0U) {
			continue;
		}

		if (drm_mode_is_420(&edid_data->display_info, edid_data->preferred_mode) &&
		    fmt->color_format != DRM_COLOR_FORMAT_YCRCB420) {
			continue;
		}

		if (!dw_dp_bandwidth_ok(dp, edid_data->preferred_mode, fmt->bpp, link->lanes,
					link->rate)) {
			continue;
		}

		break;
	}

	if (i == ARRAY_SIZE(possible_output_fmts)) {
		return 1;
	}

	return i;
}

static int dw_dp_connector_get_timing(struct rockchip_connector *conn, struct display_state *state)
{
	int ret = 0, i;
	struct connector_state *conn_state = &state->conn_state;
	struct dw_dp *dp = connector_to_dw_dp(conn);
	struct drm_display_mode *mode = &conn_state->mode;
	struct hdmi_edid_data edid_data;
	struct drm_display_mode *mode_buf;
	struct vop_rect rect;
	u32 bus_fmt;

	mode_buf = malloc((unsigned long)MODE_LEN * sizeof(struct drm_display_mode));
	if (!mode_buf) {
		return -ENOMEM;
	}

	(void)memset(mode_buf, 0, (unsigned long)MODE_LEN * sizeof(struct drm_display_mode));
	(void)memset(&edid_data, 0, sizeof(struct hdmi_edid_data));
	edid_data.mode_buf = mode_buf;

	if (!dp->force_output) {
		ret = drm_do_get_edid(&dp->aux.ddc, conn_state->edid);
		if (ret == 0) {
			ret = drm_add_edid_modes(&edid_data, conn_state->edid);
		}

		if (ret < 0) {
			(void)printf("failed to get edid\n");
			goto err;
		}

		//drm_rk_filter_whitelist(&edid_data);
		if (state->conn_state.secondary) {
			rect.width = state->crtc_state.max_output.width / 2;
			rect.height = state->crtc_state.max_output.height / 2;
		} else {
			rect.width = state->crtc_state.max_output.width;
			rect.height = state->crtc_state.max_output.height;
		}

		drm_mode_max_resolution_filter(&edid_data, &rect);
		(void)dw_dp_mode_valid(dp, &edid_data);

		if (drm_mode_prune_invalid(&edid_data) == 0) {
			(void)printf("can't find valid dp mode\n");
			ret = -EINVAL;
			goto err;
		}

		for (i = 0; i < edid_data.modes; i++) {
			edid_data.mode_buf[i].vrefresh =
				drm_mode_vrefresh(&edid_data.mode_buf[i]);
		}

		drm_mode_sort(&edid_data);
		(void)memcpy(mode, edid_data.preferred_mode, sizeof(struct drm_display_mode));
	}

	if (state->force_output) {
		bus_fmt = (unsigned int)dw_dp_get_output_fmts_index(state->force_bus_format);
	} else {
		bus_fmt = dw_dp_get_output_bus_fmts(dp, &edid_data);
	}

	conn_state->bus_format = (int)possible_output_fmts[bus_fmt].bus_format; /* PRQA S 2844 */

	switch (possible_output_fmts[bus_fmt].color_format) {
	case (u32)DRM_COLOR_FORMAT_YCRCB420:
		conn_state->output_mode = ROCKCHIP_OUT_MODE_YUV420;
		break;
	case (u32)DRM_COLOR_FORMAT_YCRCB422:
		conn_state->output_mode = ROCKCHIP_OUT_MODE_S888_DUMMY;
		break;
	case (u32)DRM_COLOR_FORMAT_RGB444:
	case (u32)DRM_COLOR_FORMAT_YCRCB444:
	default:
		conn_state->output_mode = ROCKCHIP_OUT_MODE_AAAA;
		break;
	}

err:
	free(mode_buf);

	return ret;
}

static const struct rockchip_connector_funcs dw_dp_connector_funcs = {
	.init = dw_dp_connector_init,
	.get_edid = dw_dp_connector_get_edid,
	.prepare = dw_dp_connector_prepare,
	.enable = dw_dp_connector_enable,
	.disable = dw_dp_connector_disable,
	.detect = dw_dp_connector_detect,
	.get_timing = dw_dp_connector_get_timing,
};

static int dw_dp_ddc_init(struct dw_dp *dp)
{
	dp->aux.name = "dw-dp";
	dp->aux.dev = dp->dev;
	dp->aux.transfer = dw_dp_aux_transfer;
	dp->aux.ddc.ddc_xfer = drm_dp_i2c_xfer;

	return 0;
}

static u32 dw_dp_parse_link_frequencies(struct dw_dp *dp)
{
	struct udevice *dev = dp->dev;
	const struct device_node *endpoint;
	u64 frequency = 0;

	endpoint = rockchip_of_graph_get_endpoint_by_regs(dev->node, 1, 0);
	if (!endpoint) {
		return 0;
	}

	if (of_property_read_u64(endpoint, "link-frequencies", &frequency) < 0) {
		return 0;
	}

	if (frequency == 0UL) {
		return 0;
	}

	(void)do_div(frequency, 10 * 1000);	/* symbol rate kbytes */

	switch (frequency) {
	case 162000:
	case 270000:
	case 540000:
	case 810000:
		break;
	default:
		dev_err(dev, "invalid link frequency value: %llu\n", frequency);
		frequency = 0UL;
		break;
	}

	return (u32)frequency;
}

static int dw_dp_parse_dt(struct dw_dp *dp)
{
	dp->force_hpd = dev_read_bool(dp->dev, "force-hpd");

	dp->max_link_rate = dw_dp_parse_link_frequencies(dp);
	if (dp->max_link_rate == 0U) {
		dp->max_link_rate = 810000;
	}

	return 0;
}

static int dw_dp_probe(struct udevice *dev)
{
	struct dw_dp *dp = dev_get_priv(dev);
	const struct dw_dp_chip_data *pdata =
		(const struct dw_dp_chip_data *)dev_get_driver_data(dev);
	int ret;

	ret = regmap_init_mem(dev, &dp->regmap);
	if (ret != 0) {
		return ret;
	}

	dp->id = of_alias_get_id(ofnode_to_np(dev->node), "dp");
	if (dp->id < 0) {
		dp->id = 0;
	}

	dp->video.pixel_mode = (u8)pdata->pixel_mode;

	ret = reset_get_by_index(dev, 0, &dp->reset);
	if (ret != 0) {
		dev_err(dev, "failed to get reset control: %d\n", ret);
		return ret;
	}

	ret = gpio_request_by_name(dev, "hpd-gpios", 0, &dp->hpd_gpio,
				   GPIOD_IS_IN);
	if (ret != 0 && ret != -ENOENT) {
		dev_err(dev, "failed to get hpd GPIO: %d\n", ret);
		return ret;
	}

	(void)generic_phy_get_by_index(dev, 0, &dp->phy);

	dp->dev = dev;

	ret = dw_dp_parse_dt(dp);
	if (ret != 0) {
		dev_err(dev, "failed to parse DT\n");
		return ret;
	}

	(void)dw_dp_ddc_init(dp);

	(void)rockchip_connector_bind(&dp->connector, dev, dp->id, &dw_dp_connector_funcs, NULL,
				DRM_MODE_CONNECTOR_DisplayPort);

	return 0;
}

static int dw_dp_bind(struct udevice *parent)
{
	struct udevice *child;
	ofnode subnode;
	const char *node_name;
	int ret;

	dev_for_each_subnode(subnode, parent) {
		if (!ofnode_valid(subnode)) {
			(void)printf("%s: no subnode for %s\n", __func__, parent->name);
			return -ENXIO;
		}

		node_name = ofnode_get_name(subnode);
		debug("%s: subnode %s\n", __func__, node_name);

		if (strcasecmp(node_name, "dp0") == 0) {
			ret = device_bind_driver_to_node(parent,
							 "dw_dp_port0",
							 node_name, subnode, &child);
			if (ret != 0) {
				(void)printf("%s: '%s' cannot bind its driver\n",
				       __func__, node_name);
				return ret;
			}
		}
	}

	return 0;
}

static int dw_dp_port_probe(struct udevice *dev)
{
	struct dw_dp *dp = dev_get_priv(dev->parent);

	(void)rockchip_connector_bind(&dp->connector, dev, dp->id, &dw_dp_connector_funcs, NULL,
				DRM_MODE_CONNECTOR_DisplayPort);

	return 0;
}

static const struct dw_dp_chip_data rk3588_dp = {
	.pixel_mode = DPTX_MP_QUAD_PIXEL,
};

static const struct dw_dp_chip_data rk3576_dp = {
	.pixel_mode = DPTX_MP_DUAL_PIXEL,
};

static const struct udevice_id dw_dp_ids[] = {
	{
		.compatible = "rockchip,rk3576-dp",
		.data = (ulong)&rk3576_dp,
	},
	{
		.compatible = "rockchip,rk3588-dp",
		.data = (ulong)&rk3588_dp,
	},
	{}
};

U_BOOT_DRIVER(dw_dp_port) = {
	.name		= "dw_dp_port0",
	.id		= UCLASS_DISPLAY,
	.probe		= dw_dp_port_probe,
};

U_BOOT_DRIVER(dw_dp) = {
	.name = "dw_dp",
	.id = UCLASS_DISPLAY,
	.of_match = dw_dp_ids,
	.probe = dw_dp_probe,
	.bind = dw_dp_bind,
	.priv_auto_alloc_size = (int)sizeof(struct dw_dp),
};

// PRQA S 5118 --
// PRQA S 5124 --
