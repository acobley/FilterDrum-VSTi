//------------------------------------------------------------------------
// FilterDrum - plug-in factory
//------------------------------------------------------------------------

#include "FilterDrumController.h"
#include "FilterDrumIDs.h"
#include "FilterDrumProcessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "FilterDrum"

using namespace Steinberg::Vst;
using namespace FilterDrum;

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF (stringCompanyName, "https://github.com/", "mailto:aecobley@googlemail.com")

	// FilterDrum is an INSTRUMENT, and specifically a drum one:
	// PlugType::kInstrumentDrum. This string is what a host reads to
	// decide which list to put it in, and it has to agree with the
	// 'aumu' type code in resource/au-info.plist and with the buses the
	// processor adds - notes in, stereo out, no audio input.
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kFilterDrumProcessorUID),
	            PClassInfo::kManyInstances,
	            kVstAudioEffectClass,
	            stringPluginName,
	            Vst::kDistributable,
	            PlugType::kInstrumentDrum,
	            FULL_VERSION_STR,
	            kVstVersionString,
	            FilterDrumProcessor::createInstance)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (kFilterDrumControllerUID),
	            PClassInfo::kManyInstances,
	            kVstComponentControllerClass,
	            stringPluginName "Controller",
	            0,
	            "",
	            FULL_VERSION_STR,
	            kVstVersionString,
	            FilterDrumController::createInstance)

END_FACTORY
