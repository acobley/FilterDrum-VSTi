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

/** The diode pair in the resonance feedback path.
 
    tanh, scaled so the knee sits at kDiodeKnee. The real thing is three
    back-to-back diodes, so the threshold is a few hundred millivolts
    times three and the curve is sharper than a tanh at the corner; tanh
    is used because it is smooth, monotone and has an exact derivative,
    and Newton needs all three. A piecewise diode equation would give a
    slightly harder squelch and cost a solve that can fail to converge.
 
    MONOTONE IS NOT OPTIONAL: Newton's guarantee below rests on
    sat' >= 0. */
constexpr double kDiodeKnee = 0.9;

inline double diode (double v)
{
	return kDiodeKnee * std::tanh (v / kDiodeKnee);
}

/** d/dv of the above. 1 - tanh^2, which is where the falling incremental
    gain that bounds the self-oscillation comes from. */
inline double diodeSlope (double v)
{
	const double t = std::tanh (v / kDiodeKnee);
	return 1.0 - t * t;
}

/** Newton iterations per sample in the filter.
 
    THREE, and the tests assert the residual rather than trusting the
    number. One is visibly wrong at high resonance; two is inaudible but
    leaves a residual around 1e-6; three puts it at the limit of double
    precision for every setting the parameters can reach. The
    convergence is monotone (see Ms20Filter::process), so more only
    costs cycles. */
constexpr int kNewtonIterations = 3;

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
void AREnvelope::setAttackShape (double shape)
{
	mAttackShape = shape;
	recompute ();
}

//------------------------------------------------------------------------
void AREnvelope::setReleaseShape (double shape)
{
	mReleaseShape = shape;
	recompute ();
}

//------------------------------------------------------------------------
void AREnvelope::recompute ()
{
	mAttackCurve  = shapeToCurve (mAttackShape);
	mReleaseCurve = shapeToCurve (mReleaseShape);

	// A STAGE ALREADY RUNNING IS RE-PRIMED IN PLACE, from the phase it
	// has reached. Turning a shape knob during a hit therefore bends the
	// curve the hit is on rather than restarting it, and turning a time
	// knob re-scales the remaining travel. Leaving the old step and
	// denominator in place instead would keep the hit on the previous
	// curve until the next trigger, which reads as a control that does
	// not work.
	if (mStage == Stage::Attack)
		beginStage (mAttackTime, mAttackCurve);
	else if (mStage == Stage::Release)
		beginStage (mReleaseTime, mReleaseCurve);
}

//------------------------------------------------------------------------
void AREnvelope::beginStage (double seconds, double curve)
{
	// A ZERO-LENGTH STAGE IS ONE STEP, not a division by zero: the phase
	// goes straight to 1 on the next sample and the stage ends. The
	// table's minimum attack is 0.1 ms, so this is reachable only
	// through the DSP's own API, but it is reachable.
	const double samples = seconds * mSampleRate;
	mPhaseStep = (samples >= 1.0) ? (1.0 / samples) : 1.0;

	mLinear = (curve > -kLinearCurve && curve < kLinearCurve);

	if (mLinear)
	{
		mDenominator = 1.0;
		mExpTerm     = 1.0;
		mExpStep     = 1.0;
		return;
	}

	mDenominator = 1.0 - std::exp (-curve);

	// PRIMED FROM THE CURRENT PHASE, not from zero. recompute() calls
	// this mid-stage, and a retrigger enters the attack at whatever
	// phase the current level corresponds to.
	mExpTerm = std::exp (-curve * mPhase);
	mExpStep = std::exp (-curve * mPhaseStep);
}

//------------------------------------------------------------------------
void AREnvelope::trigger ()
{
	// NOT reset to zero. A retrigger part way through a decaying hit
	// continues from the level it is at, which is what stops a fast roll
	// clicking on every note - the discontinuity, not the loudness, is
	// what you hear.
	//
	// A PHASE-DRIVEN ENVELOPE HAS TO BE TOLD WHERE THAT IS. The old
	// recursion carried the level as its state and so continued for
	// free; this one carries a phase, so it asks which phase of the
	// ATTACK curve holds the level the envelope is at, and starts there.
	// Note the curve it inverts is the attack's, not whichever stage the
	// envelope was in - the level is about to be climbing an attack.
	mPhase = shapedRiseInverse (mLevel, mAttackCurve);
	mStage = Stage::Attack;
	beginStage (mAttackTime, mAttackCurve);
}

//------------------------------------------------------------------------
void AREnvelope::reset ()
{
	mLevel = 0.f;
	mPhase = 0.0;
	mStage = Stage::Idle;
	beginStage (mAttackTime, mAttackCurve);
}

//------------------------------------------------------------------------
float AREnvelope::next ()
{
	switch (mStage)
	{
		case Stage::Attack:
		{
			mPhase += mPhaseStep;

			if (mPhase >= 1.0)
			{
				// ARRIVES EXACTLY, whatever the shape. The old envelope
				// had to aim at 1.2 to get here at all.
				mLevel = 1.f;
				mPhase = 0.0;
				// RELEASE STARTS HERE, not at note-off. See the banner
				// on AREnvelope: a drum has to sound the same whether
				// the key was tapped or held.
				mStage = Stage::Release;
				beginStage (mReleaseTime, mReleaseCurve);
				break;
			}

			if (mLinear)
			{
				mLevel = static_cast<float> (mPhase);
			}
			else
			{
				mExpTerm *= mExpStep;
				mLevel = static_cast<float> ((1.0 - mExpTerm) / mDenominator);
			}
			break;
		}

		case Stage::Release:
		{
			mPhase += mPhaseStep;

			if (mPhase >= 1.0)
			{
				// ENDS AT EXACTLY ZERO and goes idle, rather than
				// decaying into denormals forever. An envelope that
				// never quite reaches zero keeps the voice "active",
				// which keeps the silence flags clear and keeps every
				// downstream plug-in awake for the life of the session.
				mLevel = 0.f;
				mPhase = 0.0;
				mStage = Stage::Idle;
				break;
			}

			if (mLinear)
			{
				mLevel = static_cast<float> (1.0 - mPhase);
			}
			else
			{
				mExpTerm *= mExpStep;
				mLevel = static_cast<float> (1.0 - (1.0 - mExpTerm) / mDenominator);
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

/** How much time a display needs to show this envelope.

    Just the attack plus the release, because the shaped envelope takes
    exactly those and lands on its endpoints. Before the shape controls
    this was a real question - the old envelope ran on to -100 dB, 5/3
    of its release knob, and drawing that far spent four fifths of the
    axis on a visibly flat line. */
double traceLength (const ArSpec& spec)
{
	return spec.attack + spec.release;
}

} // anonymous namespace

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
		e.setAttackShape (spec.attackShape);
		e.setReleaseShape (spec.releaseShape);
		e.reset ();
		e.trigger ();

		const double height = std::min (1.0, std::max (0.0, spec.height));

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

			out[written] = (float) (v * height);
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
	//     damping(bp) = 2*bp - K*diode(bp)
	//
	// The CONSTANT 2 is the two integrator stages' own loss and stays
	// linear. Only the FEEDBACK is saturated. That is what bounds the
	// self-oscillation: as |bp| grows, diode(bp)/bp falls, the net
	// damping 2 - K*diode(bp)/bp comes back positive and the amplitude
	// settles. Saturating the whole damping term instead removes the
	// loss along with the feedback, and then it grows without bound.
	//
	// Substituting gives F(hp) = 0 with
	//
	//   F  = hp*(1 + 2g + g^2) + 2*s1 - K*diode(g*hp + s1) + g*s1 + s2 - x
	//   F' = (1+g)^2 - K*g*diodeSlope(g*hp + s1)
	//
	// F' > 0 FOR EVERY g WHEN K <= 4, since diodeSlope <= 1 and
	// (1+g)^2 - 4g = (1-g)^2 >= 0. kMaxResonanceK is 2.4, which leaves
	// 1 - 0.4g + g^2 - no real roots, so strictly positive. Newton
	// therefore converges monotonically from any seed and cannot
	// divide by zero. Raise kMaxResonanceK past 4 and that guarantee
	// is gone.
	//--------------------------------------------------------------------
	const double denom = (1.0 + g) * (1.0 + g);
	const double rest  = 2.0 * mS1 + g * mS1 + mS2 - x;

	// Seeded from the linear solution - the K = 0 answer - which is
	// within a few per cent even at full resonance, so the three
	// iterations below are refining rather than searching.
	double hp = -rest / denom;

	for (int i = 0; i < kNewtonIterations; ++i)
	{
		const double bp = g * hp + mS1;
		const double F  = hp * denom - mK * diode (bp) + rest;
		const double Fp = denom - mK * g * diodeSlope (bp);
		hp -= F / Fp;
	}

	{
		const double bp = g * hp + mS1;
		mResidual = hp * denom - mK * diode (bp) + rest;
	}

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
