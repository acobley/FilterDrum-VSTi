//------------------------------------------------------------------------
// FilterDrum - audio processor implementation
//------------------------------------------------------------------------

#include "FilterDrumProcessor.h"
#include "FilterDrumIDs.h"

#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstdint>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace FilterDrum {

//------------------------------------------------------------------------
/** Bumped whenever the layout of the state stream changes. setState
    reads it first and refuses a stream from the future rather than
    guessing at it. */
static const int32 kStateVersion = 5;
//
// VERSION 2 appended ten parameters to version 1's one; VERSION 3
// appended drum 2's eleven and the crossfader; VERSION 4 appended the
// sixteen step switches, Run and the launch division; VERSION 5 appended
// the eight envelope shapes. The stream carries its
// own COUNT, and setState applies defaults before reading it, so an
// older project loads correctly: its values land on the parameters they
// were written for and everything newer takes its default rather than
// whatever the last project left in it.
//
// A VERSION-4 PROJECT SOUNDS THE SAME, which is not luck. The eight
// shapes default to -100, the Exponential end, and the RC curve family
// was chosen precisely because the old fixed envelope is a member of it:
// at that setting the release is the -60 dB decay it always was, to
// within the 0.001 the old one never got to shed. testShapedEnvelope
// asserts that against a copy of the old one-pole rather than trusting
// it. Had the shapes defaulted to Linear, every project ever saved would
// have opened restyled.
//
// A VERSION-2 PROJECT THEREFORE OPENS AS A ONE-DRUM PATCH, which is the
// right answer - it was one. Drum 2 arrives at its defaults and the mix
// at 50 %, so the sound CHANGES on load: the old kick is now blended
// half-and-half with a snap that was not there before. There is no way
// round that short of defaulting the mix to 100 % drum 1, which would
// hide the second drum from everyone who never opens an old project.
// The trade is recorded here rather than discovered.

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

		// The clock and the sequencer start from nothing, so the first
		// bar line after the plug-in is switched on is a line it has not
		// fired before and an armed pattern launches on it.
		mBarClock.reset ();
		mSequencer.reset ();
		mWasPlaying = false;
		mPublishedPlayhead = -2;

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

	//--------------------------------------------------------------------
	// THE PER-DRUM PARAMETERS GO THROUGH ONE SWITCH, NOT TWO.
	//
	// splitDrumParam turns an id into "which drum" and "which knob", so
	// the thirty per-drum ids are wired by fifteen case labels against a
	// DrumVoice reference. Writing it out twice would be thirty more
	// chances to send drum 2's release to drum 1 - a mistake that
	// compiles, runs, and sounds almost right.
	//
	// THIRTY, NOT TWENTY-TWO, because the four shape controls are a
	// second per-drum block appended past the sequencer with an offset
	// of its own. splitDrumParam knows about both; nothing here does.
	//--------------------------------------------------------------------
	int drum = 0;
	ParamID base = 0;
	if (splitDrumParam (id, drum, base))
	{
		DrumVoice& voice = (drum == 1) ? mDsp.drum1 () : mDsp.drum2 ();

		switch (base)
		{
			case kNoiseLevel:  voice.setNoiseLevel (internal);  break;
			case kCutoff:      voice.setCutoff (internal);      break;
			case kResonance:   voice.setResonance (internal);   break;

			case kVcfAttack:   voice.setVcfAttack (internal);   break;
			case kVcfAmount:   voice.setVcfAmount (internal);   break;
			case kVcfVelocity: voice.setVcfVelocity (internal); break;

			case kVcaAttack:   voice.setVcaAttack (internal);   break;
			case kVcaAmount:   voice.setVcaAmount (internal);   break;
			case kVcaVelocity: voice.setVcaVelocity (internal); break;

			// The four releases also feed getTailSamples, which needs
			// the longest of them.
			case kVcfRelease:
				voice.setVcfRelease (internal);
				mReleaseSeconds[(drum - 1) * 2 + 0] = internal;
				break;

			case kVcaRelease:
				voice.setVcaRelease (internal);
				mReleaseSeconds[(drum - 1) * 2 + 1] = internal;
				break;

			// The four shapes. They reach this switch through the SAME
			// splitDrumParam call as the other eleven even though they
			// live in a separate block four ids apart - that is what
			// splitDrumParam's second range test is for, and it is why
			// this switch did not have to learn about two offsets.
			case kVcfAttackShape:  voice.setVcfAttackShape (internal);  break;
			case kVcfReleaseShape: voice.setVcfReleaseShape (internal); break;
			case kVcaAttackShape:  voice.setVcaAttackShape (internal);  break;
			case kVcaReleaseShape: voice.setVcaReleaseShape (internal); break;

			default:
				// A per-drum parameter in the table that nothing reads.
				// Appending one and forgetting this switch is the quiet
				// failure; tests/DspTests.cpp asserts that every id in
				// the table changes something.
				break;
		}
		return;
	}

	// The sixteen steps, as one contiguous block - see the static_assert
	// on kStep16 - kStep1 in FilterDrumParams.h.
	if (id >= kStep1 && id <= kStep16)
	{
		mSequencer.setStep (static_cast<int> (id - kStep1), normalized >= 0.5);
		return;
	}

	switch (id)
	{
		case kOutputTrim: mDsp.setOutputTrimDb (internal); break;
		case kMix:        mDsp.setMix (internal);          break;

		case kSeqRun:
			// Switching on ARMS; switching off stops at once. The
			// asymmetry lives in StepSequencer::setRunning.
			mSequencer.setRunning (normalized >= 0.5);
			break;

		case kSeqDivision:
			// toInternal() has already rounded the enum to a whole step.
			mSequencer.setDivision (
			    divisionFromIndex (static_cast<int> (internal + 0.5)));
			break;

		default:
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
void FilterDrumProcessor::handleEvent (const Event& event, int32& offsetOut,
                                       bool& triggerOut, double& velocityOut)
{
	triggerOut = false;

	switch (event.type)
	{
		case Event::kNoteOnEvent:
		{
			// MONOPHONIC, TWO DRUMS DEEP: one note strikes both voices.
			// The PITCH IS IGNORED - every key makes the same pair, which
			// is what keeps the Cutoff knobs meaning absolute
			// frequencies.
			//
			// event.noteOn.velocity is ALREADY 0..1: VST3 normalises it,
			// so MIDI 127 arrives as 1.0. A plug-in that divides by 127
			// anyway ends up 127 times too quiet.
			double velocity = static_cast<double> (event.noteOn.velocity);
			if (velocity < 0.0) velocity = 0.0;
			if (velocity > 1.0) velocity = 1.0;

			// THE SAMPLE OFFSET IS CARRIED OUT, not thrown away. It used
			// to be: every note landed at offset 0 and so was quantised
			// to the block size, which is 11 ms at 512 samples and 44.1 k
			// - audible swing. process() now splits the block here.
			offsetOut = event.sampleOffset;
			velocityOut = velocity;
			triggerOut = true;
			break;
		}

		case Event::kNoteOffEvent:
			// DELIBERATELY NOTHING. The envelopes are triggered, not
			// gated: a drum has to sound the same whether the key was
			// tapped or held, and a MIDI drum note is often only a couple
			// of milliseconds long. Releasing here would cut every hit
			// off at its note length and make all four Release knobs
			// appear broken.
			//
			// The case is written out rather than falling into the
			// default, because "we looked at note-off and chose to ignore
			// it" and "we never handled note-off" are different things to
			// read six months from now.
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
	// The longest of the FOUR releases - two drums, two envelopes each -
	// plus a margin for the filters' own ringing, which at high
	// resonance is the longest thing in here.
	//
	// THE RELEASE KNOB IS THE WHOLE ANSWER, and it only became so when
	// the shape controls went in. The old one-pole release approached
	// zero without reaching it and had to run on to -100 dB before it
	// could call itself finished - 5/3 of the knob - so this had to
	// convert, and for a while it did. The shaped envelope is a phase
	// ramp, so it lands on zero at exactly the time the knob says and
	// there is nothing left to convert. testShapedEnvelope asserts that,
	// because if it ever stops being true this line starts truncating
	// decays again and a truncated decay is a click.
	//
	// ROUNDED UP AND GENEROUS ON PURPOSE. Too long costs a host a few
	// blocks of silence it did not need; too short truncates the decay,
	// and a truncated decay is a click. kInfiniteTail would also be
	// correct and would stop a host ever sleeping the plug-in, which is
	// the sort of thing that gets noticed on battery.
	const double longest = *std::max_element (std::begin (mReleaseSeconds),
	                                          std::end (mReleaseSeconds));
	const double seconds = longest + 0.5;
	const double samples = seconds * mSampleRate;

	return static_cast<uint32> (samples + 0.5);
}

//------------------------------------------------------------------------
tresult PLUGIN_API FilterDrumProcessor::process (ProcessData& data)
{
	// PARAMETERS FIRST, ALWAYS - before any early return, or a
	// parameter-only block would be thrown away along with the block.
	applyParameterChanges (data.inputParameterChanges);

	//--------------------------------------------------------------------
	// COLLECT EVERY TRIGGER IN THE BLOCK, then render around them.
	//
	// Two sources - MIDI notes and sequencer steps - and both carry a
	// SAMPLE OFFSET. Firing everything at offset 0 would quantise every
	// hit to the block size: 512 samples is 11 ms at 44.1 k, which is
	// audible swing on a sixteenth and was the behaviour before the
	// sequencer arrived.
	//--------------------------------------------------------------------
	struct Trigger
	{
		int32  offset = 0;
		double velocity = 1.0;
	};

	Trigger triggers[kMaxTriggersPerBlock];
	int triggerCount = 0;

	// ---- MIDI ----------------------------------------------------------
	if (data.inputEvents != nullptr)
	{
		const int32 count = data.inputEvents->getEventCount ();
		for (int32 i = 0; i < count && triggerCount < kMaxTriggersPerBlock; ++i)
		{
			Event event = {};
			if (data.inputEvents->getEvent (i, event) != kResultOk)
				continue;

			int32  offset = 0;
			bool   fires = false;
			double velocity = 1.0;
			handleEvent (event, offset, fires, velocity);

			if (fires)
			{
				const int32 last = (data.numSamples > 0) ? data.numSamples - 1 : 0;
				triggers[triggerCount].offset = std::min (std::max<int32> (0, offset), last);
				triggers[triggerCount].velocity = velocity;
				++triggerCount;
			}
		}
	}

	// ---- the sequencer -------------------------------------------------
	const TransportInfo transport = readTransport (data);

	// A TRANSPORT STOP UN-LAUNCHES IT. Without this, pressing play again
	// resumes mid-pattern instead of launching on the next line - and
	// rewinding to the top of a bar would skip that bar line, because the
	// clock remembers having fired it.
	if (mWasPlaying && !transport.playing)
	{
		mSequencer.reset ();
		mBarClock.reset ();
	}
	mWasPlaying = transport.playing;

	GridLine lines[kMaxGridLinesPerBlock];
	const int lineCount = mBarClock.gridLinesInBlock (transport, data.numSamples,
	                                                  mSampleRate, lines,
	                                                  kMaxGridLinesPerBlock);

	for (int i = 0; i < lineCount && triggerCount < kMaxTriggersPerBlock; ++i)
	{
		// lineFires advances the sequencer whether or not the step is
		// switched on, so it must be called for EVERY line and not only
		// the ones expected to sound - that is what moves the playhead
		// and what launches an armed pattern.
		if (!mSequencer.lineFires (lines[i].step))
			continue;

		// SEQUENCED HITS ARE FULL VELOCITY. There is no per-step level,
		// so the four Velocity sensitivity knobs do nothing for these -
		// they respond to MIDI only. Worth finding here rather than by
		// ear.
		triggers[triggerCount].offset = lines[i].offset;
		triggers[triggerCount].velocity = 1.0;
		++triggerCount;
	}

	publishPlayhead (data);

	// IN OFFSET ORDER. MIDI events arrive sorted and grid lines arrive
	// sorted, but the two lists are interleaved, and a trigger out of
	// order would make the segment loop below render backwards.
	std::stable_sort (triggers, triggers + triggerCount,
	                  [] (const Trigger& a, const Trigger& b) { return a.offset < b.offset; });

	//--------------------------------------------------------------------
	// A PARAMETER-ONLY BLOCK: numSamples == 0, or no output bus at all.
	// Both are legal and both arrive in practice. The triggers still have
	// to happen - a note delivered in one is a note - they just have
	// nowhere to be rendered yet.
	//--------------------------------------------------------------------
	if (data.numSamples <= 0 || data.numOutputs < 1)
	{
		for (int i = 0; i < triggerCount; ++i)
			mDsp.trigger (triggers[i].velocity);
		return kResultOk;
	}

	const int32 numSamples = data.numSamples;

	// NEVER TRUST THE BLOCK SIZE - setupProcessing promised
	// maxSamplesPerBlock, and growing the scratch here would be an
	// allocation on the audio thread.
	if (mScratch.size () < static_cast<size_t> (numSamples) * kChannelCount)
		return kResultFalse;

	float* const left  = mScratch.data ();
	float* const right = mScratch.data () + numSamples;

	if (mBypass)
	{
		// AN INSTRUMENT'S BYPASS IS SILENCE, not a dry path: there is no
		// input to pass through. The sequencer above still ran, so the
		// playhead keeps its place and un-bypassing does not jump.
		std::fill (left,  left  + numSamples, 0.f);
		std::fill (right, right + numSamples, 0.f);
	}
	else
	{
		//----------------------------------------------------------------
		// RENDER IN SEGMENTS, striking the drums between them. Each
		// segment is [position, next trigger), so a trigger takes effect
		// on exactly the sample it asked for.
		//----------------------------------------------------------------
		int32 position = 0;

		for (int i = 0; i < triggerCount; ++i)
		{
			const int32 at = triggers[i].offset;

			if (at > position)
			{
				mDsp.render (left + position, right + position, at - position);
				position = at;
			}

			// Several triggers on the same sample are legal - a MIDI note
			// landing on a step - and each one retriggers. The envelopes
			// continue from where they are rather than restarting, so
			// that is a double hit, not a click.
			mDsp.trigger (triggers[i].velocity);
		}

		if (position < numSamples)
			mDsp.render (left + position, right + position, numSamples - position);
	}

	writeOutput (data, numSamples);

	return kResultOk;
}

//------------------------------------------------------------------------
uint32 PLUGIN_API FilterDrumProcessor::getProcessContextRequirements ()
{
	// WITHOUT THIS, ALL OF IT ARRIVES INVALID. Opt-in since VST3 3.7, and
	// the failure is silent: the tempo reads 120 in every host, the bar
	// lines land nowhere, the sequencer never launches, and the validator
	// says nothing about any of it.
	//
	// Only what is actually read. kNeedTransportState is the play flag,
	// kNeedProjectTimeMusic the position, and the other two are what a
	// bar is made of - a bar is `numerator` notes of 1/denominator, so
	// assuming 4/4 would put every bar line in the wrong place in half
	// the music anyone writes.
	processContextRequirements.needTransportState ();
	processContextRequirements.needProjectTimeMusic ();
	processContextRequirements.needTempo ();
	processContextRequirements.needTimeSignature ();

	return AudioEffect::getProcessContextRequirements ();
}

//------------------------------------------------------------------------
TransportInfo FilterDrumProcessor::readTransport (const ProcessData& data) const
{
	TransportInfo info;

	const ProcessContext* ctx = data.processContext;
	if (ctx == nullptr)
	{
		// No context at all. The bar clock returns no lines, so the
		// sequencer never launches - which is the honest answer: there
		// are no bars to follow. MIDI still plays.
		return info;
	}

	info.hasContext = true;
	info.playing = (ctx->state & ProcessContext::kPlaying) != 0;

	// THE THREE FACTS DEGRADE SEPARATELY, and each has its own flag,
	// because a host that reports tempo and position but not its time
	// signature must not stop the sequencer dead - see TransportInfo.
	info.tempoKnown = (ctx->state & ProcessContext::kTempoValid) != 0;
	if (info.tempoKnown)
		info.tempoBpm = ctx->tempo;

	info.posKnown = (ctx->state & ProcessContext::kProjectTimeMusicValid) != 0;
	if (info.posKnown)
		info.ppq = ctx->projectTimeMusic;

	const bool sigKnown = (ctx->state & ProcessContext::kTimeSigValid) != 0;
	if (sigKnown)
	{
		info.sigNumerator = ctx->timeSigNumerator;
		info.sigDenominator = ctx->timeSigDenominator;
	}

	// Locating a bar needs all three, so `musical` is one flag over the
	// three rather than three the caller has to remember to check.
	info.musical = info.tempoKnown && info.posKnown && sigKnown;

	return info;
}

//------------------------------------------------------------------------
void FilterDrumProcessor::publishPlayhead (ProcessData& data)
{
	const int now = mSequencer.launched () ? mSequencer.playhead () : -1;

	// ONLY WHEN IT CHANGES. Publishing every block would put a point into
	// the host's automation queue for every buffer whether or not the
	// playhead had moved - which at 512 samples is ninety a second doing
	// nothing.
	if (now == mPublishedPlayhead)
		return;

	IParameterChanges* out = data.outputParameterChanges;
	if (out == nullptr)
		return;

	int32 index = 0;
	if (IParamValueQueue* queue = out->addParameterData (kPlayheadOut, index))
	{
		int32 pointIndex = 0;
		// playheadToNormalized is shared with the panel, so the two
		// cannot disagree about the encoding - see FilterDrumParams.h.
		queue->addPoint (0, playheadToNormalized (now), pointIndex);
		mPublishedPlayhead = now;
	}
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
