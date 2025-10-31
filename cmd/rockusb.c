/*
 * Copyright 2017 Rockchip Electronics Co., Ltd
 * Frank Wang <frank.wang@rock-chips.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <errno.h>
#include <common.h>
#include <command.h>
#include <console.h>
#include <dm/device.h>
#include <g_dnl.h>
#include <part.h>
#include <usb.h>
#include <usb_mass_storage.h>
#include <rockusb.h>

static struct rockusb rkusb;
static struct rockusb *g_rkusb;

static int rkusb_read_sector(struct ums *ums_dev,
			     ulong start, lbaint_t blkcnt, void *buf)
{
	struct blk_desc *block_dev = &ums_dev->block_dev;
	lbaint_t blkstart = start + ums_dev->start_sector;
	int ret;

	if ((blkstart + blkcnt) > (ulong)RKUSB_READ_LIMIT_ADDR) {
		(void)memset(buf, 0xcc, blkcnt * (ulong)SECTOR_SIZE);
		return (int)blkcnt;
	} else {
		ret = (int)blk_dread(block_dev, blkstart, blkcnt, buf);
		if (ret == 0) {
			ret = -EIO;
		}
		return ret;
	}
}

static int rkusb_write_sector(struct ums *ums_dev,
			      ulong start, lbaint_t blkcnt, const void *buf)
{
	struct blk_desc *block_dev = &ums_dev->block_dev;
	lbaint_t blkstart = start + ums_dev->start_sector;
	struct blk_desc *mtd_blk = NULL;
	int ret;

	if (block_dev->if_type == IF_TYPE_MTD) {
		mtd_blk = dev_get_uclass_platdata(block_dev->bdev);
		mtd_blk->op_flag |= (unsigned char)BLK_MTD_CONT_WRITE;
	}

	ret = (int)blk_dwrite(block_dev, blkstart, blkcnt, buf);
	if (ret == 0) {
		ret = -EIO;
	}
#if defined(CONFIG_SCSI) && defined(CONFIG_CMD_SCSI) && (defined(CONFIG_UFS))
	if (block_dev->if_type == IF_TYPE_SCSI && block_dev->rawblksz == 4096) {
		/* write loader to UFS BootA */
		if (blkstart < 8192 && blkstart >= 64)
			blk_write_devnum(IF_TYPE_SCSI, 1, blkstart, blkcnt, (ulong *)buf);
	}
#endif
	if (block_dev->if_type == IF_TYPE_MTD && mtd_blk != NULL) {
		mtd_blk->op_flag &= (unsigned char)~BLK_MTD_CONT_WRITE;
	}
	return ret;
}

static int rkusb_erase_sector(struct ums *ums_dev,
			      ulong start, lbaint_t blkcnt)
{
	struct blk_desc *block_dev = &ums_dev->block_dev;
	lbaint_t blkstart = start + ums_dev->start_sector;

#if defined(CONFIG_SCSI) && defined(CONFIG_CMD_SCSI) && (defined(CONFIG_UFS))
	if (block_dev->if_type == IF_TYPE_SCSI && block_dev->rawblksz == 4096) {
		/* write loader to UFS BootA */
		if (blkstart < 8192) {
			lbaint_t cur_cnt = 8192 - blkstart;

			if (cur_cnt > blkcnt)
				cur_cnt = blkcnt;
			blk_erase_devnum(IF_TYPE_SCSI, 1, blkstart, cur_cnt);
		}
	}
#endif

	return (int)blk_derase(block_dev, blkstart, blkcnt);
}

static void rkusb_fini(void)
{
	int i;

	for (i = 0; i < g_rkusb->ums_cnt; i++) {
		free((void *)&g_rkusb->ums[i].name);
	}

	free(g_rkusb->ums);
	g_rkusb->ums = NULL;
	g_rkusb->ums_cnt = 0;
	g_rkusb = NULL;
}

#define RKUSB_NAME_LEN 16

static int rkusb_init(const char *devtype, const char *devnums_part_str)
{
	char *s, *t, *devnum_part_str, *name;
	struct blk_desc *block_dev;
	disk_partition_t info;
	int partnum, cnt;
	int ret = -1;
	struct ums *ums_new;

	s = strdup(devnums_part_str);
	if (!s) {
		return -1;
	}

	t = s;
	g_rkusb->ums_cnt = 0;

	for (;;) {
		devnum_part_str = strsep(&t, ",");
		if (!devnum_part_str) {
			break;
		}
#if defined(CONFIG_SCSI) && defined(CONFIG_CMD_SCSI) && (defined(CONFIG_UFS))
		if (!strcmp(devtype, "scsi")) {
			block_dev= blk_get_devnum_by_typename(devtype, 0);
			if (block_dev == NULL)
				return -ENXIO;
		} else
#endif
		{
			partnum = blk_get_device_part_str(devtype, devnum_part_str,
						&block_dev, &info, 1);
			if (partnum < 0) {
				free(s);
				rkusb_fini();
				return ret;
			}
		}

		/* f_mass_storage.c assumes SECTOR_SIZE sectors */
		if (block_dev->blksz != (ulong)SECTOR_SIZE) {
			free(s);
			rkusb_fini();
			return ret;
		}

		ums_new = realloc((void *)g_rkusb->ums, ((ulong)g_rkusb->ums_cnt + 1UL) *
				  sizeof(*g_rkusb->ums));
		if (!ums_new) {
			free(s);
			rkusb_fini();
			return ret;
		}
		g_rkusb->ums = ums_new;
		cnt = g_rkusb->ums_cnt;

		/* Expose all partitions for rockusb command */
		g_rkusb->ums[cnt].start_sector = 0U;
		g_rkusb->ums[cnt].num_sectors = (unsigned int)block_dev->lba;

		g_rkusb->ums[cnt].read_sector = rkusb_read_sector;
		g_rkusb->ums[cnt].write_sector = rkusb_write_sector;
		g_rkusb->ums[cnt].erase_sector = rkusb_erase_sector;

		name = malloc(RKUSB_NAME_LEN);
		if (!name) {
			free(s);
			rkusb_fini();
			return ret;
		}
		(void)snprintf(name, RKUSB_NAME_LEN, "rkusb disk %d", cnt);
		g_rkusb->ums[cnt].name = name;
		g_rkusb->ums[cnt].block_dev = *block_dev;

		(void)printf("RKUSB: LUN %d, dev %d, hwpart %d, sector %#x, count %#x\n",
		       g_rkusb->ums_cnt,
		       g_rkusb->ums[cnt].block_dev.devnum,
		       g_rkusb->ums[cnt].block_dev.hwpart,
		       g_rkusb->ums[cnt].start_sector,
		       g_rkusb->ums[cnt].num_sectors);

		g_rkusb->ums_cnt++;
	}

	if (g_rkusb->ums_cnt != 0) {
		ret = 0;
	}

	free(s);
	return ret;
}

void rkusb_force_to_usb2(bool enable)
{
	if (g_rkusb) {
		g_rkusb->force_usb2 = enable;
	}
}

bool rkusb_force_usb2_enabled(void)
{
	if (!g_rkusb) {
		return true;
	}

	return g_rkusb->force_usb2;
}

void rkusb_switch_to_usb3_enable(bool enable)
{
	if (g_rkusb) {
		g_rkusb->switch_usb3 = enable;
	}
}

bool rkusb_switch_usb3_enabled(void)
{
	if (!g_rkusb) {
		return false;
	}

	return g_rkusb->switch_usb3;
}

static int do_rkusb(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	const char *usb_controller;
	const char *devtype;
	const char *devnum;
	int controller_index;
	int rc;
	int cable_ready_timeout __maybe_unused;
	const char *s;
	bool re_enumerate;

	if (argc != 4) {
		return (int)CMD_RET_USAGE;
	}

	usb_controller = argv[1];
	devtype = argv[2];
	devnum	= argv[3];

	if (strncmp(devtype, "mmc", 3) == 0) {
		if (strncmp(devnum, "1", 1) == 0) {
			(void)printf("Forbid to flash mmc 1(sdcard)\n");
			return (int)CMD_RET_FAILURE;
		}
	} else if (strncmp(devtype, "nvme", 4)  == 0) {
		if (strncmp(devnum, "0", 1) == 0) {
			/*
			 * Add partnum ":0" to active 'allow_whole_dev' partiti
			 * search mechanism on multi storage, where there maybe not
			 * valid partition table
			 */
			devnum = "0:0";
		}
	} else if (strncmp(devtype, "scsi", 4) == 0) {
		if (strncmp(devnum, "0", 1) == 0) {
			/*
			 * Add partnum ":0" to active 'allow_whole_dev' partitio
			 * search mechanism on multi storage, where there maybe not
			 * valid partition table.
			 */
			devnum = "0:0";
		}
	} else {
		/* Intentionally Empty */
	}

	g_rkusb = &rkusb;
	rc = rkusb_init(devtype, devnum);
	if (rc < 0) {
		return (int)CMD_RET_FAILURE;
	}

	if (g_rkusb->ums[0].block_dev.if_type == IF_TYPE_MTD &&
	    g_rkusb->ums[0].block_dev.devnum == BLK_MTD_NAND) {
#ifdef CONFIG_CMD_GO
		(void)printf("Enter bootrom rockusb...\n");
		flushc();
		(void)run_command("rbrom", 0);
#else
		pr_err("rockusb: count not support loader upgrade!\n");
#endif
	}

	do {
		controller_index = (int)(simple_strtoul(usb_controller,	NULL, 0));
		rc = usb_gadget_initialize(controller_index);
		if (rc != 0 ) {
			(void)printf("Couldn't init USB controller.");
			rkusb_fini();
			rc = (int)CMD_RET_FAILURE;
			break;
		}

		rc = fsg_init(g_rkusb->ums, g_rkusb->ums_cnt);
		if (rc != 0) {
			(void)printf("fsg_init failed");
			(void)usb_gadget_release(controller_index);
			rkusb_fini();
			rc = (int)CMD_RET_FAILURE;
			break;
		}

		if (rkusb_switch_usb3_enabled()) {
			/* Maskrom usb3 serialnumber get from upgrade tool */
			rkusb_switch_to_usb3_enable(false);
		} else {
			s = env_get("serial#");
			if (s != NULL) {
				size_t s_len = strlen(s);
				char *sn = (char *)calloc(s_len + 1UL, sizeof(char));
				unsigned int i;

				if (!sn) {
					(void)usb_gadget_release(controller_index);
					rkusb_fini();
					rc = (int)CMD_RET_FAILURE;
					break;
				}

				sn [s_len] = '\0';
				(void)memcpy(sn, s, s_len);

				for (i = 0; i <= s_len; i++) {
					if (sn[i] == '\\' || sn[i] == '/') {
						sn[i] = '_';
					}
				}

				g_dnl_set_serialnumber(sn);
				free(sn);
#if defined(CONFIG_SUPPORT_USBPLUG)
			} else {
				char sn[9] = "Rockchip";

				g_dnl_set_serialnumber(sn);
#endif
			}
		}

		rc = g_dnl_register("rkusb_ums_dnl");
		if (rc != 0) {
			(void)printf("g_dnl_register failed");
			(void)usb_gadget_release(controller_index);
			rkusb_fini();
			rc = (int)CMD_RET_FAILURE;
			break;
		}

		/* Timeout unit: seconds */
		cable_ready_timeout = UMS_CABLE_READY_TIMEOUT;

		if (g_dnl_board_usb_cable_connected() == 0) {
			puts("Please connect USB cable.\n");

			while (g_dnl_board_usb_cable_connected() == 0) {
				if (ctrlc() != 0) {
					puts("\rCTRL+C - Operation aborted.\n");
					g_dnl_unregister();
					(void)usb_gadget_release(controller_index);
					rkusb_fini();
					rc = (int)CMD_RET_SUCCESS;
					break;
				}
				if (cable_ready_timeout == 0) {
					puts("\rUSB cable not detected.\nCommand exit.\n");
					g_dnl_unregister();
					(void)usb_gadget_release(controller_index);
					rkusb_fini();
					rc = (int)CMD_RET_SUCCESS;
					break;
				}

				(void)printf("\rAuto exit in: %.2d s.", cable_ready_timeout);
				mdelay(1000);
				cable_ready_timeout--;
			}
			puts("\r\n");
			break;
		}

		while (1) {
			(void)usb_gadget_handle_interrupts(controller_index);

			rc = fsg_main_thread(NULL);
			if (rc != 0) {
				if (rc == -ENODEV) {
					if (rkusb_usb3_capable()) {
						if (!rkusb_force_usb2_enabled()) {
							(void)printf("wait for usb3 connect timeout\n");
							rkusb_force_to_usb2(true);
							g_dnl_unregister();
							(void)usb_gadget_release(controller_index);
							re_enumerate = true;
							break;
						}
					}
				}
				/* Check I/O error */
				if (rc == -EIO) {
					(void)printf("\rCheck USB cable connection\n");
				}

				/* Check CTRL+C */
				if (rc == -EPIPE) {
					(void)printf("\rCTRL+C - Operation aborted\n");
				}

				g_dnl_unregister();
				(void)usb_gadget_release(controller_index);
				rkusb_fini();
				re_enumerate = false;
				rc = (int)CMD_RET_SUCCESS;
				break;
			}

			if (rkusb_switch_usb3_enabled()) {
				(void)printf("rockusb switch to usb3\n");
				g_dnl_unregister();
				(void)usb_gadget_release(controller_index);
				re_enumerate = true;
				break;
			}
		}
	} while (re_enumerate);

	return rc;
}

U_BOOT_CMD_ALWAYS(rockusb, 4, 1, do_rkusb,
		  "Use the rockusb Protocol",
		  "<USB_controller> <devtype> <dev[:part]>  e.g. rockusb 0 mmc 0\n"
);
