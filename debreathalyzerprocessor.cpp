#include "debreathalyzerprocessor.h"
#include "parameters.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cmath> // For fabsf, log10f, powf, expf, fmaxf
#include <algorithm> // For std::max

namespace DeBreathalyzer
{
    // --- Our custom processing function (from the snippet) ---
    // Moved here for implementation, declared in header.
    float DeBreathalyzerProcessor::processDeBreathalyzerSample(float inputSample,
                                                                 float threshold_dB, float reduction_dB,
                                                                 float attack_coeff, float release_coeff,
                                                                 float& envelope_state, float& gain_reduction_state)
    {
        // 1. Envelope Follower
        float abs_input = fabsf(inputSample);
        if (abs_input > envelope_state) {
            envelope_state = abs_input + attack_coeff * (envelope_state - abs_input);
        } else {
            envelope_state = abs_input + release_coeff * (envelope_state - abs_input);
        }
        envelope_state = fmaxf(envelope_state, 0.000001f); // Prevent log(0)

        // 2. Convert envelope to dB
        float envelope_dB = 20.0f * log10f(envelope_state);

        // 3. Determine target gain reduction factor
        float target_reduction_factor = 0.0f; // 0.0 = no reduction, 1.0 = full reduction
        if (envelope_dB < threshold_dB) {
            target_reduction_factor = 1.0f; // Apply full reduction when below threshold
        }

        // 4. Smooth the actual gain reduction applied
        float current_smoothing_coeff = (target_reduction_factor > gain_reduction_state) ? attack_coeff : release_coeff;
        gain_reduction_state = target_reduction_factor + current_smoothing_coeff * (gain_reduction_state - target_reduction_factor);

        // 5. Calculate final gain multiplier
        float actual_reduction_dB = gain_reduction_state * reduction_dB;
        float gain_multiplier = powf(10.0f, actual_reduction_dB / 20.0f);

        // 6. Apply gain to the sample
        return inputSample * gain_multiplier;
    }

    // --- DeBreathalyzerProcessor Implementation ---

    DeBreathalyzerProcessor::DeBreathalyzerProcessor()
    : AudioEffect()
    , mThreshold(kThresholdDefault)
    , mReduction(kReductionDefault)
    , mAttack(kAttackDefault)
    , mRelease(kReleaseDefault)
    , mAttackCoeff(0.0f)
    , mReleaseCoeff(0.0f)
    , mSampleRate(0.0f)
    {
        set  <false> (kProcessor);
        // We do not want to receive MIDI events in this plugin
        set  <false> (kMidi);
        // We want to receive 1 audio input and 1 audio output
        set  <1> (kNumInputs);
        set  <1> (kNumOutputs);
    }

    DeBreathalyzerProcessor::~DeBreathalyzerProcessor()
    {
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerProcessor::initialize(FUnknown* context)
    {
        Steinberg::tresult result = AudioEffect::initialize(context);
        if (result != Steinberg::kResultOk)
        {
            return result;
        }

        // Register our parameters
        addParameter(new Steinberg::Vst::RangeParameter(
            USTRING("Threshold"), kThresholdId, USTRING("dB"),
            kThresholdMin, kThresholdMax, kThresholdDefault, 0,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsBipolar,
            0, USTRING("%1.1f")));

        addParameter(new Steinberg::Vst::RangeParameter(
            USTRING("Reduction"), kReductionId, USTRING("dB"),
            kReductionMin, kReductionMax, kReductionDefault, 0,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsBipolar,
            0, USTRING("%1.1f")));

        addParameter(new Steinberg::Vst::RangeParameter(
            USTRING("Attack"), kAttackId, USTRING("ms"),
            kAttackMin, kAttackMax, kAttackDefault, 0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            0, USTRING("%1.1f")));

        addParameter(new Steinberg::Vst::RangeParameter(
            USTRING("Release"), kReleaseId, USTRING("ms"),
            kReleaseMin, kReleaseMax, kReleaseDefault, 0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            0, USTRING("%1.1f")));

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerProcessor::terminate()
    {
        mChannelStates.clear();
        return AudioEffect::terminate();
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerProcessor::setActive(Steinberg::TBool state)
    {
        if (state) // plugin is activating
        {
            // Initialize per-channel state
            mChannelStates.resize(processSetup.maxSamplesPerBlock); // This is a placeholder; should be based on actual channel count.
                                                                    // A better approach is to resize when process() is called and input bus is known.
                                                                    // For simplicity, we'll assume stereo here or resize later.
            if (processSetup.symbolicSampleSize == Steinberg::Vst::kSample32)
            {
                mChannelStates.resize(processSetup.maxChannels); // Assuming maxChannels is for the main bus
            }
            else if (processSetup.symbolicSampleSize == Steinberg::Vst::kSample64)
            {
                mChannelStates.resize(processSetup.maxChannels);
            }
            else
            {
                // Handle unsupported sample size
                return Steinberg::kResultFalse;
            }

            for (auto& cs : mChannelStates)
            {
                cs.envelope_state = 0.0f;
                cs.gain_reduction_state = 0.0f;
            }
            updateCoefficients();
        }
        else // plugin is deactivating
        {
            mChannelStates.clear();
        }
        return AudioEffect::setActive(state);
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerProcessor::setupProcessing(Steinberg::Vst::ProcessSetup& newSetup)
    {
        // We get the sample rate here
        mSampleRate = newSetup.sampleRate;
        updateCoefficients(); // Update coefficients based on new sample rate
        return AudioEffect::setupProcessing(newSetup);
    }

    void DeBreathalyzerProcessor::updateCoefficients()
    {
        if (mSampleRate > 0.0f)
        {
            // Convert ms to seconds for calculations
            float attack_s = mAttack / 1000.0f;
            float release_s = mRelease / 1000.0f;

            // Calculate coefficients for exponential smoothing
            // exp(-1 / (sampleRate * time_in_seconds))
            mAttackCoeff = expf(-1.0f / (mSampleRate * attack_s));
            mReleaseCoeff = expf(-1.0f / (mSampleRate * release_s));
        }
        else
        {
            mAttackCoeff = 0.0f;
            mReleaseCoeff = 0.0f;
        }
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerProcessor::process(Steinberg::Vst::ProcessData& data)
    {
        // If there are parameter changes, apply them
        if (data.inputParameterChanges)
        {
            int32 numParamsChanged = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < numParamsChanged; i++)
            {
                Steinberg::Vst::IParamValueQueue* paramQueue = data.inputParameterChanges->getParameterData(i);
                if (paramQueue)
                {
                    Steinberg::Vst::ParamValue value;
                    int32 sampleOffset;
                    int32 numPoints = paramQueue->getPointCount();
                    if (numPoints > 0)
                    {
                        // Get the last value, assuming we only care about the final state per block
                        // For sample-accurate automation, you'd iterate through all points.
                        paramQueue->getPoint(numPoints - 1, sampleOffset, value);

                        switch (paramQueue->getParameterID())
                        {
                            case kThresholdId:
                                mThreshold = static_cast<float>(value);
                                break;
                            case kReductionId:
                                mReduction = static_cast<float>(value);
                                break;
                            case kAttackId:
                                mAttack = static_cast<float>(value);
                                updateCoefficients(); // Re-calculate if attack/release changes
                                break;
                            case kReleaseId:
                                mRelease = static_cast<float>(value);
                                updateCoefficients(); // Re-calculate if attack/release changes
                                break;
                        }
                    }
                }
            }
        }

        // No inputs or outputs, nothing to do
        if (!data.inputs || !data.outputs)
        {
            return Steinberg::kResultOk;
        }

        // We only support one input bus and one output bus for simplicity
        // and assuming stereo or mono processing.
        Steinberg::Vst::IBusBuffer* inputBus = data.inputs[0];
        Steinberg::Vst::IBusBuffer* outputBus = data.outputs[0];

        // Ensure channel states are correctly sized for the current processing block
        if (mChannelStates.size() != inputBus->numChannels)
        {
            mChannelStates.resize(inputBus->numChannels);
            for (auto& cs : mChannelStates)
            {
                cs.envelope_state = 0.0f;
                cs.gain_reduction_state = 0.0f;
            }
        }


        // Process audio samples
        if (data.symbolicSampleSize == Steinberg::Vst::kSample32)
        {
            for (int32 channel = 0; channel < inputBus->numChannels; ++channel)
            {
                float* inputChannel = inputBus->getChannelData32(channel);
                float* outputChannel = outputBus->getChannelData32(channel);
                ChannelState& cs = mChannelStates[channel];

                for (int32 sample = 0; sample < data.numSamples; ++sample)
                {
                    outputChannel[sample] = processDeBreathalyzerSample(
                        inputChannel[sample],
                        mThreshold, mReduction,
                        mAttackCoeff, mReleaseCoeff,
                        cs.envelope_state, cs.gain_reduction_state
                    );
                }
            }
        }
        else if (data.symbolicSampleSize == Steinberg::Vst::kSample64)
        {
            for (int32 channel = 0; channel < inputBus->numChannels; ++channel)
            {
                double* inputChannel = inputBus->getChannelData64(channel);
                double* outputChannel = outputBus->getChannelData64(channel);
                ChannelState& cs = mChannelStates[channel];

                for (int32 sample = 0; sample < data.numSamples; ++sample)
                {
                    // Convert double to float for processing, then back to double
                    outputChannel[sample] = static_cast<double>(processDeBreathalyzerSample(
                        static_cast<float>(inputChannel[sample]),
                        mThreshold, mReduction,
                        mAttackCoeff, mReleaseCoeff,
                        cs.envelope_state, cs.gain_reduction_state
                    ));
                }
            }
        }
        else
        {
            // Unsupported sample size, do nothing or return error
            return Steinberg::kResultFalse;
        }

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerProcessor::getEditorClassID(Steinberg::TUID classID)
    {
        // We'll create a controller later, so return its UID
        // For now, no editor, so return kResultFalse
        return Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerProcessor::setState(Steinberg::IBStream* state)
    {
        // Called when loading a preset or project
        if (!state)
            return Steinberg::kResultFalse;

        float savedThreshold = 0.f;
        if (state->read(&savedThreshold, sizeof(float)) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        float savedReduction = 0.f;
        if (state->read(&savedReduction, sizeof(float)) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        float savedAttack = 0.f;
        if (state->read(&savedAttack, sizeof(float)) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        float savedRelease = 0.f;
        if (state->read(&savedRelease, sizeof(float)) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        mThreshold = savedThreshold;
        mReduction = savedReduction;
        mAttack = savedAttack;
        mRelease = savedRelease;

        updateCoefficients(); // Ensure coefficients are up-to-date after loading state

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerProcessor::getState(Steinberg::IBStream* state)
    {
        // Called when saving a preset or project
        if (!state)
            return Steinberg::kResultFalse;

        // Save our parameters
        if (state->write(&mThreshold, sizeof(float)) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (state->write(&mReduction, sizeof(float)) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (state->write(&mAttack, sizeof(float)) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (state->write(&mRelease, sizeof(float)) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        return Steinberg::kResultOk;
    }
} // namespace DeBreathalyzer