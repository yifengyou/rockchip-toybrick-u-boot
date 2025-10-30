/*
 * (C) Copyright 2017 Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */
#include <common.h>
#include <boot_rkimg.h>
#include <bmp_layout.h>
#include <malloc.h>
#include <asm/unaligned.h>
#include <linux/libfdt.h>
#include <linux/list.h>
#include <asm/arch/resource_img.h>
#include <asm/arch/rk_hwid.h>
#include <asm/arch/uimage.h>
#include <asm/arch/fit.h>

// PRQA S 5124 ++
// PRQA S 3408 ++
// PRQA S 5118 ++
// PRQA S 3200 ++
// PRQA S 2839 ++
/* -------- dataflow ----------- */
// PRQA S 2981 ++
// PRQA S 2839 ++
// PRQA S 1504 ++
// PRQA S 2811 ++
// PRQA S 2844 ++
// PRQA S 2849 ++
// PRQA S 2996 ++
// PRQA S 2998 ++
// PRQA S 2934 ++
// PRQA S 1502 ++
// PRQA S 2810 ++

DECLARE_GLOBAL_DATA_PTR;

#define PART_RESOURCE			"resource"
#define RESOURCE_MAGIC			"RSCE"
#define RESOURCE_MAGIC_SIZE		4
#define ENTRY_TAG			"ENTR"
#define ENTRY_TAG_SIZE			4
#define MAX_FILE_NAME_LEN		220
#define MAX_HASH_LEN			32
#define DEFAULT_DTB_FILE		"rk-kernel.dtb"

/*
 *         resource image structure
 * ----------------------------------------------
 * |                                            |
 * |    header  (1 block)                       |
 * |                                            |
 * ---------------------------------------------|
 * |                      |                     |
 * |    entry0  (1 block) |                     |
 * |                      |                     |
 * ------------------------                     |
 * |                      |                     |
 * |    entry1  (1 block) | contents (n blocks) |
 * |                      |                     |
 * ------------------------                     |
 * |    ......            |                     |
 * ------------------------                     |
 * |                      |                     |
 * |    entryn  (1 block) |                     |
 * |                      |                     |
 * ----------------------------------------------
 * |                                            |
 * |    file0  (x blocks)                       |
 * |                                            |
 * ----------------------------------------------
 * |                                            |
 * |    file1  (y blocks)                       |
 * |                                            |
 * ----------------------------------------------
 * |                   ......                   |
 * |---------------------------------------------
 * |                                            |
 * |    filen  (z blocks)                       |
 * |                                            |
 * ----------------------------------------------
 */

/**
 * struct resource_img_hdr
 *
 * @magic: should be "RSCE"
 * @version: resource image version, current is 0
 * @c_version: content version, current is 0
 * @blks: the size of the header ( 1 block = 512 bytes)
 * @c_offset: contents offset(by block) in the image
 * @e_blks: the size(by block) of the entry in the contents
 * @e_num: numbers of the entrys.
 */
struct resource_img_hdr {
	char		magic[4];
	uint16_t	version;
	uint16_t	c_version;
	uint8_t		blks;
	uint8_t		c_offset;
	uint8_t		e_blks;
	uint32_t	e_nums;
};

struct resource_entry {
	char		tag[4];
	char		name[MAX_FILE_NAME_LEN];
	char		hash[MAX_HASH_LEN];
	uint32_t	hash_size;
	uint32_t	blk_offset;
	uint32_t	size;		/* in byte */
};

LIST_HEAD(entry_head);

static int resource_check_header(struct resource_img_hdr *hdr)
{
	return strncmp(RESOURCE_MAGIC, hdr->magic, RESOURCE_MAGIC_SIZE);
}

static void resource_dump(struct resource_file *f)
{
	printf("%s\n", f->name);
	printf("  blk_start:  0x%08lx\n", f->blk_start);
	printf("  blk_offset: 0x%08lx\n", f->blk_offset);
	printf("  size:       0x%08x\n", f->size);
	printf("  in_ram:     %d\n", f->in_ram);
	printf("  hash_size:  %d\n\n", f->hash_size);
}

static int resource_add_file(const char *name, u32 size,
			     ulong blk_start,  u32 blk_offset,
			     char *hash, u32 hash_size,
			     bool in_ram)
{
	struct resource_file *f;
	struct list_head *node;
	bool _new = true;

	/* old one ? */
	list_for_each((node), (&entry_head)) {
		f = list_entry(node, struct resource_file, link);
		if (strcmp(f->name, name) == 0) {
			_new = false;
			break;
		}
	}

	if (_new == true) {
		f = calloc(1, sizeof(*f));
		if (f == NULL) {
			return -ENOMEM;
		}

		list_add_tail(&f->link, &entry_head);
	}

	strcpy(f->name, name);
	f->size       = size;
	f->in_ram     = in_ram;
	f->blk_start  = blk_start;
	f->blk_offset = blk_offset;
	f->hash_size  = hash_size;
	memcpy(f->hash, hash, hash_size);
#ifdef DEBUG
	resource_dump(f);
#endif
	return 0;
}

// PRQA S 1503 ++
void resource_destroy(void)
{
	INIT_LIST_HEAD(&entry_head);
}
// PRQA S 1503 --

#ifdef CONFIG_ANDROID_BOOT_IMAGE
/*
 * Add logo.bmp and logo_kernel.bmp from "logo" parititon
 *
 * Provide a "logo" partition for user to store logo.bmp and
 * logo_kernel.bmp, so that the user can update them from
 * kernel or user-space dynamically.
 *
 * "logo" partition layout, do not change order:
 *
 *   |----------------------| 0x00
 *   | raw logo.bmp	    |
 *   |----------------------| -> 512-byte aligned
 *   | raw logo_kernel.bmp  |
 *   |----------------------|
 *
 * N: the sector count of logo.bmp
 *
 * How to generate:
 * 	cat logo.bmp > logo.img && truncate -s %512 logo.img && cat logo_kernel.bmp >> logo.img
 */
static int resource_setup_logo_bmp(struct blk_desc *desc)
{
	struct bmp_header *header;
	const char *name[] = { "logo.bmp", "logo_kernel.bmp" };
	disk_partition_t part;
	u32 blk_offset = 0;
	u32 filesz;
	int ret, i;

	if (part_get_info_by_name(desc, PART_LOGO, &part) < 0) {
		return 0;
	}

	header = memalign(ARCH_DMA_MINALIGN, desc->blksz);
	if (header == NULL) {
		return -ENOMEM;
	}

	for (i = 0; i < (int)ARRAY_SIZE(name); i++) {
		if (blk_dread(desc, part.start + blk_offset, 1, header) != 1U) {
			ret = -EIO;
			break;
		}

		if (header->signature[0] != 'B' || header->signature[1] != 'M') {
			ret = -EINVAL;
			break;
		}

		filesz = (u32)get_unaligned_le32(&header->file_size);
		ret = resource_add_file(name[i], filesz, part.start, blk_offset,
					NULL, 0, false);
		if (ret != 0) {
			break;
		}

		/* move to next file */
		blk_offset += (u32)DIV_ROUND_UP((filesz), (desc->blksz));

		printf("LOGO: %s\n", name[i]);

	}

	free(header);

	return ret;
}
#endif

static int resource_setup_list(struct blk_desc *desc, ulong blk_start,
			       void *resc_hdr, bool in_ram)
{
	struct resource_img_hdr *hdr = resc_hdr;
	struct resource_entry *et;
	ulong stride;
	void *pos;
	u32 i;

	pos = (void *)hdr + hdr->c_offset * desc->blksz;
	stride = hdr->e_blks * desc->blksz;

	for (i = 0; i < hdr->e_nums; i++) {
		et = pos + (i * stride);
		if (strncmp(et->tag, ENTRY_TAG, ENTRY_TAG_SIZE) != 0) {
			continue;
		}

		resource_add_file(et->name, et->size,
				  blk_start, et->blk_offset,
				  et->hash, et->hash_size, in_ram);
	}
#ifdef CONFIG_ANDROID_BOOT_IMAGE
	resource_setup_logo_bmp(desc);
#endif
	return 0;
}

int resource_setup_ram_list(struct blk_desc *desc, void *hdr)
{
	ulong blk_start;

	if (desc == NULL) {
		return -ENODEV;
	}

	if (resource_check_header(hdr) != 0) {
		printf("RESC: invalid\n");
		return -EINVAL;
	}

	blk_start = (ulong)hdr;

	/* @blk_start: set as 'hdr' point addr, to be used in byte */
	return resource_setup_list(desc, blk_start, hdr, true) != 0 ? 1 : 0;
}

#ifdef CONFIG_ANDROID_BOOT_IMAGE
static int resource_setup_blk_list(struct blk_desc *desc, ulong blk_start)
{
	struct resource_img_hdr *hdr;
	ulong blk_cnt;
	int ret = 0;
	void *buf;

	hdr = memalign(ARCH_DMA_MINALIGN, desc->blksz);
	if (hdr == NULL) {
		return -ENOMEM;
	}

	if (blk_dread(desc, blk_start, 1, hdr) != 1U) {
		ret = -EIO;
		goto out;
	}

	if (resource_check_header(hdr) != 0) {
		printf("RESC: invalid\n");
		if (fdt_check_header(hdr) != 0) {
			ret = -EINVAL;
			goto out;
		} else {
			/* this is a dtb file */
			printf("RESC: this is dtb\n");
			ret = resource_add_file(DEFAULT_DTB_FILE,
						(u32)fdt_totalsize((ulong)hdr),
						blk_start, 0, NULL, 0, false);
			goto out;
		}
	}

	blk_cnt = (ulong)hdr->e_blks * hdr->e_nums;
	hdr = realloc(hdr, (1U + blk_cnt) * desc->blksz);
	if (hdr == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	// PRQA S 2844
	buf = (void *)hdr + desc->blksz;
	// PRQA S 2844
	if (blk_dread(desc, blk_start + hdr->c_offset, blk_cnt, buf) != blk_cnt) {
		ret = -EIO;
		goto out;
	}

	resource_setup_list(desc, blk_start, hdr, false);
out:
	free(hdr);

	return ret;
}

static int resource_init(struct blk_desc *desc,
			 disk_partition_t *part,
			 ulong blk_offset)
{
	printf("RESC: '%s', blk@0x%08lx\n", part->name, part->start + blk_offset);

#ifdef CONFIG_ANDROID_AVB
	char hdr[512];
	ulong resc_buf = 0;
	bool handle = false;
	int ret;

	if (blk_dread(desc, part->start, 1, hdr) != 1U) {
		return -EIO;
	}

	if (android_image_check_header((void *)hdr) == 0) {
		handle = true;
	}
	if (resource_check_header((void *)hdr) == 0) {
		handle = true;
	}

	/* only handle android boot/recovery.img and resource.img, ignore fit */
	if (handle) {
		ret = android_image_verify_resource((const char *)part->name, &resc_buf);
		if (ret != 0) {
			printf("RESC: '%s', avb verify fail: %d\n", part->name, ret);
			return ret;
		}

		/*
		 * unlock=0: resc_buf is valid and file was already full load in ram.
		 * unlock=1: resc_buf is 0.
		 */
		if (resc_buf != 0U) {
			if (resource_check_header((struct resource_img_hdr *)resc_buf) == 0) {
				return resource_setup_ram_list(desc, (struct resource_img_hdr *)resc_buf);
			}
		}
	}

#endif

	return resource_setup_blk_list(desc, part->start + blk_offset);
}

static int resource_default(struct blk_desc *desc,
			    disk_partition_t *out_part,
			    ulong *out_blk_offset)
{
	disk_partition_t part;

	if (part_get_info_by_name(desc, PART_RESOURCE, &part) < 0) {
		return -ENODEV;
	}

	*out_part = part;
	*out_blk_offset = 0;

	return 0;
}
#endif

static int resource_scan(void)
{
	struct blk_desc *desc = rockchip_get_bootdev();
	__maybe_unused int ret;

	if (desc == NULL) {
		printf("RESC: No bootdev\n");
		return -ENODEV;
	}

	if (list_empty(&entry_head) == 0) {
		return 0;
	}

#ifdef CONFIG_ROCKCHIP_FIT_IMAGE
	ret = (int)fit_image_init_resource(desc);
	if (ret == 0 || ret != -EAGAIN) {
		return ret;
	}
#endif
#ifdef CONFIG_ROCKCHIP_UIMAGE
	ret = uimage_init_resource(desc);
	if (!ret || ret != -EAGAIN)
		return ret;
#endif
#ifdef CONFIG_ANDROID_BOOT_IMAGE
	disk_partition_t part;
	ulong blk_offset;
	char hdr[512];
	char name[32];

	/* partition priority: boot/recovery > resource */
	if (android_image_init_resource(desc, &part, &blk_offset) == 0) {
		if (blk_dread(desc, part.start + blk_offset, 1, hdr) != 1U) {
			return -EIO;
		}

		if (resource_check_header((void *)hdr) != 0) {
			strcpy(name, (char *)part.name);
			if (resource_default(desc, &part, &blk_offset) != 0) {
				return -ENOENT;
			}

			printf("RESC: '%s' -> '%s'\n", name, part.name);
		}
	} else {
		if (resource_default(desc, &part, &blk_offset) != 0) {
			return -ENOENT;
		}
	}

	/* now, 'part' can be boot/recovery/resource */
	return resource_init(desc, &part, blk_offset);
#endif
	return -ENOENT;
}

static struct resource_file *resource_get_file(const char *name)
{
	struct resource_file *f;
	struct list_head *node;

	if (resource_scan() != 0) {
		return NULL;
	}

	list_for_each((node), (&entry_head)) {
		f = list_entry(node, struct resource_file, link);
		if (strcmp(f->name, name) == 0) {
			return f;
		}
	}

	return NULL;
}

int rockchip_read_resource_file(void *buf, const char *name, int blk_offset, int len)
{
	struct blk_desc *desc = rockchip_get_bootdev();
	struct resource_file *f;
	ulong blk_cnt;
	ulong pos;

	if (desc == NULL) {
		return -ENODEV;
	}

	f = resource_get_file(name);
	if (!f) {
		printf("No resource file: %s\n", name);
		return -ENOENT;
	}

	if (len <= 0 || (u32)len > f->size) {
		len = (int)f->size;
	}

	if (f->in_ram) {
		pos = f->blk_start + (f->blk_offset + (ulong)blk_offset) * desc->blksz;
		memcpy((char *)buf, (char *)pos, (ulong)len);
	} else {
		blk_cnt = DIV_ROUND_UP((ulong)len, desc->blksz);
		if (blk_dread(desc,
			      f->blk_start + f->blk_offset + (ulong)blk_offset,
			      blk_cnt, buf) != blk_cnt) {
			len = -EIO;
		}
	}

	return len;
}

#ifdef CONFIG_ROCKCHIP_HWID_DTB
static struct resource_file *resource_read_hwid_dtb(void)
{
	struct resource_file *file;
	struct list_head *node;

	hwid_init_data();

	list_for_each((node), (&entry_head)) {
		file = list_entry(node, struct resource_file, link);
		if (!strstr(file->name, DTB_SUFFIX)) {
			continue;
		}

		if (hwid_dtb_is_available(file->name)) {
			return file;
		}
	}

	return NULL;
}
#endif

int rockchip_read_resource_dtb(void *fdt_addr, char **hash, int *hash_size)
{
	struct resource_file *f = NULL;
	int ret;

#ifdef CONFIG_ROCKCHIP_HWID_DTB
	if (resource_scan() != 0) {
		return -ENOENT;
	}

	f = resource_read_hwid_dtb();
#endif
	/* If no dtb match hardware id(GPIO/ADC), use the default */
	if (!f) {
		f = resource_get_file(DEFAULT_DTB_FILE);
	}

	if (!f) {
		return -ENODEV;
	}

	ret = rockchip_read_resource_file(fdt_addr, f->name, 0, 0);
	if (ret < 0) {
		return ret;
	}

	if (fdt_check_header(fdt_addr) != 0) {
		return -EBADF;
	}

	*hash = f->hash;
	*hash_size = (int)f->hash_size;

	printf("DTB: %s\n", f->name);

	return 0;
}

static int do_dump_resource(cmd_tbl_t *cmdtp, int flag,
			    int argc, char *const argv[])
{
	struct resource_file *f;
	struct list_head *node;

	list_for_each((node), (&entry_head)) {
		f = list_entry(node, struct resource_file, link);
		resource_dump(f);
	}

	return 0;
}

// PRQA S 5124 --
// PRQA S 3408 --
// PRQA S 5118 --
// PRQA S 3200 --
/* -------- dataflow ----------- */
// PRQA S 2981 --
// PRQA S 2839 --
// PRQA S 1504 --
// PRQA S 2811 --
// PRQA S 2844 --
// PRQA S 2849 --
// PRQA S 2996 --
// PRQA S 2998 --
// PRQA S 2934 --
// PRQA S 1502 --
// PRQA S 2810 --


// PRQA S 3432,0753,0310,0311,3410,3305 ++
U_BOOT_CMD(
	dump_resource, 1, 1, do_dump_resource,
	"dump resource files",
	""
);
// PRQA S 3432,0753,0310,0311,3410,3305 --

