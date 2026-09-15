//------------------------------------------------------------------------
// FilterDrum - audio processor
//
// FilterDrum is an INSTRUMENT: an event input and one stereo audio
// output, no audio input. That decision reaches FOUR places and they
// must all agree, or the plug-in loads in the wrong list, or auval
// rejects the Audio Unit:
//
//   * PlugType::kInstrumentDrum in FilterDrumEntry.cpp
//   * the buses added in initialize() below
//   * what setBusArrangements accepts
//   * the single 0-in / 2-out entry in resource/au-info.plist
//
// auval checks the last two against each other.
//
// THE VOICE IS MONOPHONIC. One drum, retriggered - so there is no voice
// allocation here at all, and note-on simply strikes it. See the banner
// on FilterDrumDsp.h for the signal path and on AREnvelope for why
// note-off is deliberately ignored.
//
// THE PROCESS CONTEXT IS NOT ASKED FOR, and this is the note about why
// that is a decision rather than an omission.
//
//   Since VST3 3.7 the ProcessContext is OPT-IN and the default is NO
//   FLAGS. Without a getProcessContextRequirements override,
//   data.processContext arrives with nothing valid in it: everything
//   that reads the tempo silently gets 120 in every host, the bar lines
//   land nowhere, and the validator prints "- None" rather than
//   complaining. A drum machine that launches on the bar would simply
//   never launch and nothing would say why.
//
//   Nothing here reads the tempo yet, so the override is absent
//   deliberately. THE MOMENT ANYTHING DOES - a sync division, a bar
//   launch, a tempo readout - add it back as:
//
//       Steinberg::uint32 PLUGIN_API getProcessContextRequirements ()
//           SMTG_OVERRIDE
//       {
//           processContextRequirements.needTempo ();
//           processContextRequirements.needTransportState ();
//           processContextRequirements.needProjectTimeMusic ();
//           processContextRequirements.needTimeSignature ();
//           return AudioEffect::getProcessContextRequirements ();
//       }
//
//   asking for only the fields actually read. AudioEffect already
//   implements IProcessContextRequirements; naming the fields is all
//   that is needed.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumDsp.h"
#include "FilterDrumParams.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <vector>

namespace FilterDrum {

//------------------------------------------------------------------------
class FilterDrumProcessor : public Steinberg::Vst::AudioEffect
{
public:
	FilterDrumProcessor ();

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IAudioProcessor*)new FilterDrumProcessor;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
	                                                  Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	/** How long the voice keeps sounding after the last note.

	    DECLARED RATHER THAN PORTED, which is what PORTING-GUIDE.md
	    section 5 asks for: the host feeds silence for this long and the
	    line flushes through the normal path, instead of the plug-in
	    having to answer questions about its own tail. Without it a host
	    is entitled to stop calling process() the moment the notes stop,
	    and every hit gets truncated at its note length - the same
	    symptom a gated envelope would give, from a different cause. */
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;

private:
	/** Read every change out of data.inputParameterChanges and hand the
	    values to the DSP. */
	void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes);

	/** One normalised value onto the thing that owns it. The single
	    place a parameter id turns into DSP state, so a new parameter has
	    one place to be wired up and one place to be forgotten. */
	void applyParam (Steinberg::Vst::ParamID id, double normalized);

	/** Note on, note off, and everything else ignored. A stub with the
	    event input already unpacked, because an instrument whose events
	    never arrive looks identical to one whose voices are silent. */
	void handleEvent (const Steinberg::Vst::Event& event);

	/** Tell the controller the rate the DSP is really running at. From
	    setActive - the UI thread - and never from process(). */
	void sendSampleRateToController ();

	/** Write the DSP's block out to the host's bus, converting to
	    64-bit if that is what it asked for. */
	void writeOutput (Steinberg::Vst::ProcessData& data, Steinberg::int32 numSamples);

	FilterDrumDsp mDsp;

	double mSampleRate = 44100.0;
	bool   mBypass     = false;

	/** The two release times in SECONDS, mirrored here so
	    getTailSamples can answer without reaching into the DSP's
	    private state.

	    THE PROCESSOR HOLDS NO OTHER PARAMETER VALUES, on purpose: the
	    DSP owns them, and a second copy of a value is a second thing to
	    keep in step. These two are the exception because the tail is a
	    question about the parameters that the DSP is not the right
	    place to answer, and both are needed to take a maximum. */
	double mVcfReleaseSeconds = 0.120;
	double mVcaReleaseSeconds = 0.150;

	/** What the host last sent for each table parameter, normalised.
	    Written by applyParam, read only by getState - see the comment
	    there for why the inverse mappings are not used instead.
	    Initialised from the table in the constructor, so a getState
	    before any parameter has moved writes the defaults rather than
	    zeros. */
	double mNormalized[kNumParams] = {};

	/** THE 32-BIT SCRATCH THE DSP ALWAYS RENDERS INTO.

	    Sized in setupProcessing and never resized anywhere else, so the
	    audio thread never allocates. It exists because a host is
	    entitled to hand over 64-bit buffers (canProcessSampleSize
	    accepts kSample64), and the DSP is float throughout - rather than
	    write the DSP twice, it renders here and writeOutput widens on
	    the way out. */
	std::vector<float> mScratch;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
