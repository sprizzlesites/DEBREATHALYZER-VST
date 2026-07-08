#pragma once

#include "pluginterfaces/base/fstrdefs.h"

namespace DeBreathalyzer
{
    // Parameter IDs
    enum ParamID
    {
        kThresholdId = 0,
        kReductionId,
        kAttackId,
        kReleaseId,
        kNumParams
    };

    // Parameter ranges and default values
    const float kThresholdMin = -60.0f;
    const float kThresholdMax = 0.0f;
    const float kThresholdDefault = -35.0f;

    const float kReductionMin = -30.0f;
    const float kReductionMax = 0.0f;
    const float kReductionDefault = -18.0f;

    const float kAttackMin = 1.0f; // ms
    const float kAttackMax = 50.0f; // ms
    const float kAttackDefault = 5.0f; // ms

    const float kReleaseMin = 50.0f; // ms
    const float kReleaseMax = 500.0f; // ms
    const float kReleaseDefault = 150.0f; // ms

    // Component IDs for VST3
    static const Steinberg::FUID kProcessorUID(0x28970899, 0xC6544521, 0x82A1278C, 0xA26F809B);
    static const Steinberg::FUID kControllerUID(0x38970899, 0xC6544521, 0x82A1278C, 0xA26F809B);

    static const char* kPluginName = "DeBreathalyzer";
    static const char* kPluginVendor = "YourCompany";
    static const char* kPluginURL = "www.yourcompany.com";
    static const char* kPluginEmail = "info@yourcompany.com";
} // namespace DeBreathalyzer