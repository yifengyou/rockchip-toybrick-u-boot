/* SPDX-License-Identifier:     GPL-2.0+ */
/*
 * (C) Copyright 2019 Rockchip Electronics Co., Ltd
 */

#ifndef _MEMBLK_H
#define _MEMBLK_H

#define ALIAS_COUNT_MAX		2
#define MEM_RESV_COUNT		10

enum memblk_id {
	MEM_UNK,

	/* Preloader */
	MEM_ATF,
	MEM_OPTEE,
	MEM_SHM,

	/* U-Boot self */
	MEM_UBOOT,
	MEM_STACK,
	MEM_FASTBOOT,

	/* Image */
	MEM_RESOURCE,
	MEM_RAMDISK,
	MEM_FDT,
	MEM_FDT_DTBO,
	MEM_KERNEL,
	MEM_UNCOMP_KERNEL,
	MEM_ANDROID,
	MEM_AVB_ANDROID,
	MEM_FIT_USER,
	MEM_FIT,
	MEM_UIMAGE_USER,
	MEM_UIMAGE,

	/* Other */
	MEM_SEARCH,
	MEM_BY_NAME,
	MEM_KMEM_RESERVED,
	MEM_MAX,
};

struct memblk_attr {
	const char *name;
	const char *alias[ALIAS_COUNT_MAX];
	u32 flags;
};

struct memblock {
	phys_addr_t base;
	phys_size_t size;
	u64 base_u64; /* 4GB+ */
	u64 size_u64;
	phys_addr_t orig_base;
	struct memblk_attr attr;
	struct list_head node;
};

extern const struct memblk_attr *mem_attr;

#define SIZE_MB(len)		((len) / (1024UL * 1024UL))
#define SIZE_KB(len)		((len) / 1024UL)

#define F_NONE			0

/* Over-Flow-Check for region tail */
#define F_OFC			(0x00001U)

/* Over-Flow-Check for region Head, only for U-Boot stack */
#define F_HOFC			(0x00002U)

/* Memory can be overlap by fdt reserved memory, deprecated */
#define F_OVERLAP		(0x00004U)

/* The region start address should be aligned to cacheline size */
#define F_CACHELINE_ALIGN	(0x00008U)

/* Kernel 'reserved-memory' */
#define F_KMEM_RESERVED		(0x00010U)

/* The region can be overlap by kernel 'reserved-memory' */
#define F_KMEM_CAN_OVERLAP	(0x00020U)

/* Ignore invisible region reserved by bidram */
#define F_IGNORE_INVISIBLE	(0x00040U)

/* Highest memory right under the sp */
#define F_HIGHEST_MEM		(0x00080U)

/* No sysmem layout dump if failed */
#define F_NO_FAIL_DUMP		(0x00100U)

/* Warning but not Error */
#define F_FAIL_WARNING		(0x00200U)

#endif /* _MEMBLK_H */
