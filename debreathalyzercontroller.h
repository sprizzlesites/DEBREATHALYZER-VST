#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "parameters.h"

namespace DeBreathalyzer
{
    class DeBreathalyzerController : public Steinberg::Vst::EditController
    {
    public:
        DeBreathalyzerController();
        ~DeBreathalyzerController() SMTG_OVERRIDE;

        // --- VST3 Overrides ---
        Steinberg::tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

        // --- Parameter handling (optional, for custom UI/display) ---
        // Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
        // Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) SMTG_OVERRIDE;
        // Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID id, Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) SMTG_OVERRIDE;

        // --- Editor handling (if a custom UI is desired) ---
        // Steinberg::tresult PLUGIN_API createView(const char* name, Steinberg::IPlugView*& view) SMTG_OVERRIDE;
    };
} // namespace DeBreathalyzer