/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Contrast autofocus for the simple software ISP
 */

#pragma once

#include "algorithm.h"

namespace libcamera {

namespace ipa::soft::algorithms {

class Af : public Algorithm
{
public:
	int init(IPAContext &context, const YamlObject &tuningData) override;
	int configure(IPAContext &context, const IPAConfigInfo &configInfo) override;
	void queueRequest(typename Module::Context &context,
			  const uint32_t frame,
			  typename Module::FrameContext &frameContext,
			  const ControlList &controls) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const SwIspStats *stats,
		     ControlList &metadata) override;

private:
	void startScan(IPAContext &context);
	void processScan(IPAContext &context, uint64_t sharpness);
	int32_t maxPosition_;
};

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
