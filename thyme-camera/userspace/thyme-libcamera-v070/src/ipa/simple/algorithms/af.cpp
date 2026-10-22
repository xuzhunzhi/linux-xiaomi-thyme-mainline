/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Contrast autofocus for the simple software ISP
 */

#include "af.h"

#include <algorithm>
#include <cmath>
#include <stdint.h>

#include <linux/v4l2-controls.h>

#include <libcamera/base/log.h>

#include "control_ids.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftAf)

namespace ipa::soft::algorithms {

static constexpr unsigned int kAeStableSamples = 3;

int Af::init(IPAContext &context,
	     const YamlObject &tuningData)
{
	maxPosition_ = tuningData["maxPosition"].get<int32_t>().value_or(INT32_MAX);
	context.ctrlMap[&controls::AfMode] =
		ControlInfo(controls::AfModeValues, controls::AfModeContinuous);
	context.ctrlMap[&controls::AfTrigger] =
		ControlInfo(controls::AfTriggerValues);
	/* LensPosition needs a calibrated DAC-to-dioptre curve. */

	return 0;
}

int Af::configure(IPAContext &context, const IPAConfigInfo &configInfo)
{
	auto lens = configInfo.lensControls.find(V4L2_CID_FOCUS_ABSOLUTE);
	if (lens == configInfo.lensControls.end()) {
		context.configuration.focus.available = false;
		LOG(IPASoftAf, Warning) << "No focus lens control available";
		return 0;
	}

	context.configuration.focus.available = true;
	context.configuration.focus.min = lens->second.min().get<int32_t>();
	context.configuration.focus.max = lens->second.max().get<int32_t>();
	context.configuration.focus.max = std::clamp(maxPosition_,
		context.configuration.focus.min, context.configuration.focus.max);

	const int32_t range = std::max(1, context.configuration.focus.max -
						  context.configuration.focus.min);
	context.configuration.focus.coarseStep = std::max(1, range / 8);
	context.configuration.focus.fineStep = std::max(1, range / 40);

	auto &af = context.activeState.af;
	af.mode = controls::AfModeContinuous;
	af.state = controls::AfStateScanning;
	af.position = lens->second.def().get<int32_t>();
	af.scanning = false;
	af.pendingScan = true;

	LOG(IPASoftAf, Info)
		<< "Focus range " << context.configuration.focus.min << "-"
		<< context.configuration.focus.max << ", steps "
		<< context.configuration.focus.coarseStep << "/"
		<< context.configuration.focus.fineStep;

	return 0;
}

void Af::startScan(IPAContext &context)
{
	if (!context.configuration.focus.available)
		return;

	auto &af = context.activeState.af;
	af.pendingScan = false;
	af.scanning = true;
	af.fineScan = false;
	af.state = controls::AfStateScanning;
	af.scanStart = context.configuration.focus.min;
	af.scanEnd = context.configuration.focus.max;
	af.scanStep = context.configuration.focus.coarseStep;
	af.position = af.scanStart;
	af.bestPosition = af.position;
	af.bestSharpness = 0;
	af.settleSamples = 1;
	af.lowSharpnessSamples = 0;

	LOG(IPASoftAf, Debug) << "Starting autofocus scan";
}

void Af::queueRequest([[maybe_unused]] typename Module::Context &context,
		      [[maybe_unused]] const uint32_t frame,
		      [[maybe_unused]] typename Module::FrameContext &frameContext,
		      const ControlList &requestControls)
{
	if (!context.configuration.focus.available)
		return;

	auto &af = context.activeState.af;

	if (const auto mode = requestControls.get(controls::AfMode)) {
		if (*mode != af.mode) {
			af.mode = *mode;
			if (af.mode == controls::AfModeContinuous) {
				af.pendingScan = true;
				af.scanning = false;
				af.state = controls::AfStateScanning;
			} else {
				af.pendingScan = false;
				af.scanning = false;
				af.state = controls::AfStateIdle;
			}
		}
	}

	if (af.mode == controls::AfModeManual) {
		af.pendingScan = false;
		af.scanning = false;
		return;
	}

	if (af.mode != controls::AfModeAuto)
		return;

	if (const auto trigger = requestControls.get(controls::AfTrigger)) {
		if (*trigger == controls::AfTriggerStart && !af.scanning &&
		    !af.pendingScan) {
			af.pendingScan = true;
			af.state = controls::AfStateScanning;
		} else if (*trigger == controls::AfTriggerCancel &&
			   (af.scanning || af.pendingScan)) {
			af.pendingScan = false;
			af.scanning = false;
			af.state = controls::AfStateIdle;
		}
	}
}

void Af::processScan(IPAContext &context, uint64_t sharpness)
{
	auto &af = context.activeState.af;

	if (af.settleSamples) {
		--af.settleSamples;
		return;
	}

	LOG(IPASoftAf, Debug)
		<< (af.fineScan ? "Fine" : "Coarse")
		<< " focus sample at " << af.position
		<< " (sharpness " << sharpness << ")";

	if (sharpness > af.bestSharpness) {
		af.bestSharpness = sharpness;
		af.bestPosition = af.position;
	}

	if (af.position + af.scanStep <= af.scanEnd) {
		af.position += af.scanStep;
		af.settleSamples = 1;
		return;
	}

	if (!af.fineScan) {
		const int32_t radius = context.configuration.focus.coarseStep;
		af.fineScan = true;
		af.scanStart = std::max(context.configuration.focus.min,
					af.bestPosition - radius);
		af.scanEnd = std::min(context.configuration.focus.max,
				      af.bestPosition + radius);
		af.scanStep = context.configuration.focus.fineStep;
		af.position = af.scanStart;
		af.bestSharpness = 0;
		af.settleSamples = 1;
		return;
	}

	af.scanning = false;
	af.position = af.bestPosition;
	af.lockedSharpness = af.bestSharpness;
	af.state = af.bestSharpness ? controls::AfStateFocused
				    : controls::AfStateFailed;
	af.settleSamples = 1;

	LOG(IPASoftAf, Info)
		<< "Autofocus completed at " << af.position
		<< " (sharpness " << af.bestSharpness << ")";
}

void Af::process(IPAContext &context,
		 [[maybe_unused]] const uint32_t frame,
		 IPAFrameContext &frameContext,
		 const SwIspStats *stats,
		 ControlList &metadata)
{
	auto &af = context.activeState.af;

	if (context.configuration.focus.available && stats->valid) {
		if (af.pendingScan &&
		    context.activeState.agc.stableSamples >= kAeStableSamples)
			startScan(context);

		if (af.scanning) {
			processScan(context, stats->sharpness);
		} else if (af.mode == controls::AfModeContinuous &&
			   af.state == controls::AfStateFocused &&
			   !af.settleSamples && af.lockedSharpness &&
			   stats->sharpness * 100 < af.lockedSharpness * 65) {
			if (++af.lowSharpnessSamples >= 3) {
				af.pendingScan = true;
				af.state = controls::AfStateScanning;
				af.lowSharpnessSamples = 0;
			}
		} else {
			af.lowSharpnessSamples = 0;
			if (af.settleSamples)
				--af.settleSamples;
		}
	}

	frameContext.lens.position = af.position;
	metadata.set(controls::AfMode, af.mode);
	metadata.set(controls::AfState, af.state);
}

REGISTER_IPA_ALGORITHM(Af, "Af")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
