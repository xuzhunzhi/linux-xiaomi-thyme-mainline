// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include "q6dsp-common.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/of.h>
#include "q6voice-common.h"

/* Shared with AVCS custom topology loading even when Q6Voice is disabled. */
/*
 * Addresses handed to the DSP are not bare addresses. The DSP maps them
 * through the SMMU itself, so it has to be told which stream to translate
 * against, and that is carried in the top half of the address: the low bits of
 * this device's stream id, shifted up out of the way of the address proper.
 *
 * Sending a bare address means the DSP translates against stream zero, maps
 * whatever that lands on, and reads it. On this SoC that resets the board.
 *
 * The mask is the vendor's, and is the same across every SoC in that tree; it
 * is the part of the stream id that selects the context bank.
 */
#define Q6VOICE_SMMU_SID_MASK		0xf
#define Q6VOICE_SMMU_SID_SHIFT		32

u64 q6voice_dsp_address(struct device *dev, dma_addr_t addr)
{
	struct of_phandle_args args;
	u64 sid = 0;

	if (!of_parse_phandle_with_args(dev->of_node, "iommus", "#iommu-cells",
					0, &args)) {
		if (args.args_count)
			sid = args.args[0] & Q6VOICE_SMMU_SID_MASK;
		of_node_put(args.np);
	}

	return addr | (sid << Q6VOICE_SMMU_SID_SHIFT);
}
EXPORT_SYMBOL_GPL(q6voice_dsp_address);

int q6dsp_map_channels(u8 ch_map[PCM_MAX_NUM_CHANNEL], int ch)
{
	memset(ch_map, 0, PCM_MAX_NUM_CHANNEL);

	switch (ch) {
	case 1:
		ch_map[0] = PCM_CHANNEL_FC;
		break;
	case 2:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		break;
	case 3:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_FC;
		break;
	case 4:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_LS;
		ch_map[3] = PCM_CHANNEL_RS;
		break;
	case 5:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_FC;
		ch_map[3] = PCM_CHANNEL_LS;
		ch_map[4] = PCM_CHANNEL_RS;
		break;
	case 6:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_LFE;
		ch_map[3] = PCM_CHANNEL_FC;
		ch_map[4] = PCM_CHANNEL_LS;
		ch_map[5] = PCM_CHANNEL_RS;
		break;
	case 8:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_LFE;
		ch_map[3] = PCM_CHANNEL_FC;
		ch_map[4] = PCM_CHANNEL_LS;
		ch_map[5] = PCM_CHANNEL_RS;
		ch_map[6] = PCM_CHANNEL_LB;
		ch_map[7] = PCM_CHANNEL_RB;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(q6dsp_map_channels);

int q6dsp_get_channel_allocation(int channels)
{
	int channel_allocation;

	/* HDMI spec CEA-861-E: Table 28 Audio InfoFrame Data Byte 4 */
	switch (channels) {
	case 2:
		channel_allocation = 0;
		break;
	case 3:
		channel_allocation = 0x02;
		break;
	case 4:
		channel_allocation = 0x06;
		break;
	case 5:
		channel_allocation = 0x0A;
		break;
	case 6:
		channel_allocation = 0x0B;
		break;
	case 7:
		channel_allocation = 0x12;
		break;
	case 8:
		channel_allocation = 0x13;
		break;
	default:
		return -EINVAL;
	}

	return channel_allocation;
}
EXPORT_SYMBOL_GPL(q6dsp_get_channel_allocation);

MODULE_DESCRIPTION("ASoC MSM QDSP6 helper functions");
MODULE_LICENSE("GPL v2");
