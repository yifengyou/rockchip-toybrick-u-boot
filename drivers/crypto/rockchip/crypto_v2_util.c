// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <rockchip/crypto_v2_util.h>

/* ------------------------------------------------------------
 **
 * @brief This function executes a memory copy between 2 buffers.
 *
 * @param[in] dst_ptr - The first counter buffer.
 * @param[in] src_ptr - The second counter buffer.
 * @param[in] size    - the first counter size in words.
 *
 */
void util_word_memcpy(u32 *dst_ptr, u32 *src_ptr, u32 size)
{
	u32 i;

	/* execute the reverse memcpoy */
	for (i = 0; i < size; i++) {
		dst_ptr[i] = src_ptr[i];
	}
} /* END OF util_memcpy */

/* ------------------------------------------------------------
 **
 * @brief This function executes a memory set operation on a buffer.
 *
 * @param[in] buff_ptr - the buffer.
 * @param[in] val      - The value to set the buffer.
 * @param[in] size     - the buffers size in words.
 *
 */
void util_word_memset(u32 *buff_ptr, u32 val, u32 size)
{
	u32 i;

	/* execute the reverse memcpoy */
	for (i = 0; i < size; i++) {
		buff_ptr[i] = val;
	}
} /* END OF util_memcpy */
