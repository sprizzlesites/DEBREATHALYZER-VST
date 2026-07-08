#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "parameters.h"

namespace DeBreathalyzer
{
    // A structure to hold the state of the de-breathalyzer algorithm
    // per audio channel.
    struct ChannelState
    {
        float envelope_state = 0.0f;
        float gain_reduction_state = 0.0f;
    };

    class DeBreathalyzerProcessor : public Steinberg::Vst::AudioEffect
    {
    public:
        DeBreathalyzerProcessor();
        ~DeBreathalyzerProcessor() SMTG_OVERRIDE;

        // --- VST3 Overrides ---
        Steinberg::tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API getEditorClassID(Steinberg::TUID classID) SMTG_OVERRIDE;

        // --- Parameter Access ---
        Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

        // --- Our custom processing function (from the snippet) ---
        float processDeBreathalyzerSample(float inputSample,
                                           float threshold_dB, float reduction_dB,
                                           float attack_coeff, float release_coeff,
                                           float& envelope_state, float& gain_reduction_state);

    protected:
        // Current parameter values
        float mThreshold;
        float mReduction;
        float mAttack; // in ms
        float mRelease; // in ms

        // Pre-calculated coefficients
        float mAttackCoeff;
        float mReleaseCoeff;

        // Processing setup
        float mSampleRate;

        // Per-channel state for the de-breathalyzer algorithm
        std::vector<ChannelState> mChannelStates;

        // Helper to update coefficients when sample rate or parameter values change
        void updateCoefficients();
    };
} // namespace DeBreathalyzer