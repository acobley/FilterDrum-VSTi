//------------------------------------------------------------------------
// FilterDrum - the audio line
//
// A MONOPHONIC ANALOGUE DRUM VOICE built around a model of the Korg
// MS-20's lowpass filter:
//
// TWO DRUM VOICES, struck together by one note and blended by a
// crossfader:
//
//   DRUM 1  noise x level -> [ MS-20 LP ] -> [ VCA ] --+
//                                 ^             ^       |
//                             AR envelope   AR envelope +--> mix -> trim
//                           (cutoff, bipolar)  (level)  |
//   DRUM 2  noise x level -> [ MS-20 LP ] -> [ VCA ] --+
//
// ONE IMPLEMENTATION, TWO INSTANCES. DrumVoice below is the whole voice;
// FilterDrumDsp owns two of them and does nothing but trigger both, mix
// their outputs and apply the trim. "The second drum is exactly the same
// as the first" is therefore a fact about the code rather than a promise
// about it - there is no second copy to drift.
//
// Both envelopes in each voice are scaled by note velocity through their
// own sensitivity control. Everything a drum sound is here: the noise
// makes hats, snares and claps, and at high resonance the filter
// self-oscillates, which is what makes kicks and toms. Two of them
// layered is how you get a kick with a snap on top.
//
// THE NOISE IS THE ONLY EXCITATION, and the consequence has to be
// stated rather than discovered.
//
// A linear filter fed exact zero from a zero state outputs exact zero
// forever, however far past its self-oscillation threshold it is set -
// zero times any amount of resonance is still zero. So NOISE LEVEL AT 0
// IS SILENCE from a cold start, at every resonance setting. It is not a
// pure-tone setting; it is off.
//
// There is one wrinkle worth knowing, because it makes the silence
// intermittent rather than honest. renderVoices does not advance the
// filter while the voice is idle, so its state FREEZES between hits
// rather than decaying. Once an oscillation has been started by noise,
// turning the knob to 0 leaves it running: at K = 2.4 the hits keep
// coming at about -9 dBFS for the rest of the session, and the voice
// only falls silent when the project is reloaded with the knob already
// down. Measured, in that order: -3 dB, -9, -9, -9 with the knob moved
// mid-session; silence from a cold start.
//
// A per-note trigger ping used to close that gap - it was removed
// deliberately, and `git log` has it. What the measurements said before
// it went: at 100 % noise it was worth 0.00 dB at K=0.96 and K=1.90 and
// 0.17 dB at K=2.4, and 0.22 dB on the first 5 ms of the attack. It
// bought the 0 % setting and nothing else. If the silence at 0 turns
// out to matter, the cheaper fix is a floor on the Noise Level knob -
// 0.5 % is enough to start the oscillation at full resonance - which is
// also what a real circuit's thermal noise does.
//
// NO SDK HEADER MAY ENTER THIS FILE OR ITS .cpp. Two reasons, and the
// first is the practical one:
//
//   1. It compiles and runs standalone with plain `c++ -std=c++17`, so
//      its numbers can be tested before anything is built - which
//      matters here, because the session that writes the code reaches
//      the Mac through a Linux VM and cannot run cmake, Xcode, the SDK
//      validator or auval at all. tests/DspTests.cpp is the only thing
//      in this project that actually executes during development.
//
//   2. VST3 splits the processor from the controller, and they do not
//      share memory. Anything the EDITOR displays that the DSP COMPUTES
//      must come from ONE SHARED FUNCTION both call, or the two copies
//      of the arithmetic drift apart and the panel starts lying about
//      what you are hearing. This header is where those live:
//      dbToGain(), velocityScaled(), cutoffWithEnv().
//------------------------------------------------------------------------

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FilterDrum {

//------------------------------------------------------------------------
/** Stereo, and only stereo. Agrees with setBusArrangements in the
    processor and with the single 0-in / 2-out entry in
    resource/au-info.plist; all three must say the same thing or auval
    rejects the Audio Unit.

    The voice itself is MONO - one drum, one signal path - and is copied
    to both channels. That is deliberate rather than lazy: a stereo
    drum voice with two noise generators decorrelates the hit and makes
    it sound wide and weak, which is the opposite of what a kick wants. */
constexpr int kChannelCount = 2;

constexpr double kTrimSmoothingSeconds = 0.020;
constexpr float  kDenormalFloor = 1e-25f;

/** The ceiling on every recursive state in this file.

    PORTING-GUIDE.md section 5 calls for one, "well above any musical
    level", and 16.0 is about +24 dBFS. It is not a limiter and is never
    reached in normal use - the tests assert that a default-settings
    render is bit-identical with the clamp in place and with it removed.
    It is there so that a resonance setting, a sample rate and a
    denormal cannot combine into an inf that then poisons every
    subsequent sample forever. */
constexpr double kStateCeiling = 16.0;

/** How far the VCF envelope can move the cutoff at full amount, in
    octaves either way. Six is enough to take 100 Hz to 6.4 kHz, which
    covers every down-sweep a kick needs. */
constexpr double kMaxEnvOctaves = 6.0;

/** The resonance knob's top, in units of the MS-20 feedback gain K.

    K = 2 IS WHERE IT STARTS TO SELF-OSCILLATE, which Stinchcombe's
    analysis of the OTA revision gives as k1*k2 >= 2 (the denominator's
    damping term is 2 - k1*k2). So 2.4 puts the onset at 83 % of knob
    travel, leaving useful room past it for the ping to get louder.

    It is also the number that keeps the Newton solve unconditionally
    convergent: the derivative is (1+g)^2 - K*g*sat', which for
    K = 2.4 and sat' <= 1 is at worst 1 - 0.4g + g^2, and that has no
    real roots. Raise this past 4 and there are cutoffs where it does.
    See the comment on Ms20Filter::process. */
constexpr double kMaxResonanceK = 2.4;

/** Cutoff limits. The low end is the knob's; the high end is a fraction
    of the sample rate, because tan() at Nyquist is infinite. */
constexpr double kMinCutoffHz = 20.0;
constexpr double kMaxCutoffFraction = 0.45;

/** The voice's fixed output gain - its headroom, and A MEASURED NUMBER
    RATHER THAN A CHOSEN ONE.

    It sits AFTER the filter, which is the only place it can do the job.
    Attenuating the noise going IN would not touch the loudest thing the
    plug-in does: a self-oscillating filter's amplitude is set by where
    the diodes limit, not by how hard it is driven, so a quieter input
    gives the same self-oscillation.

    Without it the default patch measured +0.09 dBFS - clipping before
    the user has touched anything, which masks every other fault in the
    signal path. The unattenuated figures were:

        default patch, velocity 127         +0.09 dBFS
        full resonance, self-oscillating    +4.87 dBFS

    0.4 puts those at about -8 and -3 dBFS, which leaves the Output Trim
    somewhere useful to go in both directions. measureDefaultLevel() in
    tests/DspTests.cpp asserts the resulting range, so this constant
    cannot be changed without the suite saying what it did. */
constexpr double kVoiceGain = 0.4;

/** How long the crossfader takes to reach a new position, in seconds.

    IT NEEDS SMOOTHING WHERE THE CUTOFF DID NOT. A stepped cutoff turned
    out to be inaudible - a TPT filter changes coefficients without a
    discontinuity in its state - but a stepped GAIN is a step in the
    waveform itself, which is a click. The output trim has a smoother for
    exactly this reason and the crossfader needs one too. */
constexpr double kMixSmoothingSeconds = 0.020;



//------------------------------------------------------------------------
// The shared functions
//
// Called by the DSP, by the parameter table's text formatting, and by
// the editor's readouts. One copy of the arithmetic, so the number on
// screen and the number in the audio path cannot disagree.
//------------------------------------------------------------------------

/** Decibels to a linear gain. Exactly 1.0 at 0 dB, which is what makes
    the default patch bit-identical rather than nearly so. */
inline double dbToGain (double db)
{
	return (db == 0.0) ? 1.0 : std::pow (10.0, db / 20.0);
}

inline double gainToDb (double gain)
{
	return (gain <= 1e-9) ? -180.0 : 20.0 * std::log10 (gain);
}

//------------------------------------------------------------------------
/** THE VELOCITY LAW, and the only place it is written down.
 
    `amount` is what the Amount knob says. `sensitivity` is 0..1 from
    that envelope's own Velocity knob. `velocity` is 0..1 as VST3
    delivers it, which is MIDI velocity / 127.
 
      sensitivity = 1  ->  amount * velocity.  Velocity 127 gives the
                           full amount; velocity 0 gives nothing.
      sensitivity = 0  ->  amount, whatever the velocity.
      in between       ->  linear blend of those two.
 
    Written as amount * (1 - s + s*v) rather than as a lerp between
    two products, because that form makes the two end cases obvious and
    is one multiply-add.
 
    THE PANEL CALLS THIS TOO, so the readout under the Amount knob shows
    what the last hit actually used rather than what the knob says. */
inline double velocityScaled (double amount, double sensitivity, double velocity)
{
	if (sensitivity < 0.0) sensitivity = 0.0;
	if (sensitivity > 1.0) sensitivity = 1.0;
	if (velocity < 0.0) velocity = 0.0;
	if (velocity > 1.0) velocity = 1.0;

	return amount * (1.0 - sensitivity + sensitivity * velocity);
}

/** Where the cutoff actually sits, given the knob in Hz, the envelope
    amount in OCTAVES (already velocity-scaled, and signed) and the
    envelope's current 0..1 level.

    Exponential in the envelope, because pitch and cutoff are
    logarithmic quantities - a linear sweep in Hz sounds like it slows
    down as it falls, which is not what an analogue filter does.

    Clamped here rather than by the caller, so the panel's readout and
    the filter cannot disagree about what a +6-octave sweep off the top
    of the range does. */
inline double cutoffWithEnv (double cutoffHz, double octaves, double envLevel,
                             double sampleRate)
{
	double hz = cutoffHz * std::pow (2.0, octaves * envLevel);

	const double maxHz = sampleRate * kMaxCutoffFraction;
	if (hz < kMinCutoffHz) hz = kMinCutoffHz;
	if (hz > maxHz)        hz = maxHz;
	return hz;
}

//------------------------------------------------------------------------
/** THE CROSSFADE LAW, and the only place it is written down.

    `mix` is 0..1 with 1 meaning ALL DRUM 1 - which is the top of the
    fader, and drum 1 is the top block on the panel, so the control reads
    the way it is laid out.

    CONSTANT POWER, not constant amplitude. The two drums are different
    sounds from independently-seeded noise, so they are uncorrelated and
    their POWERS add rather than their amplitudes. A linear crossfade
    between two uncorrelated sources dips about 3 dB in the middle - the
    hole everyone has heard on a cheap DJ mixer. sin/cos keeps
    gainA^2 + gainB^2 = 1 at every position, so the centre holds.

    The panel's readout calls this too, so the numbers under the fader
    cannot disagree with the gains in the audio path. */
inline double crossfadeGainDrum1 (double mix)
{
	if (mix < 0.0) mix = 0.0;
	if (mix > 1.0) mix = 1.0;
	return std::sin (mix * 1.57079632679489661923);   // pi/2
}

inline double crossfadeGainDrum2 (double mix)
{
	if (mix < 0.0) mix = 0.0;
	if (mix > 1.0) mix = 1.0;
	return std::cos (mix * 1.57079632679489661923);
}

/** Is this resonance setting past the self-oscillation threshold? The
    panel lights its indicator off this, so the lamp and the audio
    cannot disagree about where the ping starts. */
inline bool selfOscillating (double resonanceK)
{
	return resonanceK >= 2.0;
}

//------------------------------------------------------------------------
// THE ENVELOPE SHAPE CONTROL
//
// One knob per stage - four per drum - sweeping
//
//     Exponential  ->  Linear  ->  Logarithmic
//
// through ONE family of curves, the charge and discharge of a capacitor
// through a resistor:
//
//     rise (x, b) = (1 - e^-bx) / (1 - e^-b)        x = 0..1
//     fall (x, b) = 1 - rise (x, b)
//
// WHY THIS FAMILY AND NOT x^k. It contains the curves this plug-in
// already had, exactly. The old release was a true exponential decay
// calibrated to -60 dB, which is fall(x, ln 1000) up to an offset of
// 0.001; the old attack - a one-pole aimed at 1.2 and stopped at 1.0 -
// is rise(x, ln 6) with nothing left over. A power law x^k gets within
// a couple of percent on the attack and is wrong by a factor of five in
// the release tail, so adopting it would have quietly restyled every
// existing patch. This way the Exponential end IS the old behaviour and
// the knob is a departure from a known point.
//
// b > 0 is fast-then-slow, the analogue RC shape, at BOTH ends of the
// envelope: a quick rise easing into the peak, and a quick drop with a
// long tail. That is the punchy shape a drum wants, and it is what
// "Exponential" means on all four knobs. b < 0 is the mirror image and
// is what "Logarithmic" means. b = 0 is a straight line.
//
// EVERY SHAPE ARRIVES EXACTLY. rise(1,b) = 1 and fall(1,b) = 0 for
// every b, by construction - the normalisation is what does it. The old
// envelope had to aim at 1.2 to make the attack arrive at all and had
// to run on to -100 dB before it could call the release finished; both
// of those artefacts are gone, which is why getTailSamples no longer
// needs releaseTailSeconds.
//------------------------------------------------------------------------

/** The curvature at each end of a shape knob. ln 1000, so the
    Exponential end of a RELEASE is exactly the -60 dB decay this
    plug-in shipped with.

    The attack's old curvature was the gentler ln 6, so an attack at the
    Exponential end is now more curved than it used to be. That is
    audible only on a long attack, and both drums default to 1 ms and
    0.5 ms; the alternative was a second constant and a default sitting
    at an unexplainable 37 %. testShapedEnvelope asserts the RELEASE is
    unchanged, which is the half anybody can hear. */
constexpr double kMaxCurve = 6.90775527898213705205;   // ln 1000

/** Below this the curve is drawn as a straight line.

    NOT A TASTE DECISION - it is a division. The normalising denominator
    is 1 - e^-b, which goes to zero with b, so the formula is 0/0 at
    exactly linear. At this threshold the curve departs from a straight
    line by at most b/8 = 1.25e-5, which is 98 dB down. */
constexpr double kLinearCurve = 1e-4;

/** A shape control, -1 .. +1, to its curvature.

    -1 is Exponential, 0 Linear, +1 Logarithmic - so the NEGATIVE end of
    the control is the POSITIVE end of b. The panel reads 0..100 % with
    0 % at Exponential; the table does that conversion, and this
    function is the only place the sign flip lives. */
inline double shapeToCurve (double shape)
{
	if (shape < -1.0) shape = -1.0;
	if (shape >  1.0) shape =  1.0;
	return -shape * kMaxCurve;
}

/** The rising curve, 0 at x = 0 and exactly 1 at x = 1. */
inline double shapedRise (double x, double curve)
{
	if (x <= 0.0) return 0.0;
	if (x >= 1.0) return 1.0;

	if (curve > -kLinearCurve && curve < kLinearCurve)
		return x;

	return (1.0 - std::exp (-curve * x)) / (1.0 - std::exp (-curve));
}

/** The falling curve, exactly 1 at x = 0 and exactly 0 at x = 1. */
inline double shapedFall (double x, double curve)
{
	return 1.0 - shapedRise (x, curve);
}

/** Where on a rising curve a given level sits - the inverse of
    shapedRise.

    THE RETRIGGER NEEDS THIS. A hit that lands part way through a
    decaying one continues from the level it is at rather than jumping
    to zero, which is what stops a fast roll clicking on every note. The
    old envelope got that free because it was a recursion on the level
    itself; a phase-driven envelope has to ask "what phase is this level"
    and start there. */
inline double shapedRiseInverse (double level, double curve)
{
	if (level <= 0.0) return 0.0;
	if (level >= 1.0) return 1.0;

	if (curve > -kLinearCurve && curve < kLinearCurve)
		return level;

	// level = (1 - e^-bx) / D  ->  x = -ln (1 - level*D) / b, where
	// D = 1 - e^-b. For b > 0, D is in (0,1) and the log's argument
	// stays above e^-b; for b < 0, D is negative and the argument is
	// above 1. Neither branch can reach zero, so there is no domain
	// guard here beyond the two clamps above.
	const double d = 1.0 - std::exp (-curve);
	return -std::log (1.0 - level * d) / curve;
}


//------------------------------------------------------------------------
/** A one-pole glide towards a target. */
class Smoother
{
public:
	void setSampleRate (double sampleRate, double seconds);
	void snap (float target);
	void setTarget (float target) { mTarget = target; }
	float target () const { return mTarget; }
	float value () const { return mValue; }
	float next ();
	float coefficient () const { return mCoeff; }

private:
	float mValue  = 0.f;
	float mTarget = 0.f;
	float mCoeff  = 1.f;
};

//------------------------------------------------------------------------
/** White noise.
 
    xorshift32, because it is four operations, has a period of 2^32-1,
    and its output is flat enough that the filter cannot tell it from
    anything more expensive. NOT std::rand: that is not reentrant, its
    low bits are notoriously correlated on some libcs, and it would make
    the render depend on global state, which would break the
    bit-identity tests.
 
    Seeded non-zero and never reseeded on note-on: see the note in
    FilterDrumDsp::trigger about why the hits are deliberately not
    identical. */
class Noise
{
public:
	/** -1..+1, uniform. */
	float next ();

	/** THE TWO VOICES MUST NOT SHARE A SEED.

	    Two generators started from the same state produce the identical
	    sequence, so the two drums would be perfectly correlated - and
	    summing two identical signals is not a layer, it is one signal
	    6 dB louder. Every measurement of the pair would look fine and it
	    would sound like one drum.

	    A zero seed is refused rather than accepted: xorshift cannot
	    escape zero, so a zero-seeded generator outputs silence forever. */
	void setSeed (std::uint32_t seed);

private:
	std::uint32_t mState = 0x9E3779B9u;
};

//------------------------------------------------------------------------
/** A one-shot attack-release envelope, exponential in both halves.
 
    TRIGGERED, NOT GATED, and this is the one place this plug-in
    deliberately departs from what "AR" usually means. Note-on starts
    the attack; the release begins the moment the attack completes, and
    NOTE-OFF IS IGNORED.
 
    A drum has to sound the same whether the key was tapped or held, and
    a MIDI drum note is often only a couple of milliseconds long - a
    gated envelope would cut every hit off at the note length and make
    the Release knob do nothing. Both of those are bugs people report as
    "the release is broken".
 
    If a gated AR is wanted instead, note-off calls release() and
    isAttacking() is the state to leave: the machinery is all here, it
    is only the call from the processor's kNoteOffEvent that is missing.
 
    THE TIMES ARE DEFINED, not approximate, because the tests assert
    them at every sample rate:
 
      attack   time to reach EXACTLY 1.0
      release  time to fall from 1.0 to EXACTLY 0
 
    "Exactly" is the word that changed when the shape controls went in.
    This used to be a one-pole recursion on the level, which meant the
    attack had to aim past its target to arrive at all and the release
    could only approach zero - it ran on to -100 dB before calling
    itself finished, which is why getTailSamples needed a conversion
    and no longer does.
 
    IT IS NOW A PHASE RAMP THROUGH A SHAPING FUNCTION. The phase is
    linear and the curve is applied to it, so every shape from
    Exponential through Linear to Logarithmic takes the time its knob
    says and lands on its endpoint on the nose. See the shape banner
    above shapeToCurve().
 
    The shape is per STAGE, not per envelope: the attack and the release
    have their own knobs, because an attack and a release are not the
    same gesture and a drum usually wants a different curve on each. */
class AREnvelope
{
public:
	enum class Stage { Idle, Attack, Release };

	void setSampleRate (double sampleRate);

	/** Times in SECONDS. Recomputed on every change, never per sample. */
	void setAttack (double seconds);
	void setRelease (double seconds);

	/** The two shape controls, -1 .. +1: -1 Exponential, 0 Linear,
	    +1 Logarithmic. See shapeToCurve() above. */
	void setAttackShape (double shape);
	void setReleaseShape (double shape);

	/** Start a hit. Does NOT reset the level to zero: a retrigger part
	    way through a decaying hit continues from where it is, which is
	    what stops a fast roll clicking on every note. It resumes by
	    ASKING WHICH PHASE THAT LEVEL IS - see shapedRiseInverse. */
	void trigger ();

	/** Cut to silence immediately - for a host reset, not for a
	    note-off. */
	void reset ();

	float next ();
	float level () const { return mLevel; }
	Stage stage () const { return mStage; }
	bool idle () const { return mStage == Stage::Idle; }

	/** How far through the current stage, 0..1. The tests use it; so
	    does nothing else. */
	double phase () const { return mPhase; }

	double attackCurve () const { return mAttackCurve; }
	double releaseCurve () const { return mReleaseCurve; }

private:
	void recompute ();

	/** Start a stage: set the phase step, and prime the running
	    exponential from the phase already in mPhase. */
	void beginStage (double seconds, double curve);

	double mSampleRate   = 44100.0;
	double mAttackTime   = 0.001;
	double mReleaseTime  = 0.150;
	double mAttackShape  = -1.0;      // Exponential: the old behaviour
	double mReleaseShape = -1.0;
	double mAttackCurve  = kMaxCurve;
	double mReleaseCurve = kMaxCurve;

	/** THE RUNNING EXPONENTIAL. e = exp(-curve * phase), advanced by a
	    MULTIPLY per sample rather than a call to exp():

	        e(phase + step) = e(phase) * exp(-curve * step)

	    so the per-sample cost is the same one multiply the old one-pole
	    recursion cost, and exp() is called only when a stage starts.
	    testShapedEnvelope asserts the recursion against the closed form
	    over a four-second release, because a running product is exactly
	    the kind of thing that drifts quietly. */
	double mExpTerm = 1.0;
	double mExpStep = 1.0;

	/** The normalising denominator 1 - e^-curve, and whether this stage
	    is near enough to linear to skip the arithmetic entirely. */
	double mDenominator = 1.0;
	bool   mLinear      = false;

	double mPhase     = 0.0;
	double mPhaseStep = 1.0;

	float mLevel = 0.f;
	Stage mStage = Stage::Idle;
};

//------------------------------------------------------------------------
/** One envelope's settings, as a panel display needs them.

    The editor cannot see the DrumVoice - it is in the processor, in
    another object and possibly another process - so the display is
    built from parameter values. This is the shape of those values. */
struct ArSpec
{
	double attack  = 0.001;   // seconds
	double release = 0.150;   // seconds

	/** The two shape controls, -1 Exponential .. +1 Logarithmic. The
	    display draws the SHAPED curve, so a shape knob moves the picture
	    as well as the sound - which is most of the point of having a
	    picture. */
	double attackShape  = -1.0;
	double releaseShape = -1.0;

	// NO HEIGHT. The curves are the SHAPE and nothing else - both are
	// drawn full height, so the two can be compared.
	//
	// They used to be scaled by their Amount controls, on the argument
	// that the height was meaningful. It is, but it is the wrong thing
	// to spend the axis on: the VCF Amount is kept low in normal use,
	// because a large one is a siren sweep rather than a drum, so the
	// filter envelope was drawn as a flat smear along the bottom of the
	// panel exactly when it most needed looking at. The amounts are
	// drawn as two short marker lines instead - see
	// SpyEnvelopeView::setAmounts - which says the same thing in a
	// corner of the display rather than by flattening the subject.
};

/** Draw both envelopes of one drum, on ONE SHARED TIME AXIS.

    Fills `vcfOut` and `vcaOut` with `count` points each, both sampled
    over the same span, and returns that span in seconds so the caller
    can print it.

    THE SHARED AXIS IS WHY THIS IS ONE FUNCTION AND NOT TWO CALLS. Two
    independently scaled traces would draw a 45 ms amp envelope and a
    4 s filter envelope identically, and "which of these two outlasts
    the other" is the question the display exists to answer.

    THE SPAN IS THE LONGER ENVELOPE'S LENGTH - its attack plus its
    release, both of which the envelope now takes exactly.

    IT DRIVES REAL AREnvelope OBJECTS rather than evaluating an
    idealised exponential, so the picture cannot drift away from the
    audio the way a hand-written formula would the first time the
    envelope changed. */
double traceDrumEnvelopes (const ArSpec& vcf, const ArSpec& vca,
                           float* vcfOut, float* vcaOut, int count);

//------------------------------------------------------------------------
/** The MS-20 lowpass.
 
    WHICH MS-20, because there are two and they are not the same filter.
    This models the LATER, OTA (LM13600) revision, whose small-signal
    response Stinchcombe gives as
 
        Vo/Vin = -k1 / ( s^2/wc^2 + (2 - k1*k2) s/wc + 1 )
 
    - a two-pole lowpass whose DAMPING is reduced by the resonance
    feedback while its cutoff stays put, self-oscillating once
    k1*k2 >= 2. The earlier Korg35 version is a true Sallen-Key with a
    slightly different threshold (2 1/3) and, more importantly, its
    diodes in the FORWARD path, so they distort everything rather than
    only the resonance. The OTA revision's three back-to-back diodes sit
    in the feedback loop and colour the resonance alone, which is the
    sound people mean by "the MS-20 filter".
 
    HOW IT IS REALISED: a topology-preserving-transform state variable
    filter (Zavalishin), which gives exactly that denominator with
    2R = 2 - K, is stable for every cutoff including past Nyquist, and
    - the point here - exposes the BANDPASS signal, which is the
    resonance feedback path. So the diode saturator goes on that signal
    and nowhere else, which is topologically where the hardware's are.
 
    THE DIODES MAKE IT IMPLICIT, so it is solved with Newton rather than
    with a one-sample-delayed feedback. Delayed feedback would detune
    the resonance at high cutoffs and is exactly what makes cheap
    emulations sound wrong at the top of the knob.
 
    WHAT BOUNDS THE SELF-OSCILLATION: the damping is 2 - K*sat(bp)/bp.
    The constant 2 is the two integrator stages' own loss and stays
    linear; only the feedback is saturated. So as the oscillation grows,
    sat(bp)/bp falls, the net damping comes back positive, and the
    amplitude settles. Saturating the whole damping term instead - the
    obvious-looking simplification - removes the loss along with the
    feedback and the oscillation then grows without bound.
 
    KNOWN LIMITATION: no oversampling. The saturator generates harmonics
    that alias, and the reason that is tolerable here is that they are
    generated INSIDE a lowpass loop and then filtered by it - the
    standard argument for undersampled ZDF filters. It is still the
    first thing to change if the ping sounds gritty at high cutoffs; the
    place to do it is FilterDrumDsp::renderVoices, around the per-sample
    block. */
class Ms20Filter
{
public:
	void setSampleRate (double sampleRate);

	/** Cutoff in Hz. Clamped; call it per sample, it is one tan(). */
	void setCutoff (double hz);

	/** The MS-20 feedback gain K, 0 .. kMaxResonanceK. 2 is the
	    self-oscillation threshold. */
	void setResonance (double k);

	void reset ();

	/** One sample. */
	float process (float input);

	/** The residual of the implicit equation at the last sample, for
	    the tests: Newton has converged when this is tiny. */
	double lastResidual () const { return mResidual; }

	double g () const { return mG; }

private:
	double mSampleRate = 44100.0;
	double mG = 0.0;            // tan(pi*fc/fs)
	double mK = 0.0;            // feedback gain
	double mS1 = 0.0;           // bandpass integrator state
	double mS2 = 0.0;           // lowpass integrator state
	double mResidual = 0.0;
};

//------------------------------------------------------------------------
/** ONE DRUM. Noise into an MS-20 lowpass into a VCA, with an AR
    envelope on each and velocity scaling on both amounts.

    Everything that makes a drum sound is in here, and FilterDrumDsp
    owns two of them. It takes INTERNAL units throughout - seconds, not
    milliseconds; octaves, not per cent; the MS-20 feedback gain K, not
    a percentage of knob travel. The parameter table does every one of
    those conversions in toInternal() and nowhere else. */
class DrumVoice
{
public:
	/** Recomputes EVERY rate-dependent coefficient and resets state.
	    One place, no exceptions: a coefficient computed at 44.1 k and
	    used at 96 k is the bug that presents as "it sounds wrong on his
	    machine only". */
	void setSampleRate (double sampleRate);

	/** See Noise::setSeed - the two voices must not share one. */
	void setNoiseSeed (std::uint32_t seed) { mNoise.setSeed (seed); }

	void reset ();

	void setNoiseLevel (double gain)      { mNoiseLevel = gain; }
	void setCutoff (double hz)            { mCutoffHz = hz; }
	void setResonance (double k)          { mResonanceK = k; mFilter.setResonance (k); }

	void setVcfAttack (double seconds)    { mVcfEnv.setAttack (seconds); }
	void setVcfRelease (double seconds)   { mVcfEnv.setRelease (seconds); }
	void setVcfAmount (double octaves)    { mVcfAmount = octaves; }
	void setVcfVelocity (double sens)     { mVcfVelSens = sens; }

	void setVcaAttack (double seconds)    { mVcaEnv.setAttack (seconds); }
	void setVcaRelease (double seconds)   { mVcaEnv.setRelease (seconds); }
	void setVcaAmount (double gain)       { mVcaAmount = gain; }
	void setVcaVelocity (double sens)     { mVcaVelSens = sens; }

	/** The four shape controls, -1 Exponential .. +1 Logarithmic.

	    SHAPE IS PER STAGE, so there are four per drum and not two: an
	    attack and a release are different gestures and a drum usually
	    wants a different curve on each - a hard attack into a long
	    logarithmic tail is a sound the two-knob version cannot make. */
	void setVcfAttackShape (double shape)  { mVcfEnv.setAttackShape (shape); }
	void setVcfReleaseShape (double shape) { mVcfEnv.setReleaseShape (shape); }
	void setVcaAttackShape (double shape)  { mVcaEnv.setAttackShape (shape); }
	void setVcaReleaseShape (double shape) { mVcaEnv.setReleaseShape (shape); }

	double noiseLevel () const            { return mNoiseLevel; }

	/** Strike it. `velocity` is 0..1, as VST3 delivers it.

	    The two velocity-scaled amounts are LATCHED HERE and held for the
	    whole hit: a drum's velocity is a property of the hit, and
	    re-reading it per sample would mean a knob moved mid-decay
	    changed a note already sounding. */
	void trigger (double velocity);

	/** Is anything still sounding? The VCA alone decides - nothing that
	    happens to the cutoff of a muted signal is audible. */
	bool active () const { return !mVcaEnv.idle (); }

	/** Fill `out` with numSamples of this voice, INCLUDING kVoiceGain.
	    Writes rather than accumulates, and writes silence when idle, so
	    the caller never has to clear it. */
	void render (float* out, int numSamples);

	/** The velocity-scaled amounts the current hit is using. The panel
	    shows these; they are the only way to see what velocity did. */
	double currentVcfOctaves () const { return mVcfOctavesNow; }
	double currentVcaGain () const { return mVcaGainNow; }

	/** For the tests: the filter, so its residual can be inspected. */
	const Ms20Filter& filter () const { return mFilter; }

private:
	double mSampleRate  = 44100.0;

	double mNoiseLevel  = 1.0;
	double mCutoffHz    = 800.0;
	double mResonanceK  = 0.96;
	double mVcfAmount   = 3.6;    // octaves, signed
	double mVcfVelSens  = 1.0;
	double mVcaAmount   = 1.0;    // linear gain
	double mVcaVelSens  = 1.0;

	double mVcfOctavesNow = 0.0;
	double mVcaGainNow    = 0.0;

	Noise      mNoise;
	AREnvelope mVcfEnv;
	AREnvelope mVcaEnv;
	Ms20Filter mFilter;
};

//------------------------------------------------------------------------
/** The plug-in's audio line: two drums, a crossfader and the trim. */
class FilterDrumDsp
{
public:
	FilterDrumDsp ();

	void setSampleRate (double sampleRate);
	double sampleRate () const { return mSampleRate; }

	void setMaxBlockSize (int maxSamples);
	int maxBlockSize () const { return mMaxBlockSize; }

	void reset ();

	/** THE TWO DRUMS, by reference, so the processor can hand a
	    parameter to one of them without this class needing a setter per
	    parameter per drum. Twenty-two forwarding methods would be
	    twenty-two chances to wire drum 2's release to drum 1's. */
	DrumVoice& drum1 () { return mDrum1; }
	DrumVoice& drum2 () { return mDrum2; }
	const DrumVoice& drum1 () const { return mDrum1; }
	const DrumVoice& drum2 () const { return mDrum2; }

	/** The crossfader, 0..1, where 1 is ALL DRUM 1 - the top of the
	    fader and the top block on the panel. Smoothed. */
	void setMix (double mix);
	double mix () const { return mMix; }

	void setOutputTrimDb (double db);
	double outputTrimDb () const { return mTrimDb; }

	/** BOTH DRUMS, from one note. That is the whole of what "triggered
	    from the same MIDI note" means, and it is one line so it cannot
	    get out of step. */
	void trigger (double velocity);

	bool active () const { return mDrum1.active () || mDrum2.active (); }

	void render (float* left, float* right, int numSamples);

	/** The trim stage on its own, in place. Exposed for the tests: at
	    0 dB with the smoother settled it is bit-identical to its input. */
	void applyTrim (float* left, float* right, int numSamples);

	float currentTrimGain () const { return mTrim.value (); }

	/** The crossfade gains actually in use, after smoothing. */
	float currentGainDrum1 () const { return mGain1.value (); }
	float currentGainDrum2 () const { return mGain2.value (); }

private:
	void renderVoices (float* left, float* right, int numSamples);

	double mSampleRate   = 44100.0;
	int    mMaxBlockSize = 0;
	double mTrimDb       = 0.0;
	double mMix          = 0.5;

	DrumVoice mDrum1;
	DrumVoice mDrum2;

	Smoother mGain1;
	Smoother mGain2;
	Smoother mTrim;

	/** One mono block per drum, laid end to end. Sized in
	    setMaxBlockSize and never resized anywhere else, so the audio
	    thread never allocates. */
	std::vector<float> mScratch;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
