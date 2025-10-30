// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright © 2009 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
// PRQA S 5124 ++
#include <common.h>
#include <drm/drm_dp_helper.h>

/**
 * DOC: dp helpers
 *
 * These functions contain some common logic and helpers at various abstraction
 * levels to deal with Display Port sink devices and related things like DP aux
 * channel transfers, EDID reading over DP aux channels, decoding certain DPCD
 * blocks, ...
 */

/* Helpers for DP link training */
static u8 dp_link_status(const u8 link_status[DP_LINK_STATUS_SIZE], int r)
{
	if (r - DP_LANE0_1_STATUS < 0 || r - DP_LANE0_1_STATUS > 5) {
		return 0U;
	}

	return link_status[r - DP_LANE0_1_STATUS];
}

static u8 dp_get_lane_status(const u8 link_status[DP_LINK_STATUS_SIZE],
			     int lane)
{
	u32 i = (u32)DP_LANE0_1_STATUS + ((u32)lane >> 1U);
	u32 s = ((u32)lane & 1U) * 4U;
	u8 l = dp_link_status(link_status, (int)i);

	return (l >> s) & 0xfU;
}

bool drm_dp_channel_eq_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			  int lane_count)
{
	u8 lane_align;
	u8 lane_status;
	int lane;

	lane_align = dp_link_status(link_status,
				    DP_LANE_ALIGN_STATUS_UPDATED);
	if ((lane_align & (u8)DP_INTERLANE_ALIGN_DONE) == 0U) {
		return false;
	}
	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & (u8)DP_CHANNEL_EQ_BITS) != (u8)DP_CHANNEL_EQ_BITS) {
			return false;
		}
	}
	return true;
}

bool drm_dp_clock_recovery_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			      int lane_count)
{
	int lane;
	u8 lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & (u8)DP_LANE_CR_DONE) == 0U) {
			return false;
		}
	}
	return true;
}

u8 drm_dp_get_adjust_request_voltage(const u8 link_status[DP_LINK_STATUS_SIZE],
				     int lane)
{
	u32 i = (u32)DP_ADJUST_REQUEST_LANE0_1 + ((u32)lane >> 1U);
	u32 s = (((u32)lane & 1U) != 0U ?
		 (u32)DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
		 (u32)DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, (int)i);

	return ((l >> s) & 0x3U) << (u8)DP_TRAIN_VOLTAGE_SWING_SHIFT;
}

u8 drm_dp_get_adjust_request_pre_emphasis(const u8 link_status[DP_LINK_STATUS_SIZE],
					  int lane)
{
	u32 i = (u32)DP_ADJUST_REQUEST_LANE0_1 + ((u32)lane >> 1U);
	u32 s = (((u32)lane & 1U) != 0U ?
		 (u32)DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
		 (u32)DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, (int)i);

	return ((l >> s) & 0x3U) << (u8)DP_TRAIN_PRE_EMPHASIS_SHIFT;
}

void drm_dp_link_train_clock_recovery_delay(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 rd_interval = dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
			  (u8)DP_TRAINING_AUX_RD_MASK;

	if (rd_interval > 4U) {
		(void)printf("AUX interval %d, out of range (max 4)\n", rd_interval);
	}

	if (rd_interval == 0U || dpcd[DP_DPCD_REV] >= (u8)DP_DPCD_REV_14) {
		udelay(100);
	} else {
		mdelay((unsigned long)rd_interval * 4UL);
	}
}

void drm_dp_link_train_channel_eq_delay(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 rd_interval = dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
			  (u8)DP_TRAINING_AUX_RD_MASK;

	if (rd_interval > 4U) {
		(void)printf("AUX interval %d, out of range (max 4)\n", rd_interval);
	}

	if (rd_interval == 0U) {
		udelay(400);
	} else {
		mdelay((unsigned long)rd_interval * 4UL);
	}
}

u8 drm_dp_link_rate_to_bw_code(int link_rate)
{
	/* Spec says link_bw = link_rate / 0.27Gbps */
	return (u8)((u32)link_rate / 27000U);
}

int drm_dp_bw_code_to_link_rate(u8 link_bw)
{
	/* Spec says link_rate = link_bw * 0.27Gbps */
	return (int)link_bw * 27000;
}

#define AUX_RETRY_INTERVAL 500 /* us */

static int drm_dp_dpcd_access(struct drm_dp_aux *aux, u8 request,
			      unsigned int offset, void *buffer, size_t size)
{
	struct drm_dp_aux_msg msg;
	unsigned int retry, native_reply;
	int err = 0, ret = 0;

	(void)memset(&msg, 0, sizeof(msg));
	msg.address = offset;
	msg.request = request;
	msg.buffer = buffer;
	msg.size = size;

	/*
	 * The specification doesn't give any recommendation on how often to
	 * retry native transactions. We used to retry 7 times like for
	 * aux i2c transactions but real world devices this wasn't
	 * sufficient, bump to 32 which makes Dell 4k monitors happier.
	 */
	for (retry = 0; retry < 32U; retry++) {
		if (ret != 0 && ret != -ETIMEDOUT) {
			udelay(AUX_RETRY_INTERVAL);
		}

		ret = (int)aux->transfer(aux, &msg);
		if (ret >= 0) {
			native_reply = (unsigned int)msg.reply & (unsigned int)DP_AUX_NATIVE_REPLY_MASK;
			if (native_reply == (unsigned int)DP_AUX_NATIVE_REPLY_ACK) {
				if ((size_t)ret == size) {
					goto out;
				}

				ret = -EPROTO;
			} else {
				ret = -EIO;
			}
		}

		/*
		 * We want the error we return to be the error we received on
		 * the first transaction, since we may get a different error the
		 * next time we retry
		 */
		if (err == 0) {
			err = ret;
		}
	}

	(void)printf("%s: Too many retries, giving up. First error: %d\n",
	       aux->name, err);
	ret = err;

out:
	return ret;
}

ssize_t drm_dp_dpcd_read(struct drm_dp_aux *aux, unsigned int offset,
			 void *buffer, size_t size)
{
	int ret;

	ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, DP_DPCD_REV,
				 buffer, 1);
	if (ret != 1) {
		goto out;
	}

	ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, offset,
				 buffer, size);

out:
	return ret;
}

ssize_t drm_dp_dpcd_write(struct drm_dp_aux *aux, unsigned int offset,
			  void *buffer, size_t size)
{
	int ret;

	ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_WRITE, offset,
				 buffer, size);

	return ret;
}

int drm_dp_dpcd_read_link_status(struct drm_dp_aux *aux,
				 u8 status[DP_LINK_STATUS_SIZE])
{
	return (int)drm_dp_dpcd_read(aux, DP_LANE0_1_STATUS, status,
				DP_LINK_STATUS_SIZE);
}

static int drm_dp_read_extended_dpcd_caps(struct drm_dp_aux *aux,
					  u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 dpcd_ext[6];
	int ret;

	/*
	 * Prior to DP1.3 the bit represented by
	 * DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT was reserved.
	 * If it is set DP_DPCD_REV at 0000h could be at a value less than
	 * the true capability of the panel. The only way to check is to
	 * then compare 0000h and 2200h.
	 */
	if ((dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
	      (u8)DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT) == 0U) {
		return 0;
	}

	ret = (int)drm_dp_dpcd_read(aux, DP_DP13_DPCD_REV, &dpcd_ext,
			       sizeof(dpcd_ext));
	if (ret < 0) {
		return ret;
	}
	if (ret != (int)sizeof(dpcd_ext)) {
		return -EIO;
	}

	if (dpcd[DP_DPCD_REV] > dpcd_ext[DP_DPCD_REV]) {
		(void)printf("%s: Extended DPCD rev less than base DPCD rev (%d > %d)\n",
		       aux->name, dpcd[DP_DPCD_REV], dpcd_ext[DP_DPCD_REV]);
		return 0;
	}

	if (memcmp(dpcd, dpcd_ext, sizeof(dpcd_ext)) != 0) {
		return 0;
	}

	(void)printf("%s: Base DPCD: %*ph\n",
	       aux->name, DP_RECEIVER_CAP_SIZE, dpcd);

	(void)memcpy(dpcd, dpcd_ext, sizeof(dpcd_ext));

	return 0;
}

int drm_dp_read_dpcd_caps(struct drm_dp_aux *aux,
			  u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	int ret;

	ret = (int)drm_dp_dpcd_read(aux, DP_DPCD_REV, dpcd, DP_RECEIVER_CAP_SIZE);
	if (ret < 0) {
		return ret;
	}
	if (ret != DP_RECEIVER_CAP_SIZE || dpcd[DP_DPCD_REV] == 0U) {
		return -EIO;
	}

	ret = drm_dp_read_extended_dpcd_caps(aux, dpcd);
	if (ret < 0) {
		return ret;
	}

	(void)printf("%s: DPCD: %*ph\n",
	       aux->name, DP_RECEIVER_CAP_SIZE, dpcd);

	return ret;
}

static void drm_dp_i2c_msg_write_status_update(struct drm_dp_aux_msg *msg)
{
	/*
	 * In case of i2c defer or short i2c ack reply to a write,
	 * we need to switch to WRITE_STATUS_UPDATE to drain the
	 * rest of the message
	 */
	if ((msg->request & (~(u8)DP_AUX_I2C_MOT)) == (u8)DP_AUX_I2C_WRITE) {
		msg->request &= (u8)DP_AUX_I2C_MOT;
		msg->request |= (u8)DP_AUX_I2C_WRITE_STATUS_UPDATE;
	}
}

static int drm_dp_i2c_do_msg(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	unsigned int retry, defer_i2c = 0U;
	int ret, ret1;
	/*
	 * DP1.2 sections 2.7.7.1.5.6.1 and 2.7.7.1.6.6.1: A DP Source device
	 * is required to retry at least seven times upon receiving AUX_DEFER
	 * before giving up the AUX transaction.
	 *
	 * We also try to account for the i2c bus speed.
	 */
	int max_retries = 7;

	for (retry = 0U; retry < ((unsigned int)max_retries + defer_i2c);
	     retry++) {
		ret = (int)aux->transfer(aux, msg);
		ret1 = 0;
		if (ret < 0) {
			if (ret == -EBUSY) {
				continue;
			}

			/*
			 * While timeouts can be errors, they're usually normal
			 * behavior (for instance, when a driver tries to
			 * communicate with a non-existent DisplayPort device).
			 * Avoid spamming the kernel log with timeout errors.
			 */
			if (ret == -ETIMEDOUT) {
				(void)printf("%s: transaction timed out\n",
				       aux->name);
			} else {
				(void)printf("%s: transaction failed: %d\n",
				       aux->name, ret);
			}
			return ret;
		}

		switch (msg->reply & DP_AUX_NATIVE_REPLY_MASK) {
		case DP_AUX_NATIVE_REPLY_ACK:
			/*
			 * For I2C-over-AUX transactions this isn't enough, we
			 * need to check for the I2C ACK reply.
			 */
			break;

		case DP_AUX_NATIVE_REPLY_NACK:
			(void)printf("%s: native nack (result=%d, size=%zu)\n",
			       aux->name, ret, msg->size);
			ret1 = -EREMOTEIO;
			break;

		case DP_AUX_NATIVE_REPLY_DEFER:
			(void)printf("%s: native defer\n", aux->name);
			/*
			 * We could check for I2C bit rate capabilities and if
			 * available adjust this interval. We could also be
			 * more careful with DP-to-legacy adapters where a
			 * long legacy cable may force very low I2C bit rates.
			 *
			 * For now just defer for long enough to hopefully be
			 * safe for all use-cases.
			 */
			udelay(AUX_RETRY_INTERVAL);
			ret1 = 1;
			break;

		default:
			(void)printf("%s: invalid native reply %#04x\n",
			       aux->name, msg->reply);
			ret1 = -EREMOTEIO;
			break;
		}
		if (ret1 < 0) {
			return ret1;
		} else if (ret1 == 1) {
			continue;
		} else {
			(void)0;
		}

		switch (msg->reply & DP_AUX_I2C_REPLY_MASK) {
		case DP_AUX_I2C_REPLY_ACK:
			/*
			 * Both native ACK and I2C ACK replies received. We
			 * can assume the transfer was successful.
			 */
			if ((unsigned long)ret != msg->size) {
				drm_dp_i2c_msg_write_status_update(msg);
			}
			break;

		case DP_AUX_I2C_REPLY_NACK:
			(void)printf("%s: I2C nack (result=%d, size=%zu)\n",
			       aux->name, ret, msg->size);
			aux->i2c_nack_count++;
			ret1 = -EREMOTEIO;
			break;

		case DP_AUX_I2C_REPLY_DEFER:
			(void)printf("%s: I2C defer\n", aux->name);
			/* DP Compliance Test 4.2.2.5 Requirement:
			 * Must have at least 7 retries for I2C defers on the
			 * transaction to pass this test
			 */
			aux->i2c_defer_count++;
			if (defer_i2c < 7U) {
				defer_i2c++;
			}
			udelay(AUX_RETRY_INTERVAL);
			drm_dp_i2c_msg_write_status_update(msg);
			ret1 = 1;
			break;

		default:
			(void)printf("%s: invalid I2C reply %#04x\n",
			       aux->name, msg->reply);
			ret1 = -EREMOTEIO;
			break;
		}
		if (ret1 < 0) {
			return ret1;
		} else if (ret1 == 1) {
			continue;
		} else {
			(void)0;
		}

		return ret;
	}

	(void)printf("%s: Too many retries, giving up\n", aux->name);
	return -EREMOTEIO;
}

static void drm_dp_i2c_msg_set_request(struct drm_dp_aux_msg *msg,
				       const struct i2c_msg *i2c_msg)
{
	msg->request = (i2c_msg->flags & (unsigned int)I2C_M_RD) > 0U ?
		(u8)DP_AUX_I2C_READ : (u8)DP_AUX_I2C_WRITE;
	if ((i2c_msg->flags & (unsigned int)I2C_M_STOP) == 0U) {
		msg->request |= (u8)DP_AUX_I2C_MOT;
	}
}

/*
 * Keep retrying drm_dp_i2c_do_msg until all data has been transferred.
 *
 * Returns an error code on failure, or a recommended transfer size on success.
 */
static int drm_dp_i2c_drain_msg(struct drm_dp_aux *aux,
				struct drm_dp_aux_msg *orig_msg)
{
	int err, ret = (int)orig_msg->size;
	struct drm_dp_aux_msg msg = *orig_msg;

	while (msg.size > 0U) {
		err = drm_dp_i2c_do_msg(aux, &msg);
		if (err <= 0) {
			return err == 0 ? -EPROTO : err;
		}

		if (err < (int)msg.size && err < ret) {
			(void)printf("%s: Reply: requested %zu bytes got %d bytes\n",
			       aux->name, msg.size, err);
			ret = err;
		}

		msg.size -= (unsigned long)err;
		msg.buffer += err;
	}

	return ret;
}

int drm_dp_i2c_xfer(struct ddc_adapter *adapter, struct i2c_msg *msgs,
		    int num)
{
	struct drm_dp_aux *aux = container_of(adapter, struct drm_dp_aux, ddc);
	unsigned int i, j;
	unsigned int transfer_size;
	struct drm_dp_aux_msg msg;
	int err = 0;

	(void)memset(&msg, 0, sizeof(msg));

	for (i = 0U; i < (unsigned int)num; i++) {
		msg.address = msgs[i].addr;
		drm_dp_i2c_msg_set_request(&msg, &msgs[i]);
		/* Send a bare address packet to start the transaction.
		 * Zero sized messages specify an address only (bare
		 * address) transaction.
		 */
		msg.buffer = NULL;
		msg.size = 0;
		err = drm_dp_i2c_do_msg(aux, &msg);

		/*
		 * Reset msg.request in case in case it got
		 * changed into a WRITE_STATUS_UPDATE.
		 */
		drm_dp_i2c_msg_set_request(&msg, &msgs[i]);

		if (err < 0) {
			break;
		}
		/* We want each transaction to be as large as possible, but
		 * we'll go to smaller sizes if the hardware gives us a
		 * short reply.
		 */
		transfer_size = DP_AUX_MAX_PAYLOAD_BYTES;
		for (j = 0; j < msgs[i].len; j += (unsigned int)msg.size) {
			msg.buffer = msgs[i].buf + j;
			msg.size = min(transfer_size, msgs[i].len - j);

			err = drm_dp_i2c_drain_msg(aux, &msg);

			/*
			 * Reset msg.request in case in case it got
			 * changed into a WRITE_STATUS_UPDATE.
			 */
			drm_dp_i2c_msg_set_request(&msg, &msgs[i]);

			if (err < 0) {
				break;
			}
			transfer_size = (unsigned int)err;
		}
		if (err < 0) {
			break;
		}
	}
	if (err >= 0) {
		err = num;
	}
	/* Send a bare address packet to close out the transaction.
	 * Zero sized messages specify an address only (bare
	 * address) transaction.
	 */
	msg.request &= ~(u8)DP_AUX_I2C_MOT;
	msg.buffer = NULL;
	msg.size = 0;
	(void)drm_dp_i2c_do_msg(aux, &msg);

	return err;
}
// PRQA S 5124 --
