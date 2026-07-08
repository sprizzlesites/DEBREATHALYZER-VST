#include "public.sdk/source/main/pluginfactory.h"
#include "debreathalyzerprocessor.h"
#include "debreathalyzercontroller.h"
#include "parameters.h"

// Define the plugin factory for the VST3 host.
// This registers our processor and controller classes with their unique IDs.

// Define the module properties (vendor, URL, email)
#define stringCompanyName   "Sprizzle"
#define stringCompanyWeb    "www.sprizzle.io"
#define stringCompanyEmail  "spryte@sprizzle.io"

// Helper function to create the processor component
static FUnknown* createProcessorInstance(void*)
{
    return (Steinberg::Vst::IAudioProcessor*)new DeBreathalyzer::DeBreathalyzerProcessor();
}

// Helper function to create the controller component
static FUnknown* createControllerInstance(void*)
{
    return (Steinberg::Vst::IEditController*)new DeBreathalyzer::DeBreathalyzerController();
}

// Register the plugin components
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    // Processor component
    DEF_CLASS2(INLINE_UID_FROM_FUID(DeBreathalyzer::kProcessorUID),
               Steinberg::PClassInfo::kManyInstances,
               Steinberg::Vst::kAudioModuleClass,
               DeBreathalyzer::kPluginName,
               Vst::kProcessorComponentCategory,
               FULL_VERSION_STR,
               "", // No subcategories
               createProcessorInstance)

    // Controller component
    DEF_CLASS2(INLINE_UID_FROM_FUID(DeBreathalyzer::kControllerUID),
               Steinberg::PClassInfo::kManyInstances,
               Steinberg::Vst::kControllerClass,
               DeBreathalyzer::kPluginName "Controller", // Controller name
               "", // No subcategories
               FULL_VERSION_STR,
               "", // No subcategories
               createControllerInstance)

END_FACTORY
