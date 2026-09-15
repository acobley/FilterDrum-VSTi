//------------------------------------------------------------------------
// FilterDrum - edit controller implementation
//------------------------------------------------------------------------

#include "FilterDrumController.h"
#include "FilterDrumDsp.h"
#include "FilterDrumEditor.h"
#include "FilterDrumIDs.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace FilterDrum {

//------------------------------------------------------------------------
namespace {

/** A RangeParameter that formats its text the way the panel does. */
class FilterDrumParameter : public RangeParameter
{
public:
	FilterDrumParameter (const ParamDef& def)
	// TITLE AND UNITS MUST NOT BE NULL. RangeParameter passes them
	// straight to UString::assign, which dereferences both without a
	// null check. The symptom is not a crash in the plug-in: it is the
	// SDK VALIDATOR segfaulting during EditController::initialize, in
	// the post-build step, which sends you looking at the build system.
	// Empty string, never null.
	: RangeParameter (USTRING (def.title), def.id, USTRING (def.units), def.plainMin, def.plainMax,
	                  def.plainDefault, def.stepCount, ParameterInfo::kCanAutomate,
	                  kRootUnitId, USTRING (def.title))
	, mDef (def)
	{
	}

	void toString (ParamValue normalized, String128 string) const SMTG_OVERRIDE
	{
		char text[64] = {};

		const double plain = mDef.toPlain (normalized);

		if (mDef.type == ParamType::Bool)
		{
			std::snprintf (text, sizeof (text), "%s", (normalized >= 0.5) ? "On" : "Off");
		}
		else if (mDef.id == kOutputTrim || mDef.id == kVcfAmount)
		{
			// SIGNED, EXPLICITLY. Both of these are bipolar and the
			// centre is the interesting place: a VCF amount reading
			// "60" when it sweeps up and "-60" when it sweeps down is
			// readable, whereas "60" and "60" with the sign swallowed
			// is not.
			std::snprintf (text, sizeof (text), "%+.1f", plain);
		}
		else if (mDef.id == kCutoff)
		{
			// Hz below 1 k, kHz above, because "8000.0" and "8.00 k"
			// take the same space and only one of them can be read at
			// a glance. No decimals under 1 k - a cutoff is not a
			// tuning reference.
			if (plain >= 1000.0)
				std::snprintf (text, sizeof (text), "%.2f k", plain / 1000.0);
			else
				std::snprintf (text, sizeof (text), "%.0f", plain);
		}
		else if (mDef.type == ParamType::Log && std::strcmp (mDef.units, "ms") == 0)
		{
			// The time parameters span four decades, so the number of
			// decimals has to follow the magnitude: "0.10" at the fast
			// end, "1000" at the slow one. A fixed %.2f would print
			// "1000.00" and a fixed %.0f would print "0" for the
			// fastest attack the plug-in has.
			if (plain < 10.0)
				std::snprintf (text, sizeof (text), "%.2f", plain);
			else if (plain < 100.0)
				std::snprintf (text, sizeof (text), "%.1f", plain);
			else
				std::snprintf (text, sizeof (text), "%.0f", plain);
		}
		else
		{
			// The per-cent parameters. Whole numbers: nobody sets
			// resonance to 40.25 %.
			std::snprintf (text, sizeof (text), "%.0f", plain);
		}

		UString (string, str16BufferSize (String128)).assign (USTRING (text));
	}

private:
	ParamDef mDef;
};

} // anonymous namespace

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumController::initialize (FUnknown* context)
{
	const tresult result = EditControllerEx1::initialize (context);
	if (result != kResultOk)
		return result;

	for (int i = 0; i < kNumParams; ++i)
		parameters.addParameter (new FilterDrumParameter (kParams[i]));

	// Standard VST3 bypass, id 1000 and not part of the table. Any
	// "enabled" switch a ported DXi brings with it stays separate from
	// this one, with its own default - see FilterDrumParams.h.
	parameters.addParameter (STR16 ("Bypass"), nullptr, 1, 0,
	                         ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypass);

	// Read-only output parameters - the mechanism for a value the DSP
	// computes per block - are added here too, from id 1001 up. None
	// yet. STR16 ("") rather than nullptr for their units when there
	// are, for the reason in the banner above.

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumController::terminate ()
{
	mEditors.clear ();
	return EditControllerEx1::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumController::setComponentState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return kResultFalse;

	int32 count = 0;
	if (!streamer.readInt32 (count))
		return kResultFalse;

	// DEFAULTS FIRST, exactly as the processor's setState does, and for
	// the same reason: a short stream must not leave the last project's
	// values showing on the panel. The two halves read the identical
	// layout in the identical order - one that disagrees with the other
	// is a panel that lies about what the DSP is doing.
	for (int i = 0; i < kNumParams; ++i)
		setParamNormalized (kParams[i].id, kParams[i].defaultNormalized ());
	setParamNormalized (kBypass, 0.0);

	for (int32 i = 0; i < count; ++i)
	{
		double v = 0.0;
		if (!streamer.readDouble (v))
			return kResultFalse;
		if (i < kNumParams)
			setParamNormalized (static_cast<ParamID> (i), v);
	}

	int32 bypass = 0;
	if (streamer.readInt32 (bypass))
		setParamNormalized (kBypass, bypass ? 1.0 : 0.0);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumController::setState (IBStream* /*state*/)
{
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumController::getState (IBStream* /*state*/)
{
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumController::setParamNormalized (ParamID tag, ParamValue value)
{
	const tresult result = EditControllerEx1::setParamNormalized (tag, value);
	if (result != kResultOk)
		return result;

	// Read-only output parameters (1001 and up) would be intercepted
	// here, before the walk below, because they move every block and
	// the panel wants them aggregated rather than per-control.

	for (auto* editor : mEditors)
		editor->updateControl (tag, value);

	return result;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API FilterDrumController::createView (FIDString name)
{
	if (name && FIDStringsEqual (name, ViewType::kEditor))
		return new FilterDrumEditor (this);
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumController::notify (IMessage* message)
{
	if (message && FIDStringsEqual (message->getMessageID (), kFilterDrumSampleRateMessage))
	{
		double sampleRate = 0.0;
		if (message->getAttributes ()->getFloat (kFilterDrumSampleRateAttribute, sampleRate) == kResultOk &&
		    sampleRate > 1000.0 && sampleRate != mHostSampleRate)
		{
			mHostSampleRate = sampleRate;
			for (auto* editor : mEditors)
				editor->refreshAllReadouts ();
		}
		return kResultOk;
	}
	return EditControllerEx1::notify (message);
}

//------------------------------------------------------------------------
void FilterDrumController::editorAttached (EditorView* editor)
{
	if (auto* e = dynamic_cast<FilterDrumEditor*> (editor))
	{
		if (std::find (mEditors.begin (), mEditors.end (), e) == mEditors.end ())
			mEditors.push_back (e);
	}
}

//------------------------------------------------------------------------
void FilterDrumController::editorRemoved (EditorView* editor)
{
	editorDestroyed (editor);
}

//------------------------------------------------------------------------
void FilterDrumController::editorDestroyed (EditorView* editor)
{
	// DO NOT dynamic_cast HERE. EditorView::~EditorView() calls this,
	// and by then the FilterDrumEditor sub-object has already been
	// destroyed: the object's dynamic type is plain EditorView, so
	// dynamic_cast<FilterDrumEditor*> yields NULL and the entry would
	// silently survive in mEditors as a dangling pointer. The next
	// setParamNormalized - which walks that list unconditionally -
	// follows it.
	//
	// It normally stays out of trouble only because hosts call removed()
	// first. One that releases the view without removing it is all it
	// takes.
	//
	// Comparing the UPCAST pointer is well defined at every point in the
	// destruction sequence, so it works on both paths.
	mEditors.erase (std::remove_if (mEditors.begin (), mEditors.end (),
	                                [editor] (FilterDrumEditor* e) {
		                                return static_cast<EditorView*> (e) == editor;
	                                }),
	                mEditors.end ());
}

//------------------------------------------------------------------------
} // namespace FilterDrum
