//------------------------------------------------------------------------
// FilterDrum - the audio line, implementation
//
// SDK-FREE. If a #include of anything under pluginterfaces/ or
// public.sdk/ ever appears in this file, the standalone test build stops
// compiling and the only executable verification on this project goes
// with it. See the banner in FilterDrumDsp.h.
//------------------------------------------------------------------------

#include "FilterDrumDsp.h"

#include <algorithm>

namespace FilterDrum {

//------------------------------------------------------------------------
namespace {

/** Keep a recursive state finite and bounded.
 
    BOTH CHECKS MATTER AND THEY ARE DIFFERENT. The isfinite test catches
    a NaN or an inf that has already happened: a NaN compares false
    against everything, so a plain clamp would pass it straight through
    and it would then poison every subsequent sample forever, silently,
    for the life of the plug-in instance. The clamp catches the merely
    enormous before it becomes an inf.
 
    PORTING-GUIDE.md section 5 asks for the ceiling to be well above any
    musical level and for a default render to be bit-identical with and
    without it. The tests assert exactly that. */
inline double guard (double v)
{
	if (!std::isfinite (v))
		return 0.0;
	if (v >  kStateCeiling) return  kStateCeiling;
	if (v < -kStateCeiling) return -kStateCeiling;
	return v;
}

/** THE DIODES in the resonance feedback path - the exponential diode law.

    The later (OTA) MS-20 has STRINGS OF THREE back-to-back diodes in the
    gain element of its feedback path (Stinchcombe). A diode's current is
    exponential in its voltage, so a resistor feeding a back-to-back
    pair - or pair of strings - obeys

        x = v + eps * sinh (v / Vd)      (x driven in, v across the diodes)

    Effectively linear while they are off, then a SHARP knee, then v still
    rising but only logarithmically: no flat ceiling. This replaced a tanh
    stand-in (knee 0.9) - the right PLACE for the clipping, the wrong
    CURVE: tanh bends early and gently and then flattens completely.

    THE SHARPNESS is knee / Vd in thermal voltages: a silicon diode's
    0.6 V over n Vt = 1.8 x 26 mV is 12.8 - and a string of three is three
    times both, so the SAME 12.8; only the knee moves. From the two,
    Vd = knee / sharpness and eps = knee / sinh (sharpness): at the knee
    the diodes carry as much as the resistor alone would, and below it
    almost nothing - small-signal gain 1 - 7e-5, so the filter screen's
    response and the self-oscillation threshold (K = 2) are unmoved.
    The same law and the same 12.8 as R2Synth's MS-20 filters.

    THE KNEE IS SET BY MEASUREMENT, not derived - this engine's signal
    units are not volts, and the gains round the diodes are not modelled.
    See kDiodeKnee. */
constexpr double kDiodeSharpness = 12.8;

/** Where the diodes start to conduct, in the filter's signal units.

    SET BY MEASUREMENT (Oct 2026), against the loudest thing the
    INSTRUMENT can do at unity trim - both drums self-oscillating at the
    top of the resonance knob, mixed 50 % - which the tanh stand-in put
    at -2.20 dBFS. Swept at 48 k (tests/DspTests.cpp's measurements):

        knee   one drum, full res   both drums, full res   default pair
        0.5        -4.98                -3.74                 -7.24
        0.6        -4.28                -2.94                 -6.88
        0.7        -3.65                -2.25                 -6.58   <-
        0.8        -3.09                -1.61                 -6.35
        0.9        -2.68                -0.95                 -6.16
        (tanh, knee 0.9: -3.09 / -2.20 / -6.80)

    0.7 keeps the instrument's worst case where it was, so the output
    trim needs no change. (0.8 would match the single drum instead, but
    leaves only 1.6 dB above the pair - the diode law's waveform sums
    differently from the tanh's.) */
constexpr double kDiodeKnee = 0.7;

const double kDiodeVd  = kDiodeKnee / kDiodeSharpness;
const double kDiodeEps = kDiodeKnee / std::sinh (kDiodeSharpness);

/** The drive x that puts v across the diodes - the law itself. */
inline double diodeDrive (double v)
{
	return v + kDiodeEps * std::sinh (v / kDiodeVd);
}

/** THE DIODES INSIDE THE LOOP: solves  (1 - b) v + eps sinh (v / Vd) = a
    for v. Inside the filter's zero-delay loop the drive into the diodes
    is itself a function of their voltage - a + b v - see
    Ms20Filter::process for a and b, and for why b < 1.

    The left side is odd, increasing and convex for v > 0, so the root is
    unique, and Newton started ABOVE it - at the smaller of the
    diodes-off answer a / (1 - b) and the diodes-only answer
    Vd asinh (a / eps), both of which are - falls monotonically onto it.
    A few iterations at most; never more than kMaxDiodeIterations. The
    same solver as R2Synth's ms20DiodeSolve(). */
constexpr int kMaxDiodeIterations = 16;

double diodeSolve (double a, double b)
{
	if (!std::isfinite (a))
		return 0.0;
	const double drive = std::fabs (a);
	const double c = 1.0 - b;
	if (drive == 0.0 || !(c > 0.0))
		return 0.0;

	// Start ABOVE the root: both of these are.
	double v = std::min (drive / c, kDiodeVd * std::asinh (drive / kDiodeEps));
	for (int i = 0; i < kMaxDiodeIterations; ++i)
	{
		const double x  = v / kDiodeVd;
		const double f  = c * v + kDiodeEps * std::sinh (x) - drive;
		const double fp = c + (kDiodeEps / kDiodeVd) * std::cosh (x);
		const double step = f / fp;
		v -= step;
		if (std::fabs (step) <= 1.0e-13 * (1.0 + v))
			break;
	}
	return (a < 0.0) ? -v : v;
}

} // anonymous namespace

//------------------------------------------------------------------------
void Smoother::setSampleRate (double sampleRate, double seconds)
{
	if (sampleRate < 1000.0)
		sampleRate = 44100.0;

	if (seconds <= 0.0)
	{
		// No glide asked for: follow the target exactly. Not a
		// degenerate case to guard against elsewhere - it is how a
		// parameter that must NOT be smoothed is expressed.
		mCoeff = 1.f;
		return;
	}

	mCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
	mCoeff = std::min (1.f, std::max (0.f, mCoeff));
}

//------------------------------------------------------------------------
void Smoother::snap (float target)
{
	mTarget = target;
	mValue  = target;
}

//------------------------------------------------------------------------
float Smoother::next ()
{
	mValue += (mTarget - mValue) * mCoeff;

	// Denormal flush on the recursive state only. The test that unity is
	// bit-identical depends on this NOT touching the signal.
	if (mValue < kDenormalFloor && mValue > -kDenormalFloor)
		mValue = 0.f;

	return mValue;
}

//------------------------------------------------------------------------
void Noise::setSeed (std::uint32_t seed)
{
	// ZERO IS REFUSED, not accepted. xorshift's state cannot escape zero
	// - every shift and xor of zero is zero - so a zero-seeded generator
	// outputs silence for the life of the plug-in, and the drum it feeds
	// would simply never make a sound.
	mState = (seed != 0u) ? seed : 0x9E3779B9u;
}

//------------------------------------------------------------------------
float Noise::next ()
{
	// xorshift32. The shift triple 13/17/5 is Marsaglia's; the state
	// must never be zero, which the non-zero seed and the fact that
	// xorshift cannot reach zero from non-zero together guarantee.
	mState ^= mState << 13;
	mState ^= mState >> 17;
	mState ^= mState << 5;

	// Top 24 bits into -1..+1. The TOP bits, because xorshift's low bits
	// are the weakest - the same reason std::rand() % n is a bad idea.
	const std::uint32_t bits = mState >> 8;
	return static_cast<float> (bits) * (2.f / 16777216.f) - 1.f;
}

//------------------------------------------------------------------------
void AREnvelope::setSampleRate (double sampleRate)
{
	mSampleRate = (sampleRate >= 1000.0) ? sampleRate : 44100.0;
	recompute ();
}

//------------------------------------------------------------------------
void AREnvelope::setAttack (double seconds)
{
	mAttackTime = (seconds > 0.0) ? seconds : 0.0;
	recompute ();
}

//------------------------------------------------------------------------
void AREnvelope::setRelease (double seconds)
{
	mReleaseTime = (seconds > 0.0) ? seconds : 0.0;
	recompute ();
}

//------------------------------------------------------------------------
void AREnvelope::recompute ()
{
	// ATTACK: a one-pole aimed at 1.2, stopped when it passes 1.0.
	//
	// level(t) = 1.2 * (1 - exp(-t/tau)), which reaches 1.0 when
	// exp(-t/tau) = 1/6, so t = tau * ln 6. Solving for tau gives the
	// coefficient below, and the attack time then MEANS "time to reach
	// full", which is the only definition a test can check.
	//
	// The overshoot target is what makes the attack ARRIVE. The curve
	// is concave either way - fast out of the gate, easing into the
	// peak - but an exponential aimed exactly at 1.0 approaches it
	// asymptotically and never gets there, which is why a naive
	// one-pole attack sounds soft and measures as far longer than its
	// knob says. Aiming at 1.2 and stopping at 1.0 cuts the curve off
	// while it is still moving.
	static const double kLn6 = std::log (6.0);
	mAttackCoeff = (mAttackTime > 0.0)
	             ? 1.0 - std::exp (-kLn6 / (mAttackTime * mSampleRate))
	             : 1.0;

	// RELEASE: time to fall from 1.0 to kReleaseTargetLevel, which is
	// -60 dBFS. exp(-t/tau) = level -> t = tau * ln (1/level).
	//
	// THE ENVELOPE DOES NOT STOP THERE - see kEnvelopeIdleLevel and
	// releaseTailSeconds() in the header. next() runs on to -100 dB,
	// which takes 5/3 of this time. The knob still means what it says;
	// it is getTailSamples() that has to know the difference.
	static const double kLnRelease = -std::log (kReleaseTargetLevel);
	mReleaseCoeff = (mReleaseTime > 0.0)
	              ? 1.0 - std::exp (-kLnRelease / (mReleaseTime * mSampleRate))
	              : 1.0;

	mAttackCoeff  = std::min (1.0, std::max (0.0, mAttackCoeff));
	mReleaseCoeff = std::min (1.0, std::max (0.0, mReleaseCoeff));
}

//------------------------------------------------------------------------
void AREnvelope::trigger ()
{
	// NOT reset to zero. A retrigger part way through a decaying hit
	// continues from the level it is at, which is what stops a fast roll
	// clicking on every note - the discontinuity, not the loudness, is
	// what you hear.
	mStage = Stage::Attack;
}

//------------------------------------------------------------------------
void AREnvelope::reset ()
{
	mLevel = 0.f;
	mStage = Stage::Idle;
}

//------------------------------------------------------------------------
float AREnvelope::next ()
{
	switch (mStage)
	{
		case Stage::Attack:
		{
			// Aiming at 1.2 - see recompute().
			mLevel += static_cast<float> ((1.2 - mLevel) * mAttackCoeff);
			if (mLevel >= 1.f)
			{
				mLevel = 1.f;
				// RELEASE STARTS HERE, not at note-off. See the banner
				// on AREnvelope: a drum has to sound the same whether
				// the key was tapped or held.
				mStage = Stage::Release;
			}
			break;
		}

		case Stage::Release:
		{
			mLevel -= mLevel * static_cast<float> (mReleaseCoeff);
			if (mLevel <= static_cast<float> (kEnvelopeIdleLevel))
			{
				// Ends at EXACTLY zero and goes idle, rather than
				// decaying into denormals forever. An envelope that
				// never quite reaches zero keeps the voice "active",
				// which keeps the silence flags clear and keeps every
				// downstream plug-in awake for the life of the session.
				mLevel = 0.f;
				mStage = Stage::Idle;
			}
			break;
		}

		case Stage::Idle:
		default:
			mLevel = 0.f;
			break;
	}

	return mLevel;
}

//------------------------------------------------------------------------
namespace {

/** Resolution of the trace, in points sampled per point drawn. Four is
    enough that a corner lands within a quarter of a pixel of where it
    belongs, and it keeps the work BOUNDED - the loop runs count * 4
    times whatever the envelope lengths are, so a 6.7 s release does not
    cost a thousand times a 7 ms one. A fixed 48 kHz trace would. */
constexpr int kTraceOversample = 4;

/** AREnvelope::setSampleRate refuses anything below 1000 and silently
    substitutes 44100, which would put the trace on a timebase that has
    nothing to do with the one it is drawing. So the rate is clamped
    HERE, where the clamp is visible, and the decimation absorbs it. */
constexpr double kMinTraceRate = 1000.0;
constexpr double kMaxTraceRate = 1000000.0;

/** How much time a display needs to show this envelope: the attack plus
    the release AS THE KNOB MEANS IT, which is the time to -60 dB.

    NOT releaseTailSeconds(). That is the right answer for
    getTailSamples() and the wrong one here, and the difference is what
    the two are for. The tail question is "when has the voice finished",
    where the -100 dB remainder counts. The display question is "what
    shape is this", where it does not: at -60 dB the trace is 0.001 of
    full height, which on a sixty-pixel panel is six hundredths of a
    pixel off the floor. Drawing to -100 dB spends four fifths of the
    axis on a line that is visibly flat and squeezes everything worth
    seeing into the first fifth - which is exactly what the first
    version of this did. */
double traceLength (const ArSpec& spec)
{
	return spec.attack + spec.release;
}

} // anonymous namespace

//------------------------------------------------------------------------
double traceFilterResponse (double cutoffHz, double k, double sampleRate,
                            double minHz, double maxHz, double topDb, double bottomDb,
                            float* out, int count)
{
	auto toUnit = [topDb, bottomDb] (double gain) {
		const double db = gainToDb (gain);
		double u = (db - bottomDb) / (topDb - bottomDb);
		if (!(u > 0.0)) u = 0.0;           // also catches NaN
		if (u > 1.0)    u = 1.0;
		return u;
	};

	if (out && count >= 2 && minHz > 0.0 && maxHz > minHz && topDb > bottomDb)
	{
		const double span = std::log (maxHz / minHz);
		for (int i = 0; i < count; ++i)
		{
			const double f = minHz * std::exp (span * static_cast<double> (i) / (count - 1));
			out[i] = static_cast<float> (toUnit (ms20ResponseGain (f, cutoffHz, k, sampleRate)));
		}
	}

	// THE DOT SITS AT THE CUTOFF THE FILTER WILL USE, which past the
	// ceiling is the ceiling - the same clamp as Ms20Filter::setCutoff.
	const double rate = (sampleRate >= 1000.0) ? sampleRate : 44100.0;
	const double fc = std::min (std::max (cutoffHz, kMinCutoffHz), rate * kMaxCutoffFraction);
	return toUnit (ms20ResponseGain (fc, fc, k, rate));
}

//------------------------------------------------------------------------
double traceDrumEnvelopes (const ArSpec& vcf, const ArSpec& vca,
                           float* vcfOut, float* vcaOut, int count)
{
	if (count < 2)
		return 0.0;

	auto fill = [count] (float* out, float v) {
		if (out)
			for (int i = 0; i < count; ++i)
				out[i] = v;
	};

	const double span = std::max (traceLength (vcf), traceLength (vca));
	if (!(span > 0.0))
	{
		// Both envelopes instantaneous. A flat floor is the honest
		// picture and there is no span to report.
		fill (vcfOut, 0.f);
		fill (vcaOut, 0.f);
		return 0.0;
	}

	double rate = (double) count * kTraceOversample / span;
	rate = std::min (kMaxTraceRate, std::max (kMinTraceRate, rate));

	long total = (long) std::ceil (span * rate);
	if (total < 2)
		total = 2;

	auto run = [&] (const ArSpec& spec, float* out)
	{
		if (!out)
			return;

		AREnvelope e;
		e.setSampleRate (rate);
		e.setAttack (spec.attack);
		e.setRelease (spec.release);
		e.reset ();
		e.trigger ();

		// EVERY POINT IS EMITTED FROM THE SAME RUN. Re-running the
		// envelope per point, or seeking, would be the obvious way to
		// write this and would also be quadratic.
		int written = 0;
		long nextIndex = 0;
		for (long n = 0; n < total && written < count; ++n)
		{
			const float v = e.next ();
			if (n < nextIndex)
				continue;

			out[written] = v;
			++written;
			nextIndex = (long) ((double) written * (total - 1) / (count - 1));
		}

		// A SHORTER ENVELOPE ON A LONGER AXIS ends early and the rest of
		// its trace is floor - which is the whole point of the shared
		// axis, so it is filled in rather than left as whatever was in
		// the buffer.
		for (; written < count; ++written)
			out[written] = 0.f;
	};

	run (vcf, vcfOut);
	run (vca, vcaOut);

	return span;
}

//------------------------------------------------------------------------
void Ms20Filter::setSampleRate (double sampleRate)
{
	mSampleRate = (sampleRate >= 1000.0) ? sampleRate : 44100.0;
	reset ();
}

//------------------------------------------------------------------------
void Ms20Filter::setCutoff (double hz)
{
	const double maxHz = mSampleRate * kMaxCutoffFraction;
	if (hz < kMinCutoffHz) hz = kMinCutoffHz;
	if (hz > maxHz)        hz = maxHz;

	// The TPT prewarp. tan() is why the cutoff has to be kept off
	// Nyquist: at fs/2 it is infinite, and the clamp above is what stops
	// a 20 kHz knob setting at a 32 kHz sample rate producing one.
	mG = std::tan (3.14159265358979323846 * hz / mSampleRate);
}

//------------------------------------------------------------------------
void Ms20Filter::setResonance (double k)
{
	if (k < 0.0) k = 0.0;
	if (k > kMaxResonanceK) k = kMaxResonanceK;
	mK = k;
}

//------------------------------------------------------------------------
void Ms20Filter::reset ()
{
	mS1 = 0.0;
	mS2 = 0.0;
	mResidual = 0.0;
}

//------------------------------------------------------------------------
float Ms20Filter::process (float input)
{
	const double x = static_cast<double> (input);
	const double g = mG;

	//--------------------------------------------------------------------
	// The implicit equation.
	//
	// TPT state variable filter, with hp the unknown:
	//
	//     bp = g*hp + s1
	//     lp = g*bp + s2
	//     hp = x - damping(bp) - lp
	//
	// and the damping is where the MS-20 lives:
	//
	//     damping(bp) = 2*bp - K*diode(bp)       (diode: the law above)
	//
	// The CONSTANT 2 is the two integrator stages' own loss and stays
	// linear. Only the FEEDBACK is saturated. That is what bounds the
	// self-oscillation: as |bp| grows, diode(bp)/bp falls, the net
	// damping 2 - K*diode(bp)/bp comes back positive and the amplitude
	// settles. Saturating the whole damping term instead removes the
	// loss along with the feedback, and then it grows without bound.
	//
	// THE UNKNOWN IS THE DIODE VOLTAGE v = diode(bp), not hp, so the
	// loop is ONE exact solve rather than Newton wrapped round an
	// implicit clipper. Writing the feedback as K*v:
	//
	//     hp * (1+g)^2 = K*v - rest,      rest = (2+g)*s1 + s2 - x
	//     bp = g*hp + s1 = a + b*v,       a = s1 - g*rest/(1+g)^2
	//                                     b = g*K/(1+g)^2
	//
	// and the diodes say bp = v + eps*sinh(v/Vd). Together:
	//
	//     (1 - b) v + eps sinh (v / Vd) = a
	//
	// which diodeSolve() solves exactly. It needs b < 1: g/(1+g)^2 is at
	// most 1/4 (at g = 1), so b <= K/4, and kMaxResonanceK = 2.4 gives
	// b <= 0.6 at every cutoff. RAISE kMaxResonanceK TO 4 AND THAT
	// GUARANTEE IS GONE - the same bound the old Newton-on-hp solve had,
	// for the same reason.
	//--------------------------------------------------------------------
	const double denom = (1.0 + g) * (1.0 + g);
	const double rest  = 2.0 * mS1 + g * mS1 + mS2 - x;

	const double a = mS1 - g * rest / denom;
	const double b = g * mK / denom;
	const double v = diodeSolve (a, b);
	const double hp = (mK * v - rest) / denom;

	// THE RESIDUAL is the diode law at the answer: bp against the drive
	// that puts v across the diodes. The tests hold it below 1e-9.
	mResidual = (g * hp + mS1) - diodeDrive (v);

	// Integrate. Trapezoidal, which is what makes this TPT: the state
	// update uses the SAME g as the solve, so the discrete filter's
	// poles are the bilinear image of the analogue ones at every
	// cutoff - including well past Nyquist, where a naive Euler
	// integrator would have gone unstable long before.
	const double bp = g * hp + mS1;
	mS1 = guard (g * hp + bp);

	const double lp = g * bp + mS2;
	mS2 = guard (g * bp + lp);

	return static_cast<float> (guard (lp));
}

//------------------------------------------------------------------------
void DrumVoice::setSampleRate (double sampleRate)
{
	if (sampleRate < 1000.0)
		sampleRate = 44100.0;

	mSampleRate = sampleRate;

	// EVERY rate-dependent coefficient, recomputed here and nowhere
	// else. Add to this function only, and the 96 k bug never gets
	// written.
	mVcfEnv.setSampleRate (mSampleRate);
	mVcaEnv.setSampleRate (mSampleRate);
	mFilter.setSampleRate (mSampleRate);

	reset ();
}

//------------------------------------------------------------------------
void DrumVoice::reset ()
{
	mVcfEnv.reset ();
	mVcaEnv.reset ();
	mFilter.reset ();

	mVcfOctavesNow = 0.0;
	mVcaGainNow    = 0.0;
}

//------------------------------------------------------------------------
void DrumVoice::trigger (double velocity)
{
	// LATCHED FOR THE WHOLE HIT, both of them. velocityScaled() is the
	// shared law; the panel calls it too.
	mVcfOctavesNow = velocityScaled (mVcfAmount, mVcfVelSens, velocity);
	mVcaGainNow    = velocityScaled (mVcaAmount, mVcaVelSens, velocity);

	mVcfEnv.trigger ();
	mVcaEnv.trigger ();

	// NOTHING EXCITES THE FILTER HERE. The noise is the only excitation
	// there is, so a hit with this drum's Noise Level at 0 has nothing
	// to make a sound from - see the banner in FilterDrumDsp.h.

	// THE FILTER STATE IS NOT RESET, and the noise is not reseeded.
	//
	// That is faithful rather than lazy: the phase of a self-oscillating
	// tone at the moment of a hit is arbitrary, and resetting would make
	// every kick start on the same part of the cycle, which sounds
	// noticeably more like a sample and less like an analogue drum.
	//
	// The cost, stated plainly: this plug-in does not render
	// deterministically from a given MIDI sequence.
}

//------------------------------------------------------------------------
void DrumVoice::render (float* out, int numSamples)
{
	// IDLE IS SILENT, and it is written rather than skipped because the
	// caller mixes this buffer unconditionally - a stale block left in
	// it would be the last hit played again under the crossfader.
	if (!active ())
	{
		std::fill (out, out + numSamples, 0.f);
		return;
	}

	for (int i = 0; i < numSamples; ++i)
	{
		const float vcaEnv = mVcaEnv.next ();
		const float vcfEnv = mVcfEnv.next ();

		// THE CUTOFF IS RECOMPUTED EVERY SAMPLE, through the shared
		// cutoffWithEnv() that the panel also calls. A per-block cutoff
		// would step the filter once per buffer, and on a fast sweep -
		// which is every kick - that is audible as a zipper.
		mFilter.setCutoff (cutoffWithEnv (mCutoffHz, mVcfOctavesNow,
		                                  static_cast<double> (vcfEnv), mSampleRate));

		// THE ONLY EXCITATION. At 0 this is exactly zero, and a linear
		// filter fed exact zero from a zero state stays at exact zero -
		// so the knob's bottom end is silence, not a pure tone.
		const float excitation = mNoise.next () * static_cast<float> (mNoiseLevel);

		const float filtered = mFilter.process (excitation);

		// The VCA: its envelope, its velocity-scaled amount and the
		// voice's fixed headroom. The crossfader and the output trim
		// are the caller's, and stay separate stages so each can be
		// tested on its own.
		out[i] = filtered * vcaEnv * static_cast<float> (mVcaGainNow * kVoiceGain);
	}
}

//------------------------------------------------------------------------
FilterDrumDsp::FilterDrumDsp ()
{
	// THE TWO VOICES GET DIFFERENT NOISE SEEDS, and this is the line
	// that makes the pair a layer rather than one drum 6 dB louder. See
	// Noise::setSeed. The constants are arbitrary and only have to
	// differ; they are golden-ratio odd values because that is a
	// habit that keeps low-order bits from lining up.
	mDrum1.setNoiseSeed (0x9E3779B9u);
	mDrum2.setNoiseSeed (0x7F4A7C15u);

	// The constructor must leave the object usable, because a host may
	// call process() before setupProcessing in a rare restart.
	setSampleRate (44100.0);
}

//------------------------------------------------------------------------
void FilterDrumDsp::setSampleRate (double sampleRate)
{
	if (sampleRate < 1000.0)
		sampleRate = 44100.0;

	mSampleRate = sampleRate;

	mTrim.setSampleRate (mSampleRate, kTrimSmoothingSeconds);
	mGain1.setSampleRate (mSampleRate, kMixSmoothingSeconds);
	mGain2.setSampleRate (mSampleRate, kMixSmoothingSeconds);

	mDrum1.setSampleRate (mSampleRate);
	mDrum2.setSampleRate (mSampleRate);

	reset ();
}

//------------------------------------------------------------------------
void FilterDrumDsp::setMaxBlockSize (int maxSamples)
{
	mMaxBlockSize = (maxSamples > 0) ? maxSamples : 0;

	// THE ONLY ALLOCATION IN THIS CLASS, and it is not on the audio
	// thread. One mono block per drum, laid end to end.
	mScratch.assign (static_cast<size_t> (mMaxBlockSize) * 2, 0.f);
}

//------------------------------------------------------------------------
void FilterDrumDsp::reset ()
{
	// SNAP, not glide, on all three: a reset that left a smoother at
	// zero would fade the instrument in on every transport start, which
	// reads as a missing first hit.
	mTrim.snap (static_cast<float> (dbToGain (mTrimDb)));
	mGain1.snap (static_cast<float> (crossfadeGainDrum1 (mMix)));
	mGain2.snap (static_cast<float> (crossfadeGainDrum2 (mMix)));

	mDrum1.reset ();
	mDrum2.reset ();

	std::fill (mScratch.begin (), mScratch.end (), 0.f);
}

//------------------------------------------------------------------------
void FilterDrumDsp::setMix (double mix)
{
	mMix = mix;

	// The law lives in FilterDrumDsp.h so the panel's readout can call
	// the same copy. Only the TARGETS are set here - the smoothers get
	// there over kMixSmoothingSeconds, because a stepped gain is a click.
	mGain1.setTarget (static_cast<float> (crossfadeGainDrum1 (mix)));
	mGain2.setTarget (static_cast<float> (crossfadeGainDrum2 (mix)));
}

//------------------------------------------------------------------------
void FilterDrumDsp::setOutputTrimDb (double db)
{
	mTrimDb = db;
	mTrim.setTarget (static_cast<float> (dbToGain (db)));
}

//------------------------------------------------------------------------
void FilterDrumDsp::trigger (double velocity)
{
	// ONE NOTE, BOTH DRUMS. The whole of "triggered from the same MIDI
	// note", in one place, so the two cannot get out of step.
	mDrum1.trigger (velocity);
	mDrum2.trigger (velocity);
}

//------------------------------------------------------------------------
void FilterDrumDsp::renderVoices (float* left, float* right, int numSamples)
{
	float* const bufA = mScratch.data ();
	float* const bufB = mScratch.data () + numSamples;

	mDrum1.render (bufA, numSamples);
	mDrum2.render (bufB, numSamples);

	for (int i = 0; i < numSamples; ++i)
	{
		// BOTH GAINS ADVANCE EVERY SAMPLE, even when a drum is idle and
		// its buffer is silent. Advancing them only when audible would
		// leave the smoother wherever it was and put the crossfade in
		// the wrong place on the next hit.
		const float g1 = mGain1.next ();
		const float g2 = mGain2.next ();

		const float out = bufA[i] * g1 + bufB[i] * g2;

		// MONO VOICES, BOTH CHANNELS THE SAME. Two drums panned apart
		// would be a wider but weaker hit, which is the opposite of what
		// a layered kick wants.
		left[i]  = out;
		right[i] = out;
	}
}

//------------------------------------------------------------------------
void FilterDrumDsp::render (float* left, float* right, int numSamples)
{
	// A PARAMETER-ONLY BLOCK is legal and arrives in practice: hosts
	// send numSamples == 0 to deliver automation between audible
	// blocks.
	if (numSamples <= 0 || left == nullptr || right == nullptr)
		return;

	// NEVER TRUST THE BLOCK SIZE. The scratch holds two mono blocks, so
	// a host that hands over more than it promised would walk off the
	// end of the second one. Growing it here would be an allocation on
	// the audio thread, so the block is refused instead - silence beats
	// a heap corruption.
	if (mScratch.size () < static_cast<size_t> (numSamples) * 2)
	{
		std::fill (left, left + numSamples, 0.f);
		std::fill (right, right + numSamples, 0.f);
		return;
	}

	renderVoices (left, right, numSamples);

	applyTrim (left, right, numSamples);
}

//------------------------------------------------------------------------
void FilterDrumDsp::applyTrim (float* left, float* right, int numSamples)
{
	if (numSamples <= 0 || left == nullptr || right == nullptr)
		return;

	// PER SAMPLE, and both channels off the SAME gain value - a stereo
	// trim that advanced its smoother once per channel would walk the
	// image while it glided.
	for (int i = 0; i < numSamples; ++i)
	{
		const float gain = mTrim.next ();
		left[i]  *= gain;
		right[i] *= gain;
	}
}

//------------------------------------------------------------------------
} // namespace FilterDrum
