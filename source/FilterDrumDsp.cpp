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
	// The overshoot target is what makes the curve convex - an
	// exponential aimed exactly at 1.0 approaches it asymptotically and
	// never arrives, which is why a naive one-pole attack sounds soft
	// and measures as far longer than its knob says.
	static const double kLn6 = std::log (6.0);
	mAttackCoeff = (mAttackTime > 0.0)
	             ? 1.0 - std::exp (-kLn6 / (mAttackTime * mSampleRate))
	             : 1.0;

	// RELEASE: time to fall from 1.0 to 0.001, which is -60 dBFS.
	// exp(-t/tau) = 0.001 -> t = tau * ln 1000.
	static const double kLn1000 = std::log (1000.0);
	mReleaseCoeff = (mReleaseTime > 0.0)
	              ? 1.0 - std::exp (-kLn1000 / (mReleaseTime * mSampleRate))
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
			if (mLevel <= 1e-5f)
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
FilterDrumDsp::FilterDrumDsp ()
{
	// The constructor must leave the object usable, because a host may
	// call process() before setupProcessing in a rare restart. 44.1 k is
	// a guess; setSampleRate replaces it with the truth.
	setSampleRate (44100.0);
}

//------------------------------------------------------------------------
void FilterDrumDsp::setSampleRate (double sampleRate)
{
	if (sampleRate < 1000.0)
		sampleRate = 44100.0;

	mSampleRate = sampleRate;

	// EVERY rate-dependent coefficient, recomputed here and nowhere
	// else. Add to this function only, and the 96 k bug never gets
	// written.
	mTrim.setSampleRate (mSampleRate, kTrimSmoothingSeconds);
	mVcfEnv.setSampleRate (mSampleRate);
	mVcaEnv.setSampleRate (mSampleRate);
	mFilter.setSampleRate (mSampleRate);

	reset ();
}

//------------------------------------------------------------------------
void FilterDrumDsp::setMaxBlockSize (int maxSamples)
{
	mMaxBlockSize = (maxSamples > 0) ? maxSamples : 0;

	// THE ONLY ALLOCATION IN THIS CLASS, and it is not on the audio
	// thread.
	mScratch.assign (static_cast<size_t> (mMaxBlockSize) * kChannelCount, 0.f);
}

//------------------------------------------------------------------------
void FilterDrumDsp::reset ()
{
	// SNAP, not glide: a reset that left the trim smoother at zero would
	// fade the instrument in over 20 ms on every transport start, which
	// reads as a missing first hit.
	mTrim.snap (static_cast<float> (dbToGain (mTrimDb)));

	mVcfEnv.reset ();
	mVcaEnv.reset ();
	mFilter.reset ();

	mVcfOctavesNow = 0.0;
	mVcaGainNow    = 0.0;
	mVelocityNow   = 0.0;

	std::fill (mScratch.begin (), mScratch.end (), 0.f);
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
	mVelocityNow = velocity;

	// LATCHED FOR THE WHOLE HIT, both of them.
	//
	// A drum's velocity is a property of the hit, not of the moment. If
	// these were recomputed per sample from the current knob positions,
	// a knob moved during a decay would change a note that is already
	// sounding - and worse, automation on the Amount knob would make
	// every hit's level drift while it decayed. velocityScaled() is the
	// shared law; the panel calls it too.
	mVcfOctavesNow = velocityScaled (mVcfAmount, mVcfVelSens, velocity);
	mVcaGainNow    = velocityScaled (mVcaAmount, mVcaVelSens, velocity);

	mVcfEnv.trigger ();
	mVcaEnv.trigger ();

	// NOTHING EXCITES THE FILTER HERE. The noise is the only excitation
	// there is, so a hit with the Noise Level knob at 0 has nothing to
	// make a sound from - see the banner in FilterDrumDsp.h, which says
	// what that costs and what the cheaper fix would be.

	// THE FILTER STATE IS NOT RESET, and the noise is not reseeded.
	//
	// That is faithful rather than lazy. In the hardware the noise runs
	// continuously and the filter is always ringing; the VCA is what
	// opens. So the phase of a self-oscillating ping at the moment of
	// the hit is arbitrary, and two identical MIDI notes are not
	// bit-identical. Resetting here would make every kick start on the
	// same part of the cycle, which is more consistent and sounds
	// noticeably more like a sample and less like an analogue drum.
	//
	// The cost, stated plainly: this plug-in does not render
	// deterministically from a given MIDI sequence. If a bit-exact
	// bounce ever matters, this is the line to change and the noise
	// seed is the other half of it.
}

//------------------------------------------------------------------------
bool FilterDrumDsp::active () const
{
	// THE VCA ALONE DECIDES. The VCF envelope can still be running while
	// the VCA has closed, and nothing that happens to the cutoff of a
	// muted signal is audible. Asking both would keep the voice alive -
	// and the silence flags clear - through the whole of a long filter
	// release for no reason.
	return !mVcaEnv.idle ();
}

//------------------------------------------------------------------------
void FilterDrumDsp::renderVoices (float* left, float* right, int numSamples)
{
	// IDLE IS SILENT, and returning early is not just an optimisation:
	// it is what lets the processor flag the bus silent and let the rest
	// of the chain sleep. The block arrives zeroed, so there is nothing
	// to write.
	if (mVcaEnv.idle ())
		return;

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

		// The VCA. Its envelope, its velocity-scaled amount and the
		// voice's fixed headroom - see kVoiceGain, which is after the
		// filter because that is the only place it reaches the
		// self-oscillation. The output trim stays a separate stage so
		// it can be tested for bit-identity on its own.
		const float out = filtered * vcaEnv
		                * static_cast<float> (mVcaGainNow * kVoiceGain);

		// MONO VOICE, BOTH CHANNELS THE SAME. See kChannelCount.
		left[i]  = out;
		right[i] = out;
	}
}

//------------------------------------------------------------------------
void FilterDrumDsp::render (float* left, float* right, int numSamples)
{
	// A PARAMETER-ONLY BLOCK is legal and arrives in practice: hosts
	// send numSamples == 0 to deliver automation between audible
	// blocks. The guard belongs here, once, rather than in every voice.
	if (numSamples <= 0 || left == nullptr || right == nullptr)
		return;

	std::fill (left, left + numSamples, 0.f);
	std::fill (right, right + numSamples, 0.f);

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
