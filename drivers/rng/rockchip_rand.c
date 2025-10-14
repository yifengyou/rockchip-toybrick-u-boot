/*
 * (C) Copyright 2021 Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */
#include <common.h>
#include <dm.h>
#include <rng.h>

unsigned int rand_r(unsigned int *seedp)
{
	struct udevice *dev;
	unsigned int rand_val;
	int ret;

	ret = uclass_get_device(UCLASS_RNG, 0, &dev);
	if (ret != 0) {
		printf("No RNG device, ret=%d\n", ret);
		return (unsigned int)ret;
	}

	ret = dm_rng_read(dev, &rand_val, sizeof(unsigned int));
	if (ret != 0) {
		printf("Reading RNG failed, ret=%d\n", ret);
		return (unsigned int)ret;
	}

	return rand_val;
}

unsigned int rand(void)
{
	return rand_r(NULL);
}

void srand(unsigned int seed)
{
	/* nothing to do */
}

