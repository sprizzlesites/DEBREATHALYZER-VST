#include "debreathalyzercontroller.h"
#include "parameters.h"
#include "public.sdk/source/vst/vstparameters.h"

namespace DeBreathalyzer
{
    // --- DeBreathalyzerController Implementation ---

    DeBreathalyzerController::DeBreathalyzerController()
    {
    }

    DeBreathalyzerController::~DeBreathalyzerController()
    {
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerController::initialize(FUnknown* context)
    {
        Steinberg::tresult result = EditController::initialize(context);
        if (result != Steinberg::kResultOk)
        {
            return result;
        }

        // Register our parameters again, so the controller knows about them
        // This is important for generic UI and host automation.
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

    Steinberg::tresult PLUGIN_API DeBreathalyzerController::terminate()
    {
        return EditController::terminate();
    }

    // Optional: Implement getParamStringByValue and getParamValueByString
    // if you want custom string representations for parameters beyond the default.
    /*
    Steinberg::tresult PLUGIN_API DeBreathalyzerController::getParamStringByValue(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string)
    {
        // ... custom string formatting ...
        return EditController::getParamStringByValue(id, valueNormalized, string);
    }

    Steinberg::tresult PLUGIN_API DeBreathalyzerController::getParamValueByString(Steinberg::Vst::ParamID id, Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized)
    {
        // ... custom string parsing ...
        return EditController::getParamValueByString(id, string, valueNormalized);
    }
    */

    // Optional: Implement createView if you have a custom editor (GUI)
    /*
    Steinberg::tresult PLUGIN_API DeBreathalyzerController::createView(const char* name, Steinberg::IPlugView*& view)
    {
        // if (strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
        // {
        //     view = new MyEditorView(this);
        //     return Steinberg::kResultOk;
        // }
        return Steinberg::kResultFalse;
    }
    */

} // namespace DeBreathalyzer