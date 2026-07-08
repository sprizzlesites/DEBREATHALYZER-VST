#include <cmath> // For fabsf, log10f, powf, expf, fmaxf

// This is a core algorithm snippet for a "de-breathalyzer" effect,
// typically used within a VST plugin's audio processing loop.
// It implements a dynamic expander/gate to reduce low-level signals.

// Parameters (these would be exposed as VST parameters):
// float threshold_dB = -35.0f;   // Signal level below which reduction starts
// float reduction_dB = -18.0f;   // Maximum gain reduction in dB
// float attack_ms = 5.0f;      // Attack time for envelope and gain reduction
// float release_ms = 150.0f;   // Release time for envelope and gain reduction
// float sampleRate = 44100.0f; // Current audio sample rate

// Internal state variables (must persist across processing blocks, per channel):
// float envelope_state = 0.0f;       // Current smoothed signal amplitude
// float gain_reduction_state = 0.0f; // Current smoothed gain reduction factor (0.0=none, 1.0=full)

// Pre-calculated coefficients (update if sampleRate or times change):
// float attack_coeff = expf(-1.0f / (sampleRate * attack_ms / 1000.0f));
// float release_coeff = expf(-1.0f / (sampleRate * release_ms / 1000.0f));

float processDeBreathalyzerSample(float inputSample,
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
