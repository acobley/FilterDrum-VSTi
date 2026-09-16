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
// MONOPHONIC, AND NOW TWO DRUMS DEEP. One note strikes both voices at
// once - there is still no voice allocation here at all, because a
// layer is not polyphony. See the banner on FilterDrumDsp.h for the
// signal path and on AREnvelope for why note-off is deliberately
// ignored.
//
// THE PROCESS CONTEXT IS ASKED FOR, at last, and this is the note the
// scaffold left about why it has to be.
//
// Since VST3 3.7 the ProcessContext is OPT-IN and the default is NO
// FLAGS. Without the getProcessContextRequirements override below,
// data.processContext arrives with nothing valid in it: the tempo reads
// 120 in every host, the bar lines land nowhere, and the validator
// prints "- None" rather than complaining. The sequencer would simply
// never launch and nothing would say why.
//
// It was absent from the scaffold until now because nothing read the
// tempo. The sequencer is the thing that does.
//
// THE BLOCK IS RENDERED IN SEGMENTS, which is the other thing this file
// gained with the sequencer. A trigger that always landed at offset 0
// would quantise every hit to the block size - 512 samples is 11 ms at
// 44.1 k, which is audible swing on a sixteenth - so process() splits
// the block at every trigger offset and renders the pieces. MIDI notes
// go through the same path and get the same accuracy, which they did not
// have before.

#pragma once

#include "FilterDrumDsp.h"
#include "FilterDrumParams.h"
#include "FilterDrumSequencer.h"
#include "FilterDrumTransport.h"

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

	/** Which fields of the ProcessContext this plug-in reads. Without
	    this they all arrive invalid - see the banner. */
	Steinberg::uint32 PLUGIN_API getProcessContextRequirements () SMTG_OVERRIDE;

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
	void handleEvent (const Steinberg::Vst::Event& event, Steinberg::int32& offsetOut,
	                  bool& triggerOut, double& velocityOut);

	/** Copy the host's ProcessContext into the SDK-free TransportInfo the
	    bar clock understands. The clock knows nothing about VST3; this is
	    the only place the two meet. */
	TransportInfo readTransport (const Steinberg::Vst::ProcessData& data) const;

	/** Publish the playhead to the panel through
	    data.outputParameterChanges - never a message, see kPlayheadOut. */
	void publishPlayhead (Steinberg::Vst::ProcessData& data);

	/** Tell the controller the rate the DSP is really running at. From
	    setActive - the UI thread - and never from process(). */
	void sendSampleRateToController ();

	/** Write the DSP's block out to the host's bus, converting to
	    64-bit if that is what it asked for. */
	void writeOutput (Steinberg::Vst::ProcessData& data, Steinberg::int32 numSamples);

	FilterDrumDsp mDsp;

	/** How many triggers one block can hold: every grid line plus room
	    for the MIDI notes alongside them. Overflowing it drops the
	    surplus rather than growing on the audio thread, and a block with
	    more than sixty-four hits in it is a host doing something no
	    drummer asked for. */
	static constexpr int kMaxTriggersPerBlock = 64;

	StepSequencer mSequencer;
	BarClock      mBarClock;

	/** Whether the transport was rolling on the previous block, so a
	    STOP can be noticed and the sequencer un-launched. Without it, a
	    stop and a restart would resume mid-pattern instead of launching
	    on the next line. */
	bool mWasPlaying = false;

	/** The last playhead published, so the output parameter is only
	    written when it CHANGES. Publishing it every block would put a
	    point into the host's queue for every buffer whether or not
	    anything moved. */
	int mPublishedPlayhead = -2;

	double mSampleRate = 44100.0;
	bool   mBypass     = false;

	/** The four release times in SECONDS - drum 1 VCF, drum 1 VCA,
	    drum 2 VCF, drum 2 VCA - mirrored here so getTailSamples can
	    answer without reaching into the DSP's private state.

	    THE PROCESSOR HOLDS NO OTHER PARAMETER VALUES, on purpose: the
	    DSP owns them, and a second copy of a value is a second thing to
	    keep in step. These four are the exception because the tail is a
	    question about the parameters that the DSP is not the right place
	    to answer, and all four are needed to take a maximum. */
	double mReleaseSeconds[4] = { 0.120, 0.150, 0.045, 0.060 };

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
