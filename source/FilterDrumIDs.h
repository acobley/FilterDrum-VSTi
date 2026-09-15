//------------------------------------------------------------------------
// FilterDrum - class UIDs and message identifiers
//
// These UIDs were generated fresh for this plug-in from os.urandom.
// NEVER CHANGE THEM once a build has been shipped: hosts store them in
// the project file, so a changed UID means every existing session
// silently loses the plug-in. The same is true of the four-character
// codes in resource/au-info.plist and of both bundle identifiers in
// CMakeLists.txt.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace FilterDrum {

static const Steinberg::FUID kFilterDrumProcessorUID  (0x155EA437, 0xC565701B, 0xCA9B562F, 0x151DE2EE);
static const Steinberg::FUID kFilterDrumControllerUID (0x813A44E4, 0x5D8B2936, 0xA835509C, 0x6BCEF690);

// The plug-in category is declared once, in FilterDrumEntry.cpp, using the
// SDK's own PlugType::kInstrumentDrum - not repeated as a string here.

//------------------------------------------------------------------------
// Processor <-> controller messages
//
// RESERVED SPACE, and the rule that governs it. Every message must
// travel on the UI THREAD. sendMessage from process() returns success
// and is then silently discarded by the host's connection proxy - it
// does nothing and tells you nothing, so a value the DSP produces per
// block can never come out this way. Anything the DSP computes per block
// that the panel needs goes out through data.outputParameterChanges
// instead, as a hidden read-only parameter; see the note beside kBypass
// in FilterDrumParams.h.
//
// Messages ARE legitimate from setActive, setState and notify, which is
// where the one below is sent from. Add new ones here, each with a
// comment saying which of those it is sent from.
//
// The other limitation to know before adding one: if a host never
// connects the two components, a message is never delivered and nothing
// says so. That is why parameters, which cannot be lost this way, carry
// everything that can be expressed as a number.
//------------------------------------------------------------------------

/** Processor -> controller, from setActive: the sample rate the DSP is
    actually running at.

    The panel needs it because anything drawn from a filter shape is a
    function of f/fs, and a display that assumed 44.1 k while the DSP ran
    at 96 k would be drawing something nobody is hearing. There is no
    filter here yet - the panel only reports the rate - but the route is
    in place so the first one does not have to invent it. */
static const char* const kFilterDrumSampleRateMessage   = "FilterDrumSampleRate";
static const char* const kFilterDrumSampleRateAttribute = "SampleRate";

//------------------------------------------------------------------------
} // namespace FilterDrum
