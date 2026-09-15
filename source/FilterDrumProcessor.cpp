//------------------------------------------------------------------------
// FilterDrum - audio processor implementation
//------------------------------------------------------------------------

#include "FilterDrumProcessor.h"
#include "FilterDrumIDs.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace FilterDrum {

//------------------------------------------------------------------------
/** Bumped whenever the layout of the state stream changes. setState
    reads it first and refuses a stream from the future rather than
    guessing at it. */
static const int32 kStateVersion = 2;
//
// VERSION 2 appended ten parameters to version 1's one. The stream
// carries its own COUNT, and setState applies defaults before reading
// it, so a version-1 project loads correctly: its single value goes to
// kOutputTrim and the ten new parameters take their defaults rather
// than whatever the last project left in them. That is the whole
// payoff of the "append, never insert" rule, and it is why the version
// number did not have to become a migration.

//------------------------------------------------------------------------
FilterDrumProcessor::FilterDrumProcessor ()
{
	setControllerClass (kFilterDrumControllerUID);

	// DEFAULTS THROUGH THE SAME PATH THE HOST USES, rather than trusting
	// the DSP's member initialisers to agree with the table. They did
	// agree when this was written; the point is that they cannot drift,
	// because there is now only one statement of what the default patch
	// is and it is the table.
	for (int i = 0; i < kNumParams; ++i)
		applyParam (kParams[i].id, kParams[i].defaultNormalized ());
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::initialize (FUnknown* context)
{
	const tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// An instrument: notes in, audio out, NO AUDIO INPUT. The empty
	// input side is what makes this an aumu rather than an aufx, and it
	// is why resource/au-info.plist says 0 in / 2 out.
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

	// 16 channels, the MIDI convention. A host maps its MIDI port onto
	// this; without it the plug-in is an instrument nobody can play, and
	// the symptom is silence rather than an error.
	addEventInput (STR16 ("Event In"), 16);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::terminate ()
{
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                            SpeakerArrangement* outputs, int32 numOuts)
{
	// Stereo out, nothing in, and nothing else accepted. This must agree
	// with the AudioComponents entry in resource/au-info.plist, which
	// lists exactly one layout - 0 in / 2 out - or auval rejects the AU.
	//
	// Refusing everything else is the point: a layout accepted here and
	// absent from the plist, or the other way round, is the mismatch
	// auval finds and nothing else does.
	if (numIns != 0 || numOuts != 1)
		return kResultFalse;

	if (outputs[0] != SpeakerArr::kStereo)
		return kResultFalse;

	return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	// BOTH, because hosts offer 64-bit buffers and one that is refused
	// silently gets a plug-in the host will not load. The DSP is float
	// throughout; writeOutput widens into the host's buffer.
	if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::setupProcessing (ProcessSetup& setup)
{
	mSampleRate = setup.sampleRate;

	// ALLOCATION BELONGS HERE, not in process(). Both the SpaceDub and
	// the ForTran DXis reallocated from inside their processing loops,
	// and this is the only place the host says how big a block to
	// expect.
	mDsp.setSampleRate (mSampleRate);
	mDsp.setMaxBlockSize (setup.maxSamplesPerBlock);

	mScratch.assign (static_cast<size_t> (setup.maxSamplesPerBlock) * kChannelCount, 0.f);

	return AudioEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::setActive (TBool state)
{
	if (state)
	{
		mDsp.reset ();

		// THE UI THREAD, which is the only thread a message may be sent
		// from. Also the only moment that makes a panel opened LATER
		// show the truth rather than its initial guess.
		sendSampleRateToController ();
	}

	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
void FilterDrumProcessor::sendSampleRateToController ()
{
	if (auto* message = allocateMessage ())
	{
		FReleaser releaser (message);
		message->setMessageID (kFilterDrumSampleRateMessage);
		message->getAttributes ()->setFloat (kFilterDrumSampleRateAttribute, mSampleRate);
		sendMessage (message);
	}
}

//------------------------------------------------------------------------
void FilterDrumProcessor::applyParam (ParamID id, double normalized)
{
	// kBypass FIRST, because it is 1000 and is not in the table. Every
	// id is checked before anything indexes kParams - see the comment on
	// kBypass in FilterDrumParams.h.
	if (id == kBypass)
	{
		mBypass = (normalized >= 0.5);
		return;
	}

	if (!isTableParam (id))
		return;

	// THE PROCESSOR'S COPY OF THE HOST'S VIEW, and the only reason it
	// exists is getState: the DSP stores internal units, and turning
	// seconds back into a normalised position would mean inverting
	// every mapping in the table - an inverse that has to be kept in
	// step with the forward one, which is a bug waiting to happen.
	// Storing what the host sent is exact and costs eleven doubles.
	mNormalized[id] = normalized;

	const ParamDef& def = paramDef (id);

	// toInternal() EVERY TIME, never toPlain(). The DSP takes seconds
	// where the panel says milliseconds, octaves where it says per
	// cent, and the MS-20 feedback gain K where it says resonance. Every
	// one of those conversions is in the table, so a unit can only be
	// got wrong in one place.
	const double internal = def.toInternal (normalized);

	switch (id)
	{
		case kOutputTrim:  mDsp.setOutputTrimDb (internal); break;

		case kCutoff:      mDsp.setCutoff (internal);       break;
		case kResonance:   mDsp.setResonance (internal);    break;

		case kVcfAttack:   mDsp.setVcfAttack (internal);    break;
		case kVcfAmount:   mDsp.setVcfAmount (internal);    break;
		case kVcfVelocity: mDsp.setVcfVelocity (internal);  break;

		case kVcaAttack:   mDsp.setVcaAttack (internal);    break;
		case kVcaAmount:   mDsp.setVcaAmount (internal);    break;
		case kVcaVelocity: mDsp.setVcaVelocity (internal);  break;

		// The two releases also feed getTailSamples, so they are the
		// only ones that do anything beyond handing the value over.
		case kVcfRelease:
			mDsp.setVcfRelease (internal);
			mVcfReleaseSeconds = internal;
			break;

		case kVcaRelease:
			mDsp.setVcaRelease (internal);
			mVcaReleaseSeconds = internal;
			break;

		default:
			// A parameter in the table that nothing reads. Appending one
			// and forgetting this switch is the quiet failure; there is
			// no way to catch it at compile time with a table, so it is
			// named here instead - and tests/DspTests.cpp asserts that
			// every id in the table changes something.
			break;
	}
}

//------------------------------------------------------------------------
void FilterDrumProcessor::applyParameterChanges (IParameterChanges* changes)
{
	if (changes == nullptr)
		return;

	const int32 count = changes->getParameterCount ();
	for (int32 i = 0; i < count; ++i)
	{
		IParamValueQueue* queue = changes->getParameterData (i);
		if (queue == nullptr)
			continue;

		const int32 points = queue->getPointCount ();
		if (points <= 0)
			continue;

		// THE LAST POINT IN THE BLOCK, not the first. Sample-accurate
		// automation within a block is not read here; the DSP's own
		// smoother is what stops that being audible, which is why the
		// smoothed column in the parameter table matters.
		int32      offset = 0;
		ParamValue value  = 0.0;
		if (queue->getPoint (points - 1, offset, value) == kResultTrue)
			applyParam (queue->getParameterId (), value);
	}
}

//------------------------------------------------------------------------
void FilterDrumProcessor::handleEvent (const Event& event)
{
	switch (event.type)
	{
		case Event::kNoteOnEvent:
		{
			// MONOPHONIC: one voice, so this retriggers it rather than
			// allocating anything. The PITCH IS IGNORED - every key
			// makes the same drum, which is what keeps the Cutoff knob
			// meaning one absolute frequency.
			//
			// event.noteOn.velocity is ALREADY 0..1: VST3 normalises
			// it, so MIDI 127 arrives as 1.0 and MIDI 0 as 0.0. There
			// is no division by 127 to get wrong here, and a plug-in
			// that does one anyway ends up 127 times too quiet.
			double velocity = static_cast<double> (event.noteOn.velocity);
			if (velocity < 0.0) velocity = 0.0;
			if (velocity > 1.0) velocity = 1.0;

			mDsp.trigger (velocity);
			break;
		}

		case Event::kNoteOffEvent:
			// DELIBERATELY NOTHING. The envelopes are triggered, not
			// gated: a drum has to sound the same whether the key was
			// tapped or held, and a MIDI drum note is often only a
			// couple of milliseconds long. Releasing here would cut
			// every hit off at its note length and make both Release
			// knobs appear broken.
			//
			// The case is still written out rather than falling into
			// the default, because "we looked at note-off and chose to
			// ignore it" and "we never handled note-off" are different
			// things to read six months from now. AREnvelope::release
			// is the hook if a gated AR is ever wanted.
			break;

		default:
			break;
	}
}


//------------------------------------------------------------------------
void FilterDrumProcessor::writeOutput (ProcessData& data, int32 numSamples)
{
	if (data.numOutputs < 1 || data.outputs[0].numChannels < kChannelCount)
		return;

	const float* const left  = mScratch.data ();
	const float* const right = mScratch.data () + numSamples;

	if (data.symbolicSampleSize == kSample64)
	{
		double** out = data.outputs[0].channelBuffers64;
		for (int32 i = 0; i < numSamples; ++i)
		{
			out[0][i] = static_cast<double> (left[i]);
			out[1][i] = static_cast<double> (right[i]);
		}
	}
	else
	{
		float** out = data.outputs[0].channelBuffers32;
		std::copy (left,  left  + numSamples, out[0]);
		std::copy (right, right + numSamples, out[1]);
	}

	// SILENCE FLAGS. A host is entitled to skip a bus we declare
	// silent, and an instrument that renders silence and does not say
	// so keeps every downstream plug-in awake for the life of the
	// session.
	//
	// A BUS FLAGGED SILENT THAT IS NOT is far worse than one that is
	// not flagged - the host may skip it and the hit simply never
	// arrives - so this asks the DSP rather than guessing. mDsp.active()
	// is false only once the VCA envelope has reached exactly zero and
	// gone idle, which is why AREnvelope::next snaps to zero instead of
	// decaying into denormals forever.
	data.outputs[0].silenceFlags = 0;

	if (!mDsp.active ())
		for (int32 c = 0; c < data.outputs[0].numChannels; ++c)
			data.outputs[0].silenceFlags |= static_cast<uint64> (1) << c;
}

//------------------------------------------------------------------------
uint32 PLUGIN_API FilterDrumProcessor::getTailSamples ()
{
	// The longer of the two releases, plus a margin for the filter's own
	// ringing - which at high resonance is the longest thing in here.
	//
	// ROUNDED UP AND GENEROUS ON PURPOSE. Too long costs a host a few
	// blocks of silence it did not need; too short truncates the decay,
	// and a truncated decay is a click. kInfiniteTail would also be
	// correct and would stop a host ever sleeping the plug-in, which is
	// the sort of thing that gets noticed on battery.
	const double seconds = std::max (mVcfReleaseSeconds, mVcaReleaseSeconds) + 0.5;
	const double samples = seconds * mSampleRate;

	return static_cast<uint32> (samples + 0.5);
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::process (ProcessData& data)
{
	// PARAMETERS FIRST, ALWAYS - before the early return below, or a
	// parameter-only block would be thrown away along with the block.
	applyParameterChanges (data.inputParameterChanges);

	if (data.inputEvents != nullptr)
	{
		const int32 count = data.inputEvents->getEventCount ();
		for (int32 i = 0; i < count; ++i)
		{
			Event event = {};
			if (data.inputEvents->getEvent (i, event) == kResultOk)
				handleEvent (event);
		}
	}

	// A PARAMETER-ONLY BLOCK: numSamples == 0, or no output bus at all.
	// Both are legal and both arrive in practice - hosts send them to
	// deliver automation between audible blocks. Returning kResultOk is
	// the correct answer; the work above has already been done.
	if (data.numSamples <= 0 || data.numOutputs < 1)
		return kResultOk;

	const int32 numSamples = data.numSamples;

	// NEVER TRUST THE BLOCK SIZE. setupProcessing promised
	// maxSamplesPerBlock, but a host that hands over more would walk off
	// the end of a scratch sized on that promise, so the scratch is
	// checked rather than assumed. Growing it here WOULD BE AN
	// ALLOCATION ON THE AUDIO THREAD, so the block is refused instead -
	// loudly silent beats intermittently crashing.
	if (mScratch.size () < static_cast<size_t> (numSamples) * kChannelCount)
		return kResultFalse;

	float* const left  = mScratch.data ();
	float* const right = mScratch.data () + numSamples;

	if (mBypass)
	{
		// AN INSTRUMENT'S BYPASS IS SILENCE, not a dry path: there is no
		// input to pass through. The host expects a bypassed plug-in to
		// be inaudible, and that is what this is.
		std::fill (left,  left  + numSamples, 0.f);
		std::fill (right, right + numSamples, 0.f);
	}
	else
	{
		mDsp.render (left, right, numSamples);
	}

	writeOutput (data, numSamples);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::getState (IBStream* state)
{
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	streamer.writeInt32 (kStateVersion);

	// THE COUNT IS WRITTEN, so a stream from an older build with fewer
	// parameters can be read by a newer one. This is the other half of
	// "append, never insert".
	streamer.writeInt32 (kNumParams);

	for (int i = 0; i < kNumParams; ++i)
	{
		// NORMALISED, not plain. The plain range is allowed to change
		// between versions - a wider trim, a longer maximum release -
		// and a state stream full of plain values would silently
		// rescale when it did. Normalised values survive a range
		// change; they just mean a slightly different number
		// afterwards, which is the lesser of the two evils and the one
		// VST3 chose.
		streamer.writeDouble (mNormalized[i]);
	}

	streamer.writeInt32 (mBypass ? 1 : 0);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::setState (IBStream* state)
{
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return kResultFalse;
	if (version > kStateVersion)
		return kResultFalse;   // written by a newer build - do not guess

	int32 count = 0;
	if (!streamer.readInt32 (count))
		return kResultFalse;

	//--------------------------------------------------------------------
	// EVERYTHING THE STREAM DOES NOT MENTION GOES BACK TO ITS DEFAULT,
	// and this is the trap that reset is here to close.
	//
	// A host reuses one plug-in instance as the user loads project after
	// project. If setState only applied what the stream contained, a
	// short stream - written by an older build, or truncated - would
	// leave every unmentioned parameter holding the PREVIOUS project's
	// value. The user opens an old song and hears the settings from the
	// new one, with nothing anywhere saying why.
	//
	// So: defaults first, stream second. The controller's
	// setComponentState does exactly the same thing in the same order,
	// reading the identical layout - two halves that disagree about the
	// stream is the same bug wearing a different hat.
	//--------------------------------------------------------------------
	for (int i = 0; i < kNumParams; ++i)
		applyParam (kParams[i].id, kParams[i].defaultNormalized ());
	mBypass = false;

	for (int32 i = 0; i < count; ++i)
	{
		double v = 0.0;
		if (!streamer.readDouble (v))
			return kResultFalse;
		if (i < kNumParams)
			applyParam (static_cast<ParamID> (i), v);
	}

	// OPTIONAL BY DESIGN: a version-1 stream always has it, but reading
	// it conditionally is what lets a later version append fields
	// without every older reader failing.
	int32 bypass = 0;
	if (streamer.readInt32 (bypass))
		mBypass = (bypass != 0);

	// The trim must not GLIDE to a loaded project's value from the last
	// one's - that is an audible swoop on every project load.
	mDsp.reset ();

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace FilterDrum
