//------------------------------------------------------------------------
// FilterDrum - edit controller
//
// The half of the plug-in that talks to the host about parameters and
// owns the panel. It does NOT share memory with the processor - they may
// not even be in the same process - which is the reason for two rules
// that run through this project:
//
//   * anything the panel displays that the DSP computes comes from one
//     shared function both call (FilterDrumDsp.h), not from two copies
//     of the arithmetic;
//   * a value the DSP produces per block reaches here through
//     data.outputParameterChanges, never through sendMessage from
//     process() (FilterDrumIDs.h).
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumParams.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <vector>

namespace FilterDrum {

class FilterDrumEditor;

//------------------------------------------------------------------------
class FilterDrumController : public Steinberg::Vst::EditControllerEx1
{
public:
	FilterDrumController () = default;
	~FilterDrumController () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IEditController*)new FilterDrumController;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	/** The PROCESSOR's state, which is the one that matters - it is what
	    the host saved. Reads the identical layout getState wrote. */
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	/** The CONTROLLER's own state - panel-only settings with no
	    parameter behind them. There are none yet, so both are no-ops
	    that return kResultOk rather than kResultFalse: a host that saves
	    an empty controller state and gets a failure back logs an error
	    for nothing. */
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
	                                                  Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

	/** Receives the sample rate the processor is running at. */
	Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

	/** The rate the DSP is actually running at, for the panel's
	    readouts. 44100 until the processor says otherwise. */
	double getHostSampleRate () const { return mHostSampleRate; }

	void editorAttached (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
	void editorRemoved (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
	void editorDestroyed (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;

private:
	std::vector<FilterDrumEditor*> mEditors;
	double mHostSampleRate = 44100.0;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
