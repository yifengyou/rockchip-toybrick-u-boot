/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/unaligned.h>
#include <boot_rkimg.h>
#include <config.h>
#include <common.h>
#include <errno.h>
#include <linux/libfdt.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <linux/hdmi.h>
#include <linux/list.h>
#include <linux/compat.h>
#include <linux/media-bus-format.h>
#include <malloc.h>
#include <memalign.h>
#include <video.h>
#include <video_rockchip.h>
#include <video_bridge.h>
#include <dm/device.h>
#include <dm/uclass-internal.h>
#include <asm/arch-rockchip/resource_img.h>
#include <asm/arch-rockchip/cpu.h>

#include "bmp_helper.h"
#include "libnsbmp.h"
#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "rockchip_bridge.h"
#include "rockchip_phy.h"
#include "rockchip_panel.h"
#include <dm.h>
#include <dm/of_access.h>
#include <dm/ofnode.h>
#include <asm/io.h>

// PRQA S 3101 ++
// PRQA S 5118 ++
// PRQA S 5124 ++
// PRQA S 5125 ++
// PRQA S 5129 ++

#define DRIVER_VERSION	"v1.0.1"

/***********************************************************************
 *  Rockchip UBOOT DRM driver version
 *
 *  v1.0.0	: add basic version for rockchip drm driver(hjc)
 *  v1.0.1	: add much dsi update(hjc)
 *
 **********************************************************************/

#define RK_BLK_SIZE 512
#define BMP_PROCESSED_FLAG 8399
#define BYTES_PER_PIXEL sizeof(uint32_t)
#define MAX_IMAGE_BYTES (8U * 1024U * 1024U)

DECLARE_GLOBAL_DATA_PTR;
static LIST_HEAD(rockchip_display_list);
static LIST_HEAD(logo_cache_list);

static unsigned long memory_start;
static unsigned long cubic_lut_memory_start;
static unsigned long memory_end;
static struct base2_info base_parameter;
static u32 align_size = PAGE_SIZE;

/*
 * the phy types are used by different connectors in public.
 * The current version only has inno hdmi phy for hdmi and tve.
 */
enum public_use_phy {
	NONE,
	INNO_HDMI_PHY
};

/* save public phy data */
struct public_phy_data {
	struct rockchip_phy *phy_drv;
	int phy_node;
	int public_phy_type;
	bool phy_init;
};

char* rockchip_get_output_if_name(u32 output_if, char *name)
{
	if ((output_if & VOP_OUTPUT_IF_RGB) != 0U) {
		(void)strcat(name, " RGB");
	}
	if ((output_if & VOP_OUTPUT_IF_BT1120) != 0U) {
		(void)strcat(name, " BT1120");
	}
	if ((output_if & VOP_OUTPUT_IF_BT656) != 0U) {
		(void)strcat(name, " BT656");
	}
	if ((output_if & VOP_OUTPUT_IF_LVDS0) != 0U) {
		(void)strcat(name, " LVDS0");
	}
	if ((output_if & VOP_OUTPUT_IF_LVDS1) != 0U) {
		(void)strcat(name, " LVDS1");
	}
	if ((output_if & VOP_OUTPUT_IF_MIPI0) != 0U) {
		(void)strcat(name, " MIPI0");
	}
	if ((output_if & VOP_OUTPUT_IF_MIPI1) != 0U) {
		(void)strcat(name, " MIPI1");
	}
	if ((output_if & VOP_OUTPUT_IF_eDP0) != 0U) {
		(void)strcat(name, " eDP0");
	}
	if ((output_if & VOP_OUTPUT_IF_eDP1) != 0U) {
		(void)strcat(name, " eDP1");
	}
	if ((output_if & VOP_OUTPUT_IF_DP0) != 0U) {
		(void)strcat(name, " DP0");
	}
	if ((output_if & VOP_OUTPUT_IF_DP1) != 0U) {
		(void)strcat(name, " DP1");
	}
	if ((output_if & VOP_OUTPUT_IF_HDMI0) != 0U) {
		(void)strcat(name, " HDMI0");
	}
	if ((output_if & VOP_OUTPUT_IF_HDMI1) != 0U) {
		(void)strcat(name, " HDMI1");
	}

	return name;
}

uint32_t rockchip_drm_get_cycles_per_pixel(uint32_t bus_format)
{
	uint32_t ret;

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
		ret = 1;
		break;
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
	case MEDIA_BUS_FMT_BGR565_2X8_LE:
		ret = 2;
		break;
	case MEDIA_BUS_FMT_RGB666_3X6:
	case MEDIA_BUS_FMT_RGB888_3X8:
	case MEDIA_BUS_FMT_BGR888_3X8:
		ret = 3;
		break;
	case MEDIA_BUS_FMT_RGB888_DUMMY_4X8:
	case MEDIA_BUS_FMT_BGR888_DUMMY_4X8:
		ret = 4;
		break;
	default:
		ret = 1;
		break;
	}

	return ret;
}

static int rockchip_get_baseparameter(void)
{
	struct blk_desc *dev_desc;
	disk_partition_t part_info;
	u32 block_num;
	struct base2_info *baseparameter_buf;
	int ret;

	dev_desc = rockchip_get_bootdev();
	if (!dev_desc) {
		(void)printf("%s: Could not find device\n", __func__);
		return -ENOENT;
	}

	if (part_get_info_by_name(dev_desc, "baseparameter", &part_info) < 0) {
		(void)printf("Could not find baseparameter partition\n");
		return -ENOENT;
	}

	block_num = (u32)BLOCK_CNT((sizeof(base_parameter)), (dev_desc));
	baseparameter_buf = memalign(ARCH_DMA_MINALIGN, block_num * dev_desc->blksz);
	if (!baseparameter_buf) {
		(void)printf("failed to alloc memory for baseparameter buffer\n");
		return -ENOMEM;
	}

	ret = (int)blk_dread(dev_desc, part_info.start, block_num, (void *)baseparameter_buf);
	if (ret < 0) {
		(void)printf("read baseparameter failed\n");
		goto out;
	}

	(void)memcpy(&base_parameter, baseparameter_buf, sizeof(base_parameter));
	if (strncasecmp(base_parameter.head_flag, "BASP", 4) != 0) {
		(void)printf("warning: bad baseparameter\n");
		(void)memset(&base_parameter, 0, sizeof(base_parameter));
	}
	rockchip_display_make_crc32_table();

out:
	return ret;
}

struct base2_disp_info *rockchip_get_disp_info(int type, int id)
{
	struct base2_disp_info *disp_info;
	struct base2_disp_header *disp_header;
	int offset = -1;
	u32 i;
	u32 crc_val;
	u32 base2_length;
	void *base_parameter_addr = (void *)&base_parameter;

	for (i = 0; i < 8U; i++) {
		disp_header = &base_parameter.disp_header[i];
		if (disp_header->connector_type == (u32)type &&
		    disp_header->connector_id == (u32)id) {
			(void)printf("disp info %d, type:%d, id:%d\n", i, type, id);
			offset = (int)disp_header->offset;
			break;
		}
	}

	if (offset < 0) {
		return NULL;
	}
	disp_info = (struct base2_disp_info *)(base_parameter_addr + offset);
	if (disp_info->screen_info[0].type != (u32)type ||
	    disp_info->screen_info[0].id != (u32)id) {
		(void)printf("base2_disp_info couldn't be found, screen_info type[%d] or id[%d] mismatched\n",
		       disp_info->screen_info[0].type,
		       disp_info->screen_info[0].id);
		return NULL;
	}

	if (strncasecmp(disp_info->disp_head_flag, "DISP", 4) != 0) {
		return NULL;
	}

	if (base_parameter.major_version == 3U && base_parameter.minor_version == 0U) {
		crc_val = rockchip_display_crc32c_cal((unsigned char *)disp_info,
						      (int)sizeof(struct base2_disp_info));
		if (crc_val != disp_info->crc2) {
			(void)printf("error: connector type[%d], id[%d] disp info crc2 check error\n",
			       type, id);
			return NULL;
		}
	} else {
		base2_length = (u32)sizeof(struct base2_disp_info) - (u32)sizeof(struct csc_info) -
			       (u32)sizeof(struct acm_data) - 10U * 1024U - 4U;
		crc_val = rockchip_display_crc32c_cal((unsigned char *)disp_info, (int)base2_length - 4);
		if (crc_val != disp_info->crc) {
			(void)printf("error: connector type[%d], id[%d] disp info crc check error\n",
			       type, id);
			return NULL;
		}
	}

	return disp_info;
}

/* check which kind of public phy does connector use */
static int check_public_use_phy(struct rockchip_connector *conn)
{
	int ret = (int)NONE;
#ifdef CONFIG_ROCKCHIP_INNO_HDMI_PHY

	if (!strncmp(dev_read_name(conn->dev), "tve", 3) ||
	    !strncmp(dev_read_name(conn->dev), "hdmi", 4)) {
		ret = (int)INNO_HDMI_PHY;
	}
#endif

	return ret;
}

#if defined(CONFIG_ROCKCHIP_RK3328) || defined(CONFIG_ROCKCHIP_RK322X)
/*
 * get public phy driver and initialize it.
 * The current version only has inno hdmi phy for hdmi and tve.
 */
static int get_public_phy(struct rockchip_connector *conn,
			  struct public_phy_data *data)
{
	struct rockchip_phy *phy;
	struct udevice *dev;
	int ret;

	switch (data->public_phy_type) {
	case (int)INNO_HDMI_PHY:
#if defined(CONFIG_ROCKCHIP_RK3328)
		ret = uclass_get_device_by_name(UCLASS_PHY,
						"hdmiphy@ff430000", &dev);
#elif defined(CONFIG_ROCKCHIP_RK322X)
		ret = uclass_get_device_by_name(UCLASS_PHY,
						"hdmi-phy@12030000", &dev);
#else
		ret = -EINVAL;
#endif
		if (ret != 0) {
			(void)printf("Warn: can't find phy driver\n");
			return 0;
		}

		phy = (struct rockchip_phy *)dev_get_driver_data(dev);
		if (phy == NULL) {
			(void)printf("failed to get phy driver\n");
			return 0;
		}

		ret = rockchip_phy_init(phy);
		if (ret != 0) {
			(void)printf("failed to init phy driver\n");
			return ret;
		}
		conn->phy = phy;

		(void)printf("inno hdmi phy init success, save it\n");
		data->phy_drv = conn->phy;
		data->phy_init = true;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
#else
static int get_public_phy(struct rockchip_connector *conn,
			  struct public_phy_data *data)
{
	return -EINVAL;
}
#endif

static void init_display_buffer(ulong base)
{
	memory_start = ALIGN((base + DRM_ROCKCHIP_FB_SIZE), (align_size));
	memory_end = memory_start;
	cubic_lut_memory_start = ALIGN((memory_start + MEMORY_POOL_SIZE), (align_size));
}

static void *get_display_buffer(int size)
{
	uintptr_t roundup_memory = roundup((memory_end), (PAGE_SIZE));
	void *buf;

	if (roundup_memory + (uintptr_t)size > memory_start + (uintptr_t)MEMORY_POOL_SIZE) {
		(void)printf("failed to alloc %dbyte memory to display\n", size);
		return NULL;
	}
	buf = (void *)roundup_memory;

	memory_end = roundup_memory + (ulong)size;

	return buf;
}

static unsigned long get_display_size(void)
{
	return memory_end - memory_start;
}

static unsigned long get_single_cubic_lut_size(void)
{
	ulong cubic_lut_size;
	ulong cubic_lut_step = CONFIG_ROCKCHIP_CUBIC_LUT_SIZE;

	/* This is depend on IC designed */
	cubic_lut_size = (cubic_lut_step * cubic_lut_step * cubic_lut_step + 1UL) / 2UL * 16UL;
	cubic_lut_size = roundup((cubic_lut_size), (PAGE_SIZE));

	return cubic_lut_size;
}

static unsigned long get_cubic_lut_offset(int crtc_id)
{
	return (ulong)crtc_id * get_single_cubic_lut_size();
}

unsigned long get_cubic_lut_buffer(int crtc_id)
{
	return cubic_lut_memory_start + (ulong)crtc_id * get_single_cubic_lut_size();
}

static unsigned long get_cubic_memory_size(void)
{
	/* Max support 4 cubic lut */
	return get_single_cubic_lut_size() * 4UL;
}

static int connector_phy_init(struct rockchip_connector *conn,
			      struct public_phy_data *data)
{
	int type;

	/* does this connector use public phy with others */
	type = check_public_use_phy(conn);
	if (type == (int)INNO_HDMI_PHY) {
		/* there is no public phy was initialized */
		if (!data->phy_init) {
			(void)printf("start get public phy\n");
			data->public_phy_type = type;
			if (get_public_phy(conn, data) != 0) {
				(void)printf("can't find correct public phy type\n");
				free(data);
				return -EINVAL;
			}
			return 0;
		}

		/* if this phy has been initialized, get it directly */
		conn->phy = (struct rockchip_phy *)data->phy_drv;
		return 0;
	}

	return 0;
}

int rockchip_ofnode_get_display_mode(ofnode node, struct drm_display_mode *mode, u32 *bus_flags)
{
	int hactive, vactive, pixelclock;
	int hfront_porch, hback_porch, hsync_len;
	int vfront_porch, vback_porch, vsync_len;
	int val;
	bool tmp;
	u32 flags;

#define FDT_GET_BOOL(val, name) \
	val = ofnode_read_bool(node, name);

#define FDT_GET_INT(val, name) \
	val = ofnode_read_s32_default(node, name, -1); \
	if (val < 0) { \
		(void)printf("Can't get %s\n", name); \
		return -ENXIO; \
	}

#define FDT_GET_INT_DEFAULT(val, name, default) \
	val = ofnode_read_s32_default(node, name, default);

	FDT_GET_INT((hactive), ("hactive"));
	FDT_GET_INT((vactive), ("vactive"));
	FDT_GET_INT((pixelclock), ("clock-frequency"));
	FDT_GET_INT((hsync_len), ("hsync-len"));
	FDT_GET_INT((hfront_porch), ("hfront-porch"));
	FDT_GET_INT((hback_porch), ("hback-porch"));
	FDT_GET_INT((vsync_len), ("vsync-len"));
	FDT_GET_INT((vfront_porch), ("vfront-porch"));
	FDT_GET_INT((vback_porch), ("vback-porch"));
	FDT_GET_INT((val), ("hsync-active"));
	flags = val != 0 ? (u32)DRM_MODE_FLAG_PHSYNC : (u32)DRM_MODE_FLAG_NHSYNC;
	FDT_GET_INT((val), ("vsync-active"));
	flags |= val != 0 ? (u32)DRM_MODE_FLAG_PVSYNC : (u32)DRM_MODE_FLAG_NVSYNC;

	FDT_GET_BOOL((tmp), ("interlaced"));
	flags |= tmp ? (u32)DRM_MODE_FLAG_INTERLACE : 0U;
	FDT_GET_BOOL((tmp), ("doublescan"));
	flags |= tmp ? (u32)DRM_MODE_FLAG_DBLSCAN : 0U;
	FDT_GET_BOOL((tmp), ("doubleclk"));
	flags |= tmp ? (u32)DRM_MODE_FLAG_DBLCLK : 0U;

	FDT_GET_INT((val), ("de-active"));
	*bus_flags |= val != 0 ? (u32)DRM_BUS_FLAG_DE_HIGH : (u32)DRM_BUS_FLAG_DE_LOW;
	FDT_GET_INT((val), ("pixelclk-active"));
	*bus_flags |= val != 0 ? (u32)DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE : (u32)DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;

	FDT_GET_INT_DEFAULT((val), ("screen-rotate"), (0));
	if (val == (int)DRM_MODE_FLAG_XMIRROR) {
		flags |= (u32)DRM_MODE_FLAG_XMIRROR;
	} else if (val == (int)DRM_MODE_FLAG_YMIRROR) {
		flags |= (u32)DRM_MODE_FLAG_YMIRROR;
	} else if (val == (int)DRM_MODE_FLAG_XYMIRROR) {
		flags |= (u32)DRM_MODE_FLAG_XMIRROR;
		flags |= (u32)DRM_MODE_FLAG_YMIRROR;
	} else {
		/* Do nothing */
	}
	mode->hdisplay = hactive;
	mode->hsync_start = mode->hdisplay + hfront_porch;
	mode->hsync_end = mode->hsync_start + hsync_len;
	mode->htotal = mode->hsync_end + hback_porch;

	mode->vdisplay = vactive;
	mode->vsync_start = mode->vdisplay + vfront_porch;
	mode->vsync_end = mode->vsync_start + vsync_len;
	mode->vtotal = mode->vsync_end + vback_porch;

	mode->clock = pixelclock / 1000;
	mode->flags = flags;
	mode->vrefresh = drm_mode_vrefresh(mode);

	return 0;
}

static int display_get_force_timing_from_dts(ofnode node,
					     struct drm_display_mode *mode,
					     u32 *bus_flags)
{
	int ret;

	ret = rockchip_ofnode_get_display_mode(node, mode, bus_flags);

	if (ret != 0) {
		mode->clock = 74250;
		mode->flags = 0x5;
		mode->hdisplay = 1280;
		mode->hsync_start = 1390;
		mode->hsync_end = 1430;
		mode->htotal = 1650;
		mode->hskew = 0;
		mode->vdisplay = 720;
		mode->vsync_start = 725;
		mode->vsync_end = 730;
		mode->vtotal = 750;
		mode->vrefresh = 60;
		mode->picture_aspect_ratio = (int)HDMI_PICTURE_ASPECT_16_9;
		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	}

	(void)printf("route node %s force_timing, use %dx%dp%d as default mode\n",
	       ret != 0 ? "undefine" : "define", mode->hdisplay, mode->vdisplay,
	       mode->vscan);

	return 0;
}

static int display_get_timing_from_dts(struct rockchip_panel *panel,
				       struct drm_display_mode *mode,
				       u32 *bus_flags)
{
	struct ofnode_phandle_args args;
	ofnode dt, timing, mcu_panel;
	int ret;

	mcu_panel = dev_read_subnode(panel->dev, "mcu-panel");
	dt = dev_read_subnode(panel->dev, "display-timings");
	if (ofnode_valid(dt)) {
		ret = ofnode_parse_phandle_with_args(dt, "native-mode", NULL,
						     0, 0, &args);
		if (ret != 0) {
			return ret;
		}

		timing = args.node;
	} else if (ofnode_valid(mcu_panel)) {
		dt = ofnode_find_subnode(mcu_panel, "display-timings");
		ret = ofnode_parse_phandle_with_args(dt, "native-mode", NULL,
						     0, 0, &args);
		if (ret != 0) {
			return ret;
		}		

		timing = args.node;
	} else {
		timing = dev_read_subnode(panel->dev, "panel-timing");
	}

	if (!ofnode_valid(timing)) {
		(void)printf("failed to get display timings from DT\n");
		return -ENXIO;
	}

	(void)rockchip_ofnode_get_display_mode(timing, mode, bus_flags);

	return 0;
}

static int display_get_timing(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;
	const struct drm_display_mode *m;
	struct rockchip_panel *panel = conn_state->connector->panel;
	int ret;

	if (panel->funcs->get_mode != NULL) {
		return panel->funcs->get_mode(panel, mode);
	}

	ret = display_get_timing_from_dts(panel, mode, &conn_state->bus_flags);
	if (dev_of_valid(panel->dev) && ret == 0) {
		(void)printf("Using display timing dts\n");
		return 0;
	}

	if (panel->data) {
		m = (const struct drm_display_mode *)panel->data;
		(void)memcpy(mode, m, sizeof(*m));
		(void)printf("Using display timing from compatible panel driver\n");
		return 0;
	}

	return -ENODEV;
}

static int display_pre_init(void)
{
	struct display_state *state;
	int ret = 0;

	list_for_each_entry((state), (&rockchip_display_list), head) {
		struct connector_state *conn_state = &state->conn_state;
		struct crtc_state *crtc_state = &state->crtc_state;
		struct rockchip_crtc *crtc = crtc_state->crtc;

		if (crtc == NULL) {
			continue;
		}

		if (crtc_state->crtc_id > 3) {
			continue;
		}

		ret = rockchip_connector_pre_init(state);
		if (ret != 0) {
			(void)printf("pre init conn error\n");
		}

		crtc->vps[crtc_state->crtc_id].output_type = conn_state->type;
	}
	return ret;
}

static int display_use_force_mode(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;

	conn_state->bpc = 8;
	(void)memcpy(mode, &state->force_mode, sizeof(struct drm_display_mode));
	conn_state->bus_format = (int)state->force_bus_format;

	return 0;
}

static int display_get_edid_mode(struct display_state *state)
{
	int ret;
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;
	int bpc;

	ret = edid_get_drm_mode(conn_state->edid, (int)sizeof(conn_state->edid), mode, &bpc);
	if (ret == 0) {
		conn_state->bpc = (u32)bpc;
		edid_print_info((void *)&conn_state->edid);
	} else {
		conn_state->bpc = 8;
		mode->clock = 74250;
		mode->flags = 0x5;
		mode->hdisplay = 1280;
		mode->hsync_start = 1390;
		mode->hsync_end = 1430;
		mode->htotal = 1650;
		mode->hskew = 0;
		mode->vdisplay = 720;
		mode->vsync_start = 725;
		mode->vsync_end = 730;
		mode->vtotal = 750;
		mode->vrefresh = 60;
		mode->picture_aspect_ratio = (int)HDMI_PICTURE_ASPECT_16_9;
		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

		(void)printf("error: %s get mode from edid failed, use 720p60 as default mode\n",
		       state->conn_state.connector->dev->name);
	}

	return ret;
}

static int display_mode_valid(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct rockchip_connector *conn = conn_state->connector;
	const struct rockchip_connector_funcs *conn_funcs = conn->funcs;
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	int ret;

	if (conn_funcs->mode_valid != NULL && state->enabled_at_spl == false) {
		ret = conn_funcs->mode_valid(conn, state);
		if (ret != 0) {
			return ret;
		}
	}

	if (crtc_funcs->mode_valid != NULL) {
		ret = crtc_funcs->mode_valid(state);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int display_mode_fixup(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	int ret;

	if (crtc_funcs->mode_fixup != NULL) {
		ret = crtc_funcs->mode_fixup(state);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int display_init(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct rockchip_connector *conn = conn_state->connector;
	struct crtc_state *crtc_state = &state->crtc_state;
	struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	struct drm_display_mode *mode = &conn_state->mode;
	const char *compatible;
	int ret = 0;
	int cmp;
	bool check;
	static bool init_onece = false;
#ifdef CONFIG_SPL_BUILD
	struct spl_display_info *spl_disp_info = (struct spl_display_info *)CONFIG_SPL_VIDEO_BUF;
#endif
	if (!init_onece) {
		init_onece = true;
		(void)printf("Rockchip UBOOT DRM driver version: %s\n", DRIVER_VERSION);
	}

	if (state->is_init) {
		return 0;
	}

	if (!crtc_funcs) {
		(void)printf("failed to find crtc functions\n");
		return -ENXIO;
	}

#ifdef CONFIG_SPL_BUILD
	if (state->conn_state.type == DRM_MODE_CONNECTOR_HDMIA)
		state->enabled_at_spl = spl_disp_info->enabled == 1 ? true : false;
	if (state->enabled_at_spl)
		(void)printf("HDMI enabled at SPL\n");
#endif
	check = drm_mode_equal(&crtc_state->crtc->active_mode, &conn_state->mode);
	if (crtc_state->crtc->active && crtc_state->ports_node == NULL && check) {
		(void)printf("%s has been used for output type: %d, mode: %dx%dp%d\n",
			crtc_state->dev->name,
			crtc_state->crtc->active_mode.type,
			crtc_state->crtc->active_mode.hdisplay,
			crtc_state->crtc->active_mode.vdisplay,
			crtc_state->crtc->active_mode.vrefresh);
		return -ENODEV;
	}

	if (crtc_funcs->preinit != NULL) {
		ret = crtc_funcs->preinit(state);
		if (ret != 0) {
			return ret;
		}
	}

	if (state->enabled_at_spl == false) {
		ret = rockchip_connector_init(state);
		if (ret != 0) {
			goto deinit;
		}
	}

	/*
	 * support hotplug, but not connect;
	 */
#ifdef CONFIG_DRM_ROCKCHIP_TVE
	if (crtc->hdmi_hpd && conn_state->type == DRM_MODE_CONNECTOR_TV) {
		(void)printf("hdmi plugin ,skip tve\n");
		goto deinit;
	}
#elif defined(CONFIG_DRM_ROCKCHIP_RK1000)
	if (crtc->hdmi_hpd && conn_state->type == DRM_MODE_CONNECTOR_LVDS) {
		(void)printf("hdmi plugin ,skip tve\n");
		goto deinit;
	}
#endif

	check = rockchip_connector_detect(state);
#if defined(CONFIG_DRM_ROCKCHIP_TVE) || defined(CONFIG_DRM_ROCKCHIP_RK1000)
	if (conn_state->type == DRM_MODE_CONNECTOR_HDMIA)
		crtc->hdmi_hpd = ret;
	if (state->enabled_at_spl)
		crtc->hdmi_hpd = true;
#endif
	if (!check && !state->force_output) {
		goto deinit;
	}

	ret = 0;
	if (state->enabled_at_spl == true) {
#ifdef CONFIG_SPL_BUILD
		struct drm_display_mode *mode = &conn_state->mode;

		(void)memcpy(mode, &spl_disp_info->mode,  sizeof(*mode));
		conn_state->bus_format = spl_disp_info->bus_format;

		(void)printf("%s get display mode from spl:%dx%d, bus format:0x%x\n",
			conn->dev->name, mode->hdisplay, mode->vdisplay, conn_state->bus_format);
#endif
	} else if (conn->panel != NULL) {
		ret = display_get_timing(state);
		if (ret == 0) {
			conn_state->bpc = conn->panel->bpc;
		}
#if defined(CONFIG_I2C_EDID)
		if (ret < 0 && conn->funcs->get_edid != NULL) {
			rockchip_panel_prepare(conn->panel);
			ret = conn->funcs->get_edid(conn, state);
			if (ret == 0) {
				(void)display_get_edid_mode(state);
			}
		}
#endif
	} else if (conn->bridge != NULL) {
		ret = video_bridge_read_edid(conn->bridge->dev,
					     conn_state->edid, EDID_SIZE);
		if (ret > 0) {
#if defined(CONFIG_I2C_EDID)
			ret = display_get_edid_mode(state);
#endif
		} else {
			ret = video_bridge_get_timing(conn->bridge->dev);
		}
	} else if (conn->funcs->get_timing != NULL) {
		ret = conn->funcs->get_timing(conn, state);
	} else if (conn->funcs->get_edid != NULL) {
		ret = conn->funcs->get_edid(conn, state);
#if defined(CONFIG_I2C_EDID)
		if (ret == 0) {
			(void)display_get_edid_mode(state);
		}
#endif
	} else {
		/* Do nothing */
	}

	if (ret == 0 && conn_state->secondary != NULL) {
		struct rockchip_connector *connector = conn_state->secondary;

		if (connector->panel != NULL) {
			if (connector->panel->funcs->get_mode != NULL) {
				struct drm_display_mode *_mode = drm_mode_create();

				ret = connector->panel->funcs->get_mode(connector->panel, _mode);
				check = drm_mode_equal(_mode, mode);
				if (ret == 0 && !check) {
					ret = -EINVAL;
				}

				drm_mode_destroy(_mode);
			}
		}
	}

	if (ret != 0 && !state->force_output) {
		goto deinit;
	}
	if (state->force_output) {
		(void)display_use_force_mode(state);
	}

	if (display_mode_valid(state) != 0) {
		goto deinit;
	}

	/* rk356x series drive mipi pixdata on posedge */
	compatible = dev_read_string(conn->dev, "compatible");
	if (compatible != NULL) {
		cmp = strcmp(compatible, "rockchip,rk3568-mipi-dsi");
		if (compatible && cmp == 0) {
			conn_state->bus_flags &= ~((u32)DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE);
			conn_state->bus_flags |= (u32)DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;
		}
	}

	if (display_mode_fixup(state) != 0) {
		goto deinit;
	}

	if (conn->bridge != NULL) {
		rockchip_bridge_mode_set(conn->bridge, &conn_state->mode);
	}

	(void)printf("%s: %s detailed mode clock %u kHz, flags[%x]\n"
	       "    H: %04d %04d %04d %04d\n"
	       "    V: %04d %04d %04d %04d\n"
	       "bus_format: %x\n",
	       conn->dev->name,
	       state->force_output ? "use force output" : "",
	       mode->clock, mode->flags,
	       mode->hdisplay, mode->hsync_start,
	       mode->hsync_end, mode->htotal,
	       mode->vdisplay, mode->vsync_start,
	       mode->vsync_end, mode->vtotal,
	       conn_state->bus_format);

	if (crtc_funcs->init != NULL && state->enabled_at_spl == false) {
		ret = crtc_funcs->init(state);
		if (ret != 0) {
			goto deinit;
		}
	}
	state->is_init = true;

	crtc_state->crtc->active = true;
	(void)memcpy(&crtc_state->crtc->active_mode,
	       &conn_state->mode, sizeof(struct drm_display_mode));

	return 0;

deinit:
	(void)rockchip_connector_deinit(state);
	return ret;
}

int display_send_mcu_cmd(struct display_state *state, u32 type, u32 val)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	int ret;

	if (!state->is_init) {
		return -EINVAL;
	}

	if (crtc_funcs->send_mcu_cmd != NULL) {
		ret = crtc_funcs->send_mcu_cmd(state, type, val);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int display_set_plane(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	int ret;

	if (!state->is_init) {
		return -EINVAL;
	}

	if (crtc_funcs->set_plane != NULL) {
		ret = crtc_funcs->set_plane(state);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int display_enable(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;

	if (!state->is_init) {
		return -EINVAL;
	}

	if (state->is_enable) {
		return 0;
	}

	if (crtc_funcs->prepare != NULL) {
		crtc_funcs->prepare(state);
	}

	if (state->enabled_at_spl == false) {
		(void)rockchip_connector_pre_enable(state);
	}

	if (crtc_funcs->enable != NULL) {
		crtc_funcs->enable(state);
	}

	if (state->enabled_at_spl == false) {
		(void)rockchip_connector_enable(state);
	}

	if (crtc_funcs->post_enable != NULL) {
		crtc_funcs->post_enable(state);
	}

#ifdef CONFIG_DRM_ROCKCHIP_RK628
	/*
	 * trigger .probe helper of U_BOOT_DRIVER(rk628) in ./rk628/rk628.c
	 */
	struct udevice * dev;
	int phandle, ret;

	phandle = ofnode_read_u32_default(state->node, "bridge", -1);
	if (phandle < 0) {
		(void)printf("%s failed to find bridge phandle\n", ofnode_get_name(state->node));
	}

	ret = uclass_get_device_by_phandle_id(UCLASS_I2C_GENERIC, phandle, &dev);
	if (ret && ret != -ENOENT) {
		(void)printf("%s:%d failed to get rk628 device ret:%d\n", __func__, __LINE__, ret);
	}

#endif

	if (crtc_state->soft_te) {
		crtc_funcs->apply_soft_te(state);
	}

	state->is_enable = true;

	return 0;
}

static int display_disable(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;

	if (!state->is_init) {
		return 0;
	}

	if (!state->is_enable) {
		return 0;
	}

	(void)rockchip_connector_disable(state);

	if (crtc_funcs->disable != NULL) {
		crtc_funcs->disable(state);
	}

	(void)rockchip_connector_post_disable(state);

	state->is_enable = false;
	state->is_init = false;

	return 0;
}

static int display_check(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct rockchip_connector *conn = conn_state->connector;
	const struct rockchip_connector_funcs *conn_funcs = conn->funcs;
	struct crtc_state *crtc_state = &state->crtc_state;
	const struct rockchip_crtc *crtc = crtc_state->crtc;
	const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
	int ret;

	if (!state->is_init) {
		return 0;
	}

	if (conn_funcs->check != NULL) {
		ret = conn_funcs->check(conn, state);
		if (ret != 0) {
			goto check_fail;
		}
	}

	if (crtc_funcs->check != NULL) {
		ret = crtc_funcs->check(state);
		if (ret != 0) {
			goto check_fail;
		}
	}

	if (crtc_funcs->plane_check != NULL) {
		ret = crtc_funcs->plane_check(state);
		if (ret != 0) {
			goto check_fail;
		}
	}

	return 0;

check_fail:
	state->is_init = false;
	return ret;
}

static int display_logo(struct display_state *state)
{
	struct crtc_state *crtc_state = &state->crtc_state;
	struct connector_state *conn_state = &state->conn_state;
	struct logo_info *logo = &state->logo;
	int hdisplay, vdisplay, ret;

	ret = display_init(state);
	if (!state->is_init || ret != 0) {
		return -ENODEV;
	}

	switch (logo->bpp) {
	case 16:
		crtc_state->format = ROCKCHIP_FMT_RGB565;
		break;
	case 24:
		crtc_state->format = ROCKCHIP_FMT_RGB888;
		break;
	case 32:
		crtc_state->format = ROCKCHIP_FMT_ARGB8888;
		break;
	default:
		(void)printf("can't support bmp bits[%d]\n", logo->bpp);
		ret = -EINVAL;
		break;
	}
	if (ret != 0) {
		return ret;
	}

	hdisplay = conn_state->mode.crtc_hdisplay;
	vdisplay = conn_state->mode.vdisplay;
	crtc_state->src_rect.w = (int)logo->width;
	crtc_state->src_rect.h = (int)logo->height;
	crtc_state->src_rect.x = 0;
	crtc_state->src_rect.y = 0;
	crtc_state->ymirror = logo->ymirror ? 1 : 0;
	crtc_state->rb_swap = 0;

	crtc_state->dma_addr = (u32)(unsigned long)logo->mem + logo->offset;
	crtc_state->xvir = ALIGN(((u32)crtc_state->src_rect.w * logo->bpp), 32U) >> 5U;

	if (state->logo_mode == ROCKCHIP_DISPLAY_FULLSCREEN) {
		crtc_state->crtc_rect.x = 0;
		crtc_state->crtc_rect.y = 0;
		crtc_state->crtc_rect.w = hdisplay;
		crtc_state->crtc_rect.h = vdisplay;
	} else {
		if (crtc_state->src_rect.w >= hdisplay) {
			crtc_state->crtc_rect.x = 0;
			crtc_state->crtc_rect.w = hdisplay;
		} else {
			crtc_state->crtc_rect.x = (hdisplay - crtc_state->src_rect.w) / 2;
			crtc_state->crtc_rect.w = crtc_state->src_rect.w;
		}

		if (crtc_state->src_rect.h >= vdisplay) {
			crtc_state->crtc_rect.y = 0;
			crtc_state->crtc_rect.h = vdisplay;
		} else {
			crtc_state->crtc_rect.y = (vdisplay - crtc_state->src_rect.h) / 2;
			crtc_state->crtc_rect.h = crtc_state->src_rect.h;
		}
	}

	(void)display_check(state);
	ret = display_set_plane(state);
	if (ret != 0) {
		return ret;
	}
	(void)display_enable(state);

	return 0;
}

static int get_crtc_id(ofnode connect, bool is_ports_node)
{
	struct device_node *port_node;
	struct device_node *remote;
	int handle;
	int val;

	if (is_ports_node) {
		port_node = of_get_parent(connect.np);
		if (!port_node) {
			goto err;
		}

		val = ofnode_read_u32_default(np_to_ofnode(port_node), "reg", U32_MAX);
		if (val < 0) {
			goto err;
		}
	} else {
		handle = ofnode_read_u32_default(connect, "remote-endpoint", U32_MAX);
		if (handle < 0) {
			goto err;
		}

		remote = of_find_node_by_phandle((phandle)handle);
		if (!remote) {
			goto err;
		}

		val = ofnode_read_u32_default(np_to_ofnode(remote), "reg", U32_MAX);
		if (val < 0) {
			goto err;
		}
	}

	return val;
err:
	(void)printf("Can't get crtc id, default set to id = 0\n");
	return 0;
}

static int get_crtc_mcu_mode(struct crtc_state *crtc_state, struct device_node *port_node,
			     bool is_ports_node)
{
	ofnode mcu_node, vp_node;
	int total_pixel, cs_pst, cs_pend, rw_pst, rw_pend;

	if (is_ports_node) {
		vp_node = np_to_ofnode(port_node);
		mcu_node = ofnode_find_subnode(vp_node, "mcu-timing");
		if (!ofnode_valid(mcu_node)) {
			return -ENODEV;
		}
	} else {
		mcu_node = dev_read_subnode(crtc_state->dev, "mcu-timing");
		if (!ofnode_valid(mcu_node)) {
			return -ENODEV;
		}
	}

#define FDT_GET_MCU_INT(val, name) \
	do { \
		val = ofnode_read_s32_default(mcu_node, name, -1); \
		if (val < 0) { \
			(void)printf("Can't get %s\n", name); \
			return -ENXIO; \
		} \
	} while (0)

	FDT_GET_MCU_INT((total_pixel), ("mcu-pix-total"));
	FDT_GET_MCU_INT((cs_pst), ("mcu-cs-pst"));
	FDT_GET_MCU_INT((cs_pend), ("mcu-cs-pend"));
	FDT_GET_MCU_INT((rw_pst), ("mcu-rw-pst"));
	FDT_GET_MCU_INT((rw_pend), ("mcu-rw-pend"));

	crtc_state->mcu_timing.mcu_pix_total = total_pixel;
	crtc_state->mcu_timing.mcu_cs_pst = cs_pst;
	crtc_state->mcu_timing.mcu_cs_pend = cs_pend;
	crtc_state->mcu_timing.mcu_rw_pst = rw_pst;
	crtc_state->mcu_timing.mcu_rw_pend = rw_pend;

	return 0;
}

static struct rockchip_logo_cache *find_or_alloc_logo_cache(const char *bmp, int rotate)
{
	struct rockchip_logo_cache *tmp, *logo_cache = NULL;

	list_for_each_entry((tmp), (&logo_cache_list), head) {
		if ((strncmp(tmp->name, bmp, 20) == 0 && rotate == tmp->logo_rotate) ||
		    (soc_is_rk3566() && tmp->logo_rotate == 1)) {
			logo_cache = tmp;
			break;
		}
	}

	if (!logo_cache) {
		logo_cache = malloc(sizeof(*logo_cache));
		if (!logo_cache) {
			(void)printf("failed to alloc memory for logo cache\n");
			return NULL;
		}
		(void)memset(logo_cache, 0, sizeof(*logo_cache));
		(void)strncpy(logo_cache->name, bmp, 20);
		INIT_LIST_HEAD(&logo_cache->head);
		list_add_tail(&logo_cache->head, &logo_cache_list);
	}

	return logo_cache;
}

/* Note: used only for rkfb kernel driver */
static int load_kernel_bmp_logo(struct logo_info *logo, const char *bmp_name)
{
#ifdef CONFIG_ROCKCHIP_RESOURCE_IMAGE
	void *dst;
	int len;
	u32 size;
	uintptr_t ptr;
	struct bmp_header *header;

	if (!logo || !bmp_name) {
		return -EINVAL;
	}

	header = malloc(RK_BLK_SIZE);
	if (!header) {
		return -ENOMEM;
	}

	len = rockchip_read_resource_file(header, bmp_name, 0, RK_BLK_SIZE);
	if (len != RK_BLK_SIZE) {
		free(header);
		return -EINVAL;
	}
	size = get_unaligned_le32(&header->file_size);
	ptr = memory_start + MEMORY_POOL_SIZE / 2UL;
	dst = (void *)(ptr);
	len = rockchip_read_resource_file(dst, bmp_name, 0, (int)size);
	if ((u32)len != size) {
		(void)printf("failed to load bmp %s\n", bmp_name);
		free(header);
		return -ENOENT;
	}

	logo->mem = dst;
	free(header);
#endif

	return 0;
}

static void *bitmap_create(int width, int height, unsigned int state)
{
	/* Ensure a stupidly large bitmap is not created */
	if (width > 4096 || height > 4096) {
		return NULL;
	}

	return calloc((ulong)width * (ulong)height, BYTES_PER_PIXEL);
}

static unsigned char *bitmap_get_buffer(void *bitmap)
{
	return bitmap;
}

static void bitmap_destroy(void *bitmap)
{
	free(bitmap);
}

static void bmp_copy(void *dst, bmp_image *bmp)
{
	u16 row, col;
	u8 *image;
	u8 *pdst = (u8 *)dst;

	image = (u8 *)bmp->bitmap;
	for (row = 0; row != bmp->height; row++) {
		for (col = 0; col != bmp->width; col++) {
			size_t z = ((size_t)row * (size_t)bmp->width + (size_t)col) * (size_t)BYTES_PER_PIXEL;

			*pdst = image[z + 2UL];
			pdst++;
			*pdst = image[z + 1UL];
			pdst++;
			*pdst = image[z];
			pdst++;
			*pdst = image[z + 3UL];
			pdst++;
		}
	}
}

static void *rockchip_logo_rotate(struct logo_info *logo, void *src)
{
	u32 width = (u32)logo->width;
	u32 height = (u32)logo->height;
	u32 width_rotate = (logo->height & 0x3U) != 0U ? (u32)(logo->height & ~0x3U) + 4U : logo->height;
	u32 height_rotate = logo->width;
	u32 dst_size = width * height * logo->bpp >> 3U;
	u32 dst_size_rotate = width_rotate * height_rotate * logo->bpp >> 3;
	u32 bytes_per_pixel = logo->bpp >> 3U;
	u32 padded_width;
	u32 i, j;
	char *src_data = (char *)src;
	char *dst_rotate;
	char *img_data;

	if (!(logo->rotate == 90 || logo->rotate == 180 || logo->rotate == 270)) {
		(void)printf("Unsupported rotation angle\n");
		return NULL;
	}

	img_data = (char *)malloc(dst_size);
	if (!img_data) {
		(void)printf("failed to alloc memory for image data\n");
		return NULL;
	}
	(void)memcpy(img_data, src_data, dst_size);

	dst_rotate = (char *)get_display_buffer((int)dst_size_rotate);
	if (!dst_rotate) {
		free(img_data);
		return NULL;
	}
	(void)memset(dst_rotate, 0, dst_size_rotate);

	switch (logo->rotate) {
	case 90:
		logo->width = width_rotate;
		logo->height = height_rotate;
		padded_width = (height & 0x3U) != 0U ? (height & ~0x3U) + 4U : height;
		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
				(void)memcpy(dst_rotate + (j * padded_width * bytes_per_pixel) +
					     (height - i - 1U) * bytes_per_pixel,
					     img_data + i * width * bytes_per_pixel + j * bytes_per_pixel,
					     bytes_per_pixel);
			}
		}
		break;
	case 180:
		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
				(void)memcpy(dst_rotate + (height - i - 1U) * width * bytes_per_pixel +
					     (width - j - 1U) * bytes_per_pixel,
					     img_data + i * width * bytes_per_pixel + j * bytes_per_pixel,
					     bytes_per_pixel);
			}
		}
		break;
	case 270:
		logo->width = width_rotate;
		logo->height = height_rotate;
		padded_width = (height & 0x3U) != 0U ? (height & ~0x3U) + 4U : height;
		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
				(void)memcpy(dst_rotate + (width - j - 1U) * padded_width * bytes_per_pixel +
					     i * bytes_per_pixel,
					     img_data + i * width * bytes_per_pixel + j * bytes_per_pixel,
					     bytes_per_pixel);
			}
		}
		break;
	default:
		(void)printf("Unsupported rotation angles: %d\n", logo->rotate);
		break;
	}

	free(img_data);

	return dst_rotate;
}

static int load_bmp_logo(struct logo_info *logo, const char *bmp_name)
{
#ifdef CONFIG_ROCKCHIP_RESOURCE_IMAGE
	struct rockchip_logo_cache *logo_cache;
	bmp_bitmap_callback_vt bitmap_callbacks = {
		bitmap_create,
		bitmap_destroy,
		bitmap_get_buffer,
	};
	bmp_result code;
	bmp_image bmp;
	void *bmp_data;
	void *dst;
	void *dst_rotate;
	int len;
	u32 dst_size;
	int ret = 0;

	if (!logo || !bmp_name) {
		return -EINVAL;
	}

	logo_cache = find_or_alloc_logo_cache(bmp_name, logo->rotate);
	if (!logo_cache) {
		return -ENOMEM;
	}

	if (logo_cache->logo.mem != NULL) {
		(void)memcpy(logo, &logo_cache->logo, sizeof(*logo));
		return 0;
	}

	bmp_data = malloc(MAX_IMAGE_BYTES);
	if (!bmp_data) {
		(void)printf("failed to alloc bmp data\n");
		return -ENOMEM;
	}

	(void)bmp_create(&bmp, &bitmap_callbacks);

	len = rockchip_read_resource_file(bmp_data, bmp_name, 0, (int)MAX_IMAGE_BYTES);
	if (len < 0) {
		ret = -EINVAL;
		goto free_bmp_data;
	}

	/* analyse the BMP */
	code = bmp_analyse(&bmp, (size_t)len, bmp_data);
	if (code != BMP_OK) {
		(void)printf("failed to parse bmp:%s header\n", bmp_name);
		ret = -EINVAL;
		goto free_bmp_data;
	}

	if (bmp.buffer_size > MAX_IMAGE_BYTES) {
		(void)printf("bmp[%s] data size[%dKB] is over the limitation MAX_IMAGE_BYTES[%dKB]\n",
			     bmp_name, bmp.buffer_size / 1024U, MAX_IMAGE_BYTES / 1024U);
		ret = -EINVAL;
		goto free_bmp_data;
	}

	/* fix bpp to 32 */
	logo->bpp = 32;
	logo->offset = 0;
	logo->ymirror = false;
	logo->width = get_unaligned_le32(&bmp.width);
	logo->height = get_unaligned_le32(&bmp.height);
	dst_size = logo->width * logo->height * logo->bpp >> 3;
	/* decode the image to RGBA8888 format */
	code = bmp_decode(&bmp);
	if (code != BMP_OK) {
		/* allow partially decoded images */
		if (code != BMP_INSUFFICIENT_DATA && code != BMP_DATA_ERROR) {
			(void)printf("failed to allocate the buffer of bmp:%s\n", bmp_name);
			ret = -EINVAL;
			goto free_bmp_data;
		}
	}

	dst = get_display_buffer((int)dst_size);
	if (!dst) {
		ret = -ENOMEM;
		goto free_bmp_data;
	}
	bmp_copy(dst, &bmp);

	if (logo->rotate != 0) {
		dst_rotate = rockchip_logo_rotate(logo, dst);
		if (dst_rotate) {
			dst = dst_rotate;
			dst_size = logo->width * logo->height * logo->bpp >> 3;
		}
		(void)printf("logo ratate %d\n", logo->rotate);
	}
	logo->mem = dst;

	(void)memcpy(&logo_cache->logo, logo, sizeof(*logo));
	logo_cache->logo_rotate = logo->rotate;

	flush_dcache_range((uintptr_t)dst, ALIGN(((uintptr_t)dst + (uintptr_t)dst_size), (CONFIG_SYS_CACHELINE_SIZE)));
free_bmp_data:
	/* clean up */
	bmp_finalise(&bmp);
	free(bmp_data);

	return ret;
#else
	return -EINVAL;
#endif
}

#ifdef CONFIG_DRM_ROCKCHIP_VIDEO_FRAMEBUFFER
void rockchip_show_fbbase(ulong fbbase)
{
	struct display_state *s;

	list_for_each_entry(s, &rockchip_display_list, head) {
		s->logo.mode = ROCKCHIP_DISPLAY_FULLSCREEN;
		s->logo.mem = (char *)fbbase;
		s->logo.width = DRM_ROCKCHIP_FB_WIDTH;
		s->logo.height = DRM_ROCKCHIP_FB_HEIGHT;
		s->logo.bpp = 32;
		s->logo.ymirror = 0;

		display_logo(s);
	}
}
#endif

int rockchip_show_bmp(const char *bmp)
{
	struct display_state *s;
	int ret = 0;

	if (!bmp) {
		list_for_each_entry((s), (&rockchip_display_list), head) {
			(void)display_disable(s);
		}
		return -ENOENT;
	}

	list_for_each_entry((s), (&rockchip_display_list), head) {
		s->logo.mode = (int)s->charge_logo_mode;
		if (load_bmp_logo(&s->logo, bmp) != 0) {
			continue;
		}
		ret = display_logo(s);
	}

	return ret;
}

int rockchip_show_logo(void)
{
	struct display_state *s;
	struct display_state *ms = NULL;
	int ret = 0;
	int count = 0;

	list_for_each_entry((s), (&rockchip_display_list), head) {
		s->logo.mode = (int)s->logo_mode;
		s->logo.rotate = s->logo_rotate;
		if (load_bmp_logo(&s->logo, s->ulogo_name) != 0) {
			(void)printf("failed to display uboot logo\n");
		} else {
			ret = display_logo(s);
			if (ret == -EAGAIN) {
				ms = s;
			}
		}
		/* Load kernel bmp in rockchip_display_fixup() later */
	}

	/*
	 * For rk3566, the mirror win must be enabled after the related
	 * source win. If error code is EAGAIN, the mirror win may be
	 * first enabled unexpectedly, and we will move the enabling process
	 * as follows.
	 */
	if (ms) {
		while (count < 5) {
			ret = display_logo(ms);
			if (ret != -EAGAIN) {
				break;
			}
			mdelay(10);
			count++;
		}
	}

	return ret;
}

static int rockchip_vop_dump(const char *cmd)
{
	struct display_state *state;
	struct crtc_state *crtc_state;
	struct rockchip_crtc *crtc;
	const struct rockchip_crtc_funcs *crtc_funcs;
	int ret = -EINVAL;
	int cmp_a, cmp_all;

	list_for_each_entry((state), (&rockchip_display_list), head) {
		if (!state->is_init) {
			continue;
		}
		crtc_state = &state->crtc_state;
		crtc = crtc_state->crtc;
		crtc_funcs = crtc->funcs;

		if (!cmd) {
			ret = crtc_funcs->active_regs_dump(state);
		} else {
			cmp_a = strncmp(cmd, "a", 1);
			cmp_all = strncmp(cmd, "all", 3);
			if (cmp_a == 0 || cmp_all == 0) {
				ret = crtc_funcs->regs_dump(state);
			}
		}
		if (ret == 0) {
			break;
		}
	}

	if (ret != 0) {
		ret = (int)CMD_RET_USAGE;
	}

	return ret;
}

enum {
	PORT_DIR_IN,
	PORT_DIR_OUT,
};

const struct device_node *rockchip_of_graph_get_port_by_id(ofnode node, int id)
{
	ofnode ports, port;
	u32 reg = U32_MAX;

	ports = ofnode_find_subnode(node, "ports");
	if (!ofnode_valid(ports)) {
		return NULL;
	}

	ofnode_for_each_subnode((port), (ports)) {
		if (ofnode_read_u32(port, "reg", &reg) != 0) {
			continue;
		}

		if (reg == (u32)id) {
			break;
		}
	}

	if (reg == (u32)id) {
		return ofnode_to_np(port);
	}

	return NULL;
}

static const struct device_node *rockchip_of_graph_get_port_parent(ofnode port)
{
	ofnode parent;
	int is_ports_node;

	parent = ofnode_get_parent(port);
	is_ports_node = strstr(ofnode_to_np(parent)->full_name, "ports") ? 1 : 0;
	if (is_ports_node != 0) {
		parent = ofnode_get_parent(parent);
	}

	return ofnode_to_np(parent);
}

const struct device_node *
rockchip_of_graph_get_endpoint_by_regs(ofnode node, int port, int endpoint)
{
	const struct device_node *port_node;
	ofnode ep;
	u32 reg;

	port_node = rockchip_of_graph_get_port_by_id(node, port);
	if (!port_node) {
		return NULL;
	}

	ofnode_for_each_subnode((ep), (np_to_ofnode(port_node))) {
		if (ofnode_read_u32(ep, "reg", &reg) != 0) {
			break;
		}
		if (reg == (u32)endpoint) {
			break;
		}
	}

	if (!ofnode_valid(ep)) {
		return NULL;
	}

	return ofnode_to_np(ep);
}

static const struct device_node *
rockchip_of_graph_get_remote_node(ofnode node, int port, int endpoint)
{
	const struct device_node *ep_node;
	ofnode ep;
	uint handle;

	ep_node = rockchip_of_graph_get_endpoint_by_regs(node, port, endpoint);
	if (!ep_node) {
		return NULL;
	}

	if (ofnode_read_u32(np_to_ofnode(ep_node), "remote-endpoint", &handle) != 0) {
		return NULL;
	}

	ep = ofnode_get_by_phandle((phandle)handle);
	if (!ofnode_valid(ep)) {
		return NULL;
	}

	return ofnode_to_np(ep);
}

static int rockchip_of_find_panel(struct udevice *dev, struct rockchip_panel **panel)
{
	const struct device_node *ep_node, *panel_node;
	ofnode panel_ofnode, port;
	struct udevice *panel_dev;
	int ret;
	bool panel_valid, panel_available;

	*panel = NULL;
	panel_ofnode = dev_read_subnode(dev, "panel");
	panel_valid = ofnode_valid(panel_ofnode);
	panel_available = ofnode_is_available(panel_ofnode);
	if (panel_valid && panel_available) {
		ret = uclass_get_device_by_ofnode(UCLASS_PANEL, panel_ofnode,
						  &panel_dev);
		if (ret == 0) {
			goto found;
		}
	}

	ep_node = rockchip_of_graph_get_remote_node(dev->node, PORT_DIR_OUT, 0);
	if (!ep_node) {
		return -ENODEV;
	}

	port = ofnode_get_parent(np_to_ofnode(ep_node));
	if (!ofnode_valid(port)) {
		return -ENODEV;
	}

	panel_node = rockchip_of_graph_get_port_parent(port);
	if (!panel_node) {
		return -ENODEV;
	}

	ret = uclass_get_device_by_ofnode(UCLASS_PANEL, np_to_ofnode(panel_node), &panel_dev);
	if (ret == 0) {
		goto found;
	}

	return -ENODEV;

found:
	*panel = (struct rockchip_panel *)dev_get_driver_data(panel_dev);
	return 0;
}

static int rockchip_of_find_bridge(struct udevice *dev, struct rockchip_bridge **bridge)
{
	const struct device_node *ep_node, *bridge_node;
	ofnode port;
	struct udevice *bridge_dev;
	int ret;

	ep_node = rockchip_of_graph_get_remote_node(dev->node, PORT_DIR_OUT, 0);
	if (!ep_node) {
		return -ENODEV;
	}

	port = ofnode_get_parent(np_to_ofnode(ep_node));
	if (!ofnode_valid(port)) {
		return -ENODEV;
	}

	bridge_node = rockchip_of_graph_get_port_parent(port);
	if (!bridge_node) {
		return -ENODEV;
	}

	ret = uclass_get_device_by_ofnode(UCLASS_VIDEO_BRIDGE, np_to_ofnode(bridge_node),
					  &bridge_dev);
	if (ret == 0) {
		goto found;
	}

	return -ENODEV;

found:
	*bridge = (struct rockchip_bridge *)dev_get_driver_data(bridge_dev);
	return 0;
}

static int rockchip_of_find_panel_or_bridge(struct udevice *dev, struct rockchip_panel **panel,
					    struct rockchip_bridge **bridge)
{
	int ret = 0;

	if (*panel != NULL) {
		return 0;
	}

	*panel = NULL;
	*bridge = NULL;

	if (panel) {
		ret  = rockchip_of_find_panel(dev, panel);
		if (ret == 0) {
			return 0;
		}
	}

	if (ret != 0) {
		ret = rockchip_of_find_bridge(dev, bridge);
		if (ret == 0) {
			ret = rockchip_of_find_panel_or_bridge((*bridge)->dev, panel,
							       &(*bridge)->next_bridge);
		}
	}

	return ret;
}

static struct rockchip_phy *rockchip_of_find_phy(struct udevice *dev)
{
	struct udevice *phy_dev;
	int ret;

	ret = uclass_get_device_by_phandle(UCLASS_PHY, dev, "phys", &phy_dev);
	if (ret != 0) {
		return NULL;
	}

	return (struct rockchip_phy *)dev_get_driver_data(phy_dev);
}

static struct udevice *rockchip_of_find_connector_device(ofnode endpoint)
{
	ofnode ep, port, ports, conn;
	uint handle;
	struct udevice *dev;
	int ret;
	bool of_node_valid, of_node_available;

	if (ofnode_read_u32(endpoint, "remote-endpoint", &handle) != 0) {
		return NULL;
	}

	ep = ofnode_get_by_phandle(handle);
	of_node_valid = ofnode_valid(ep);
	of_node_available = ofnode_is_available(ep);
	if (!of_node_valid || !of_node_available) {
		return NULL;
	}

	port = ofnode_get_parent(ep);
	if (!ofnode_valid(port)) {
		return NULL;
	}

	ports = ofnode_get_parent(port);
	if (!ofnode_valid(ports)) {
		return NULL;
	}

	conn = ofnode_get_parent(ports);
	of_node_valid = ofnode_valid(conn);
	of_node_available = ofnode_is_available(conn);
	if (!of_node_valid || !of_node_available) {
		return NULL;
	}

	ret = uclass_get_device_by_ofnode(UCLASS_DISPLAY, conn, &dev);
	if (ret != 0) {
		return NULL;
	}

	return dev;
}

static struct rockchip_connector *rockchip_of_get_connector(ofnode endpoint)
{
	struct rockchip_connector *conn;
	struct udevice *dev;
	int ret;

	dev = rockchip_of_find_connector_device(endpoint);
	if (!dev) {
		(void)printf("Warn: can't find connect driver\n");
		return NULL;
	}

	conn = get_rockchip_connector_by_device(dev);
	if (!conn) {
		return NULL;
	}
	ret = rockchip_of_find_panel_or_bridge(dev, &conn->panel, &conn->bridge);
	if (ret != 0) {
		(void)printf("Warn: no find panel or bridge\n");
	}

	conn->phy = rockchip_of_find_phy(dev);

	return conn;
}

static struct rockchip_connector *rockchip_get_split_connector(struct rockchip_connector *conn)
{
	const char *conn_name;
	struct device_node *split_node;
	struct udevice *split_dev;
	struct rockchip_connector *split_conn;
	int ret = 0;
	bool of_node_available;

	conn->split_mode = ofnode_read_bool(conn->dev->node, "split-mode");
	conn->dual_channel_mode = ofnode_read_bool(conn->dev->node, "dual-channel");
	if (!conn->split_mode && !conn->dual_channel_mode) {
		return NULL;
	}

	switch (conn->type) {
	case DRM_MODE_CONNECTOR_DisplayPort:
		conn_name = "dp";
		break;
	case DRM_MODE_CONNECTOR_eDP:
		conn_name = "edp";
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
		conn_name = "hdmi";
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		conn_name = "lvds";
		break;
	default:
		(void)printf("Unsupported connector type [%d] in split mode\n", conn->type);
		ret = -EINVAL;
		break;
	}

	if (ret != 0) {
		return NULL;
	}

	split_node = of_alias_get_dev(conn_name, conn->id == 0 ? 1 : 0);
	of_node_available = of_device_is_available(split_node);
	if (!split_node || !of_node_available) {
		return NULL;
	}

	ret = uclass_get_device_by_ofnode(UCLASS_DISPLAY, np_to_ofnode(split_node), &split_dev);
	if (ret != 0) {
		return NULL;
	}

	split_conn = get_rockchip_connector_by_device(split_dev);
	if (!split_conn) {
		return NULL;
	}
	ret = rockchip_of_find_panel_or_bridge(split_dev, &split_conn->panel, &split_conn->bridge);
	if (ret != 0) {
		(void)printf("Warn: no find panel or bridge\n");
	}

	split_conn->phy = rockchip_of_find_phy(split_dev);
	split_conn->split_mode = conn->split_mode;
	split_conn->dual_channel_mode = conn->dual_channel_mode;

	return split_conn;
}

static bool rockchip_get_display_path_status(ofnode endpoint)
{
	ofnode ep;
	uint handle;
	bool of_node_valid, of_node_available;

	if (ofnode_read_u32(endpoint, "remote-endpoint", &handle) != 0) {
		return false;
	}

	ep = ofnode_get_by_phandle(handle);
	of_node_valid = ofnode_valid(ep);
	of_node_available = ofnode_is_available(ep);
	if (!of_node_valid || !of_node_available) {
		return false;
	}

	return true;
}

#if defined(CONFIG_ROCKCHIP_RK3568)
static int rockchip_display_fixup_dts(void *blob)
{
	ofnode route_node, route_subnode, conn_ep, conn_port;
	const struct device_node *route_sub_devnode;
	const struct device_node *ep_node, *conn_ep_dev_node;
	u32 handle;
	int conn_ep_offset;
	const char *route_sub_path, *path;

	/* Don't go further if new variant after
	 * reading PMUGRF_SOC_CON15
	 */
	if ((readl(0xfdc20100) & GENMASK(15, 14))) {
		return 0;
	}

	route_node = ofnode_path("/display-subsystem/route");
	if (!ofnode_valid(route_node)) {
		return -EINVAL;
	}

	ofnode_for_each_subnode(route_subnode, route_node) {
		if (!ofnode_is_available(route_subnode)) {
			continue;
		}

		route_sub_devnode = ofnode_to_np(route_subnode);
		route_sub_path = route_sub_devnode->full_name;
		if (!strstr(ofnode_get_name(route_subnode), "dsi") &&
		    !strstr(ofnode_get_name(route_subnode), "edp")) {
			return 0;
		}

		handle = ofnode_read_u32_default(route_subnode, "connect", -1);
		if (handle < 0) {
			(void)printf("Warn: can't find connect node's handle\n");
			continue;
		}

		ep_node = of_find_node_by_phandle((phandle)handle);
		if (!ofnode_valid(np_to_ofnode(ep_node))) {
			(void)printf("Warn: can't find endpoint node from phandle\n");
			continue;
		}

		ofnode_read_u32(np_to_ofnode(ep_node), "remote-endpoint", &handle);
		conn_ep = ofnode_get_by_phandle(handle);
		if (!ofnode_valid(conn_ep) || !ofnode_is_available(conn_ep)) {
			return -ENODEV;
		}

		conn_port = ofnode_get_parent(conn_ep);
		if (!ofnode_valid(conn_port)) {
			return -ENODEV;
		}

		ofnode_for_each_subnode(conn_ep, conn_port) {
			conn_ep_dev_node = ofnode_to_np(conn_ep);
			path = conn_ep_dev_node->full_name;
			ofnode_read_u32(conn_ep, "remote-endpoint", &phandle);
			conn_ep_offset = fdt_path_offset(blob, path);

			if (!ofnode_is_available(conn_ep) &&
			    strstr(ofnode_get_name(conn_ep), "endpoint@0")) {
				do_fixup_by_path_u32(blob, route_sub_path,
						     "connect", phandle, 1);
				fdt_status_okay(blob, conn_ep_offset);

			} else if (ofnode_is_available(conn_ep) &&
				   strstr(ofnode_get_name(conn_ep), "endpoint@1")) {
				fdt_status_disabled(blob, conn_ep_offset);
			}
		}
	}

	return 0;
}
#endif

static int rockchip_display_probe(struct udevice *dev)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct video_uc_platdata *plat = dev_get_uclass_platdata(dev);
	const void *blob = gd->fdt_blob;
	int handle;
	struct udevice *crtc_dev;
	struct rockchip_crtc *crtc;
	struct rockchip_connector *conn, *split_conn;
	struct display_state *s;
	const char *name;
	int ret;
	ofnode node, route_node, timing_node;
	struct device_node *port_node, *vop_node, *ep_node, *port_parent_node;
	struct public_phy_data *data;
	bool is_ports_node = false;

#if defined(CONFIG_ROCKCHIP_RK3568)
	rockchip_display_fixup_dts((void *)blob);
#endif
	/* Before relocation we don't need to do anything */
	if ((gd->flags & (u32)GD_FLG_RELOC) == 0U) {
		return 0;
	}

	data = malloc(sizeof(struct public_phy_data));
	if (!data) {
		(void)printf("failed to alloc phy data\n");
		return -ENOMEM;
	}
	data->phy_init = false;

	init_display_buffer(plat->base);

	route_node = dev_read_subnode(dev, "route");
	if (!ofnode_valid(route_node)) {
		return -ENODEV;
	}

	ofnode_for_each_subnode((node), (route_node)) {
		if (!ofnode_is_available(node)) {
			continue;
		}
		handle = ofnode_read_u32_default(node, "connect", U32_MAX);
		if (handle < 0) {
			(void)printf("Warn: can't find connect node's handle\n");
			continue;
		}
		ep_node = of_find_node_by_phandle((phandle)handle);
		if (!ofnode_valid(np_to_ofnode(ep_node))) {
			(void)printf("Warn: can't find endpoint node from phandle\n");
			continue;
		}
		port_node = of_get_parent(ep_node);
		if (!ofnode_valid(np_to_ofnode(port_node))) {
			(void)printf("Warn: can't find port node from phandle\n");
			continue;
		}

		port_parent_node = of_get_parent(port_node);
		if (!ofnode_valid(np_to_ofnode(port_parent_node))) {
			(void)printf("Warn: can't find port parent node from phandle\n");
			continue;
		}

		is_ports_node = strstr(port_parent_node->full_name, "ports") != NULL ? true : false;
		if (is_ports_node) {
			vop_node = of_get_parent(port_parent_node);
			if (!ofnode_valid(np_to_ofnode(vop_node))) {
				(void)printf("Warn: can't find crtc node from phandle\n");
				continue;
			}
		} else {
			vop_node = port_parent_node;
		}

		ret = uclass_get_device_by_ofnode(UCLASS_VIDEO_CRTC,
						  np_to_ofnode(vop_node),
						  &crtc_dev);
		if (ret != 0) {
			(void)printf("Warn: can't find crtc driver %d\n", ret);
			continue;
		}
		crtc = (struct rockchip_crtc *)dev_get_driver_data(crtc_dev);

		conn = rockchip_of_get_connector(np_to_ofnode(ep_node));
		if (!conn) {
			(void)printf("Warn: can't get connect driver\n");
			continue;
		}
		split_conn = rockchip_get_split_connector(conn);

		s = malloc(sizeof(*s));
		if (!s) {
			continue;
		}

		(void)memset(s, 0, sizeof(*s));

		INIT_LIST_HEAD(&s->head);
		ret = ofnode_read_string_index(node, "logo,uboot", 0, &name);
		if (ret == 0) {
			(void)memcpy(s->ulogo_name, name, strlen(name));
		}
		ret = ofnode_read_string_index(node, "logo,kernel", 0, &name);
		if (ret == 0) {
			(void)memcpy(s->klogo_name, name, strlen(name));
		}
		ret = ofnode_read_string_index(node, "logo,mode", 0, &name);
		if (strcmp(name, "fullscreen") == 0) {
			s->logo_mode = ROCKCHIP_DISPLAY_FULLSCREEN;
		} else {
			s->logo_mode = ROCKCHIP_DISPLAY_CENTER;
		}
		ret = ofnode_read_string_index(node, "charge_logo,mode", 0, &name);
		if (strcmp(name, "fullscreen") == 0) {
			s->charge_logo_mode = ROCKCHIP_DISPLAY_FULLSCREEN;
		} else {
			s->charge_logo_mode = ROCKCHIP_DISPLAY_CENTER;
		}

		s->logo_rotate = ofnode_read_u32_default(node, "logo,rotate", 0);

		s->force_output = ofnode_read_bool(node, "force-output");

		if (s->force_output) {
			timing_node = ofnode_find_subnode(node, "force_timing");
			ret = display_get_force_timing_from_dts(timing_node,
								&s->force_mode,
								&s->conn_state.bus_flags);
			if (ofnode_read_u32(node, "force-bus-format", &s->force_bus_format) != 0) {
				s->force_bus_format = MEDIA_BUS_FMT_RGB888_1X24;
			}
		}

		s->blob = blob;
		s->conn_state.connector = conn;
		s->conn_state.secondary = NULL;
		s->conn_state.type = conn->type;
		if (split_conn) {
			s->conn_state.secondary = split_conn;
			s->conn_state.output_flags |= (u32)ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE;
			s->conn_state.output_flags |= conn->id != 0 ? (u32)ROCKCHIP_OUTPUT_DATA_SWAP : 0U;
		}
		s->conn_state.overscan.left_margin = 100;
		s->conn_state.overscan.right_margin = 100;
		s->conn_state.overscan.top_margin = 100;
		s->conn_state.overscan.bottom_margin = 100;
		s->crtc_state.node = np_to_ofnode(vop_node);
		s->crtc_state.port_node = port_node;
		s->crtc_state.dev = crtc_dev;
		s->crtc_state.crtc = crtc;
		s->crtc_state.crtc_id = get_crtc_id(np_to_ofnode(ep_node), is_ports_node);
		s->node = node;

		if (is_ports_node) { /* only vop2 will get into here */
			ofnode vp_node = np_to_ofnode(port_node);
			static bool get_plane_mask_from_dts;

			s->crtc_state.ports_node = port_parent_node;
			if (!get_plane_mask_from_dts) {
				ofnode vp_sub_node;
				int vp_id = 0;
				bool vp_enable = false;

				ofnode_for_each_subnode((vp_node), (np_to_ofnode(port_parent_node))) {
					int cursor_plane = -1;

					vp_id = ofnode_read_u32_default(vp_node, "reg", 0);

					s->crtc_state.crtc->vps[vp_id].xmirror_en =
						ofnode_read_bool(vp_node, "xmirror-enable");

					ret = ofnode_read_u32_default(vp_node, "rockchip,plane-mask", 0);

					cursor_plane = ofnode_read_u32_default(vp_node, "cursor-win-id", U32_MAX);
					s->crtc_state.crtc->vps[vp_id].cursor_plane = cursor_plane;
					if (ret != 0) {
						s->crtc_state.crtc->vps[vp_id].plane_mask = (u32)ret;
						s->crtc_state.crtc->assign_plane = true;
						s->crtc_state.crtc->vps[vp_id].primary_plane_id =
							(u8)ofnode_read_u32_default(vp_node, "rockchip,primary-plane", U8_MAX);
						(void)printf("get vp%d plane mask:0x%x, primary id:%d, cursor_plane:%d, from dts\n",
						       vp_id,
						       s->crtc_state.crtc->vps[vp_id].plane_mask,
						       s->crtc_state.crtc->vps[vp_id].primary_plane_id == U8_MAX ? -1 :
						       (int)s->crtc_state.crtc->vps[vp_id].primary_plane_id,
						       cursor_plane);
					}

					/* To check current vp status */
					vp_enable = false;
					ofnode_for_each_subnode((vp_sub_node), (vp_node)) {
						vp_enable = rockchip_get_display_path_status(vp_sub_node);
					}
					s->crtc_state.crtc->vps[vp_id].enable = vp_enable;
				}
				get_plane_mask_from_dts = true;
			}
		}

		(void)get_crtc_mcu_mode(&s->crtc_state, port_node, is_ports_node);

		ret = ofnode_read_u32_default(s->crtc_state.node,
					      "rockchip,dual-channel-swap", 0);
		s->crtc_state.dual_channel_swap = (u32)ret;

		if (connector_phy_init(conn, data) != 0) {
			(void)printf("Warn: Failed to init phy drivers\n");
			free(s);
			continue;
		}
		list_add_tail(&s->head, &rockchip_display_list);
	}

	if (list_empty(&rockchip_display_list) != 0) {
		(void)printf("Failed to found available display route\n");
		return -ENODEV;
	}
	(void)rockchip_get_baseparameter();
	(void)display_pre_init();

	uc_priv->xsize = DRM_ROCKCHIP_FB_WIDTH;
	uc_priv->ysize = DRM_ROCKCHIP_FB_HEIGHT;
	uc_priv->bpix = VIDEO_BPP32;

#ifdef CONFIG_DRM_ROCKCHIP_VIDEO_FRAMEBUFFER
	rockchip_show_fbbase(plat->base);
	video_set_flush_dcache(dev, true);
#endif

	return 0;
}

void rockchip_display_fixup(void *blob)
{
	const struct rockchip_connector_funcs *conn_funcs;
	const struct rockchip_crtc_funcs *crtc_funcs;
	struct rockchip_connector *conn;
	const struct rockchip_crtc *crtc;
	struct display_state *s;
	int offset;
	int ret;
	const struct device_node *np;
	const char *path;
	const char *cacm_header;
	char drm_logo_name[32] = "rockchip,drm-logo";
	char fb_logo_name[32] = "rockchip,fb-logo";
	char cubic_lut_name[32] = "rockchip,drm-cubic-lut";
	u64 aligned_memory_size;

	if (fdt_node_offset_by_compatible(blob, 0, "rockchip,drm-logo") >= 0) {
		list_for_each_entry((s), (&rockchip_display_list), head) {
			ret = load_bmp_logo(&s->logo, s->klogo_name);
			if (ret < 0) {
				s->is_klogo_valid = false;
				(void)printf("VP%d fail to load kernel logo\n", s->crtc_state.crtc_id);
			} else {
				s->is_klogo_valid = true;
			}
		}

		if (get_display_size() == 0U) {
			return;
		}

		aligned_memory_size = (u64)ALIGN((get_display_size()), (align_size));
		offset = fdt_update_reserved_memory(blob, drm_logo_name,
						    (u64)memory_start,
						    aligned_memory_size);
		if (offset < 0) {
			(void)printf("failed to reserve drm-loader-logo memory\n");
		}

		if (get_cubic_memory_size() != 0U) {
			aligned_memory_size = (u64)ALIGN((get_cubic_memory_size()), (align_size));
			offset = fdt_update_reserved_memory(blob, cubic_lut_name,
							    (u64)cubic_lut_memory_start,
							    aligned_memory_size);
			if (offset < 0) {
				(void)printf("failed to reserve drm-cubic-lut memory\n");
			}
		}
	} else {
		(void)printf("can't found rockchip,drm-logo, use rockchip,fb-logo\n");
		/* Compatible with rkfb display, only need reserve memory */
		offset = fdt_update_reserved_memory(blob, fb_logo_name,
						    (u64)memory_start,
						    MEMORY_POOL_SIZE);
		if (offset < 0) {
			(void)printf("failed to reserve fb-loader-logo memory\n");
		} else {
			list_for_each_entry((s), (&rockchip_display_list), head) {
				(void)load_kernel_bmp_logo(&s->logo, s->klogo_name);
			}
		}
		return;
	}

	list_for_each_entry((s), (&rockchip_display_list), head) {
		/*
		 * If plane mask is not set in dts, fixup dts to assign it
		 * whether crtc is initialized or not.
		 */
		if (s->crtc_state.crtc->funcs->fixup_dts != NULL && !s->crtc_state.crtc->assign_plane) {
			s->crtc_state.crtc->funcs->fixup_dts(s, blob);
		}

		if (!s->is_init || !s->is_klogo_valid) {
			continue;
		}

		conn = s->conn_state.connector;
		if (!conn) {
			continue;
		}
		conn_funcs = conn->funcs;
		if (!conn_funcs) {
			(void)printf("failed to get exist connector\n");
			continue;
		}

		crtc = s->crtc_state.crtc;
		if (!crtc) {
			continue;
		}

		crtc_funcs = crtc->funcs;
		if (!crtc_funcs) {
			(void)printf("failed to get exist crtc\n");
			continue;
		}

		np = ofnode_to_np(s->node);
		path = np->full_name;
		(void)fdt_increase_size(blob, 0x400);
#define FDT_SET_U32(name, val) \
		do_fixup_by_path_u32(blob, path, name, val, 1);

		offset = (int)s->logo.offset + (int)(uintptr_t)s->logo.mem
			 - (int)memory_start;
		FDT_SET_U32("logo,offset", (u32)offset);
		FDT_SET_U32("logo,width", s->logo.width);
		FDT_SET_U32("logo,height", s->logo.height);
		FDT_SET_U32("logo,bpp", s->logo.bpp);
		FDT_SET_U32("logo,ymirror", s->logo.ymirror ? 1U : 0U);
		FDT_SET_U32("video,clock", (u32)s->conn_state.mode.clock);
		FDT_SET_U32("video,hdisplay", (u32)s->conn_state.mode.hdisplay);
		FDT_SET_U32("video,vdisplay", (u32)s->conn_state.mode.vdisplay);
		FDT_SET_U32("video,crtc_hsync_end", (u32)s->conn_state.mode.crtc_hsync_end);
		FDT_SET_U32("video,crtc_vsync_end", (u32)s->conn_state.mode.crtc_vsync_end);
		FDT_SET_U32("video,vrefresh",
			    (u32)drm_mode_vrefresh(&s->conn_state.mode));
		FDT_SET_U32("video,flags", s->conn_state.mode.flags);
		FDT_SET_U32("video,aspect_ratio", (u32)s->conn_state.mode.picture_aspect_ratio);
		FDT_SET_U32("overscan,left_margin", (u32)s->conn_state.overscan.left_margin);
		FDT_SET_U32("overscan,right_margin", (u32)s->conn_state.overscan.right_margin);
		FDT_SET_U32("overscan,top_margin", (u32)s->conn_state.overscan.top_margin);
		FDT_SET_U32("overscan,bottom_margin", (u32)s->conn_state.overscan.bottom_margin);

		if (s->conn_state.disp_info) {
			cacm_header = (const char*)&s->conn_state.disp_info->cacm_header;

			FDT_SET_U32("bcsh,brightness", s->conn_state.disp_info->bcsh_info.brightness);
			FDT_SET_U32("bcsh,contrast", s->conn_state.disp_info->bcsh_info.contrast);
			FDT_SET_U32("bcsh,saturation", s->conn_state.disp_info->bcsh_info.saturation);
			FDT_SET_U32("bcsh,hue", s->conn_state.disp_info->bcsh_info.hue);

			if (strncasecmp(cacm_header, "CACM", 4) == 0) {
				FDT_SET_U32("post-csc,hue",
					    s->conn_state.disp_info->csc_info.hue);
				FDT_SET_U32("post-csc,saturation",
					    s->conn_state.disp_info->csc_info.saturation);
				FDT_SET_U32("post-csc,contrast",
					    s->conn_state.disp_info->csc_info.contrast);
				FDT_SET_U32("post-csc,brightness",
					    s->conn_state.disp_info->csc_info.brightness);
				FDT_SET_U32("post-csc,r-gain",
					    s->conn_state.disp_info->csc_info.r_gain);
				FDT_SET_U32("post-csc,g-gain",
					    s->conn_state.disp_info->csc_info.g_gain);
				FDT_SET_U32("post-csc,b-gain",
					    s->conn_state.disp_info->csc_info.b_gain);
				FDT_SET_U32("post-csc,r-offset",
					    s->conn_state.disp_info->csc_info.r_offset);
				FDT_SET_U32("post-csc,g-offset",
					    s->conn_state.disp_info->csc_info.g_offset);
				FDT_SET_U32("post-csc,b-offset",
					    s->conn_state.disp_info->csc_info.b_offset);
				FDT_SET_U32("post-csc,enable",
					    s->conn_state.disp_info->csc_info.csc_enable);
			}
		}

		if (s->conn_state.disp_info->cubic_lut_data.size != 0U &&
		    CONFIG_ROCKCHIP_CUBIC_LUT_SIZE != 0) {
			FDT_SET_U32("cubic_lut,offset", (u32)get_cubic_lut_offset(s->crtc_state.crtc_id));
		}

#undef FDT_SET_U32
	}
}

static int rockchip_display_bind(struct udevice *dev)
{
	struct video_uc_platdata *plat = dev_get_uclass_platdata(dev);

	plat->size = (uint)(DRM_ROCKCHIP_FB_SIZE + MEMORY_POOL_SIZE);

	return 0;
}

static const struct udevice_id rockchip_display_ids[] = {
	{ .compatible = "rockchip,display-subsystem" },
	{ }
};

U_BOOT_DRIVER(rockchip_display) = {
	.name	= "rockchip_display",
	.id	= UCLASS_VIDEO,
	.of_match = rockchip_display_ids,
	.bind	= rockchip_display_bind,
	.probe	= rockchip_display_probe,
};

static int do_rockchip_logo_show(cmd_tbl_t *cmdtp, int flag, int argc,
			char *const argv[])
{
	if (argc != 1) {
		return (int)CMD_RET_USAGE;
	}

	(void)rockchip_show_logo();

	return 0;
}

static int do_rockchip_show_bmp(cmd_tbl_t *cmdtp, int flag, int argc,
				char *const argv[])
{
	if (argc != 2) {
		return (int)CMD_RET_USAGE;
	}

	(void)rockchip_show_bmp(argv[1]);

	return 0;
}

static int do_rockchip_vop_dump(cmd_tbl_t *cmdtp, int flag, int argc,
				char *const argv[])
{
	int ret;

	if (argc < 1 || argc > 2) {
		return (int)CMD_RET_USAGE;
	}

	ret = rockchip_vop_dump(argv[1]);

	return ret;
}

U_BOOT_CMD(
	rockchip_show_logo, 1, 1, do_rockchip_logo_show,
	"load and display log from resource partition",
	NULL
);

U_BOOT_CMD(
	rockchip_show_bmp, 2, 1, do_rockchip_show_bmp,
	"load and display bmp from resource partition",
	("    <bmp_name>")
);

U_BOOT_CMD(
	vop_dump, 2, 1, do_rockchip_vop_dump,
	"dump vop regs",
	(" [a/all]")
);

// PRQA S 5129 --
// PRQA S 5125 --
// PRQA S 5124 --
// PRQA S 5118 --
// PRQA S 3101 --
