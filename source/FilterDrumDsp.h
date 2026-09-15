//------------------------------------------------------------------------
// FilterDrum - the audio line
//
// A MONOPHONIC ANALOGUE DRUM VOICE built around a model of the Korg
// MS-20's lowpass filter:
//
//   noise x Noise Level --> [ VCF: MS-20 LP ] --> [ VCA ] --> trim
//                                  ^                   ^
//                              AR envelope         AR envelope
//                            (cutoff, bipolar)      (level)
//
// Both envelopes are scaled by note velocity through their own
// sensitivity control. Everything a drum sound is here: the noise makes
// hats, snares and claps, and at high resonance the filter self-
// oscillates, which is what makes kicks and toms.
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

/** Is this resonance setting past the self-oscillation threshold? The
    panel lights its indicator off this, so the lamp and the audio
    cannot disagree about where the ping starts. */
inline bool selfOscillating (double resonanceK)
{
	return resonanceK >= 2.0;
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
 
      attack   time to reach 1.0, aiming at 1.2 through a one-pole
      release  time to fall to -60 dBFS (0.001) from 1.0
 
    Both are exponential, which is what an analogue EG does when it
    charges a capacitor through a resistor, and which is why a drum
    envelope sounds like a drum rather than like a triangle. */
class AREnvelope
{
public:
	enum class Stage { Idle, Attack, Release };

	void setSampleRate (double sampleRate);

	/** Times in SECONDS. Recomputed on every change, never per sample. */
	void setAttack (double seconds);
	void setRelease (double seconds);

	/** Start a hit. Does NOT reset the level to zero: a retrigger part
	    way through a decaying hit continues from where it is, which is
	    what stops a fast roll clicking on every note. */
	void trigger ();

	/** Cut to silence immediately - for a host reset, not for a
	    note-off. */
	void reset ();

	float next ();
	float level () const { return mLevel; }
	Stage stage () const { return mStage; }
	bool idle () const { return mStage == Stage::Idle; }

	double attackCoefficient () const { return mAttackCoeff; }
	double releaseCoefficient () const { return mReleaseCoeff; }

private:
	void recompute ();

	double mSampleRate   = 44100.0;
	double mAttackTime   = 0.001;
	double mReleaseTime  = 0.150;
	double mAttackCoeff  = 1.0;
	double mReleaseCoeff = 1.0;

	float mLevel = 0.f;
	Stage mStage = Stage::Idle;
};

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
class FilterDrumDsp
{
public:
	FilterDrumDsp ();

	/** Recomputes EVERY rate-dependent coefficient and resets state.
	    Every one, with no exceptions: a coefficient computed once at
	    44.1 k and then used at 96 k is the bug that presents as "it
	    sounds wrong on his machine only". From setupProcessing, never
	    from process(). */
	void setSampleRate (double sampleRate);
	double sampleRate () const { return mSampleRate; }

	void setMaxBlockSize (int maxSamples);
	int maxBlockSize () const { return mMaxBlockSize; }

	void reset ();

	//--------------------------------------------------------------------
	// Parameters. All take INTERNAL values - what the parameter table's
	// toInternal() produced - not the numbers on the panel. Seconds,
	// not milliseconds; octaves, not per cent; linear gain, not dB.
	//--------------------------------------------------------------------
	void setOutputTrimDb (double db);
	double outputTrimDb () const { return mTrimDb; }

	/** How much noise reaches the filter: 0 .. 1 linear.

	    At 0 the only excitation left is the per-note trigger impulse,
	    which is exactly the point - see the banner. */
	void setNoiseLevel (double gain)      { mNoiseLevel = gain; }
	double noiseLevel () const            { return mNoiseLevel; }

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

	//--------------------------------------------------------------------
	/** Strike the voice. `velocity` is 0..1, as VST3 delivers it.
 
	    MONOPHONIC: there is one voice, so this retriggers it. The two
	    velocity-scaled amounts are LATCHED HERE and held for the whole
	    hit, rather than read per sample - a drum's velocity is a
	    property of the hit, and re-reading it would mean a knob moved
	    mid-decay changed a note already sounding. */
	void trigger (double velocity);

	/** Is anything still sounding? The processor uses it for the
	    silence flags and for getTailSamples. */
	bool active () const;

	/** The velocity-scaled amounts the CURRENT hit is using. The panel
	    shows these, which is the only way to see what velocity did. */
	double currentVcfOctaves () const { return mVcfOctavesNow; }
	double currentVcaGain () const { return mVcaGainNow; }

	void render (float* left, float* right, int numSamples);

	/** The trim stage on its own, in place. Exposed for the tests: at
	    0 dB with the smoother settled it is bit-identical to its input,
	    and that is the assertion the signal path has to keep passing. */
	void applyTrim (float* left, float* right, int numSamples);

	float currentTrimGain () const { return mTrim.value (); }

	/** For the tests: the filter, so its residual can be inspected. */
	const Ms20Filter& filter () const { return mFilter; }

private:
	/** Sum the voice into the block, which arrives already zeroed. */
	void renderVoices (float* left, float* right, int numSamples);

	double mSampleRate   = 44100.0;
	int    mMaxBlockSize = 0;
	double mTrimDb       = 0.0;

	// Knob values, in internal units.
	double mNoiseLevel  = 1.0;
	double mCutoffHz    = 800.0;
	double mResonanceK  = 0.96;
	double mVcfAmount   = 3.6;    // octaves, signed
	double mVcfVelSens  = 1.0;
	double mVcaAmount   = 1.0;    // linear gain
	double mVcaVelSens  = 1.0;

	// Latched for the current hit.
	double mVcfOctavesNow = 0.0;
	double mVcaGainNow    = 0.0;
	double mVelocityNow   = 0.0;

	Noise       mNoise;
	AREnvelope  mVcfEnv;
	AREnvelope  mVcaEnv;
	Ms20Filter  mFilter;
	Smoother    mTrim;

	std::vector<float> mScratch;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
