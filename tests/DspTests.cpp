//------------------------------------------------------------------------
// FilterDrum - DSP tests
//
// SDK-FREE, compiled directly against FilterDrumDsp.cpp:
//
//   c++ -std=c++17 -O2 -I../source DspTests.cpp ../source/FilterDrumDsp.cpp
//       -o /tmp/dsptests  &&  /tmp/dsptests
//
// THIS IS THE ONLY EXECUTABLE VERIFICATION ON THIS PROJECT during
// development. The session that writes the code reaches the Mac through
// a Linux VM and cannot run cmake, Xcode, the SDK validator or auval, so
// everything that can be asserted without a host is asserted here - and
// the DSP being free of SDK types is what makes that possible.
//
// The suite is deliberately heavy on NEGATIVE CONTROLS. A test that
// says "the filter self-oscillates at K = 2.4" is worth very little on
// its own, because a filter that self-oscillates at every setting would
// also pass it. Each such assertion is therefore paired with one that
// must FAIL to hold just below the threshold, and the harness itself is
// proved capable of reporting a failure.
//------------------------------------------------------------------------

#include "FilterDrumDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace FilterDrum;

//------------------------------------------------------------------------
namespace {

int gChecks = 0;
int gFailures = 0;

void check (bool condition, const std::string& what)
{
	++gChecks;
	if (!condition)
	{
		++gFailures;
		std::printf ("  FAIL  %s\n", what.c_str ());
	}
}

void checkClose (double got, double want, double tolerance, const std::string& what)
{
	const bool ok = std::fabs (got - want) <= tolerance;
	++gChecks;
	if (!ok)
	{
		++gFailures;
		std::printf ("  FAIL  %s: got %.9g, want %.9g (tol %.3g)\n",
		             what.c_str (), got, want, tolerance);
	}
}

/** The rates anyone actually runs. Every rate-dependent assertion is
    made at all of them, because a coefficient that is only right at
    44.1 k is the classic "works on my machine" bug. */
const double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

std::string hz (double r) { return std::to_string (static_cast<int> (r)) + " Hz"; }

double peak (const std::vector<float>& block)
{
	float p = 0.f;
	for (float v : block)
		p = std::max (p, std::fabs (v));
	return static_cast<double> (p);
}

double peakDbFS (const std::vector<float>& block)
{
	const double p = peak (block);
	return (p <= 0.0) ? -std::numeric_limits<double>::infinity () : 20.0 * std::log10 (p);
}

bool allFinite (const std::vector<float>& block)
{
	for (float v : block)
		if (!std::isfinite (v))
			return false;
	return true;
}

/** Steady-state magnitude response of the filter at one frequency, by
    driving it with a sine and measuring the output once the transient
    has gone. Slow and obvious, which is what a test wants. */
double filterMagnitude (double cutoff, double k, double freq, double rate)
{
	Ms20Filter f;
	f.setSampleRate (rate);
	f.setCutoff (cutoff);
	f.setResonance (k);
	f.reset ();

	const int settle = static_cast<int> (rate * 0.5);
	const int measure = static_cast<int> (rate * 0.2);
	const double w = 2.0 * 3.14159265358979323846 * freq / rate;

	// SMALL AMPLITUDE, and this is not incidental: the diode saturator
	// makes the filter nonlinear, so "the magnitude response" only means
	// anything in the small-signal limit - which is exactly the limit
	// Stinchcombe's transfer function is derived in. Drive it at full
	// scale and the measurement is of a different, quieter filter.
	const double amp = 0.001;

	for (int i = 0; i < settle; ++i)
		f.process (static_cast<float> (amp * std::sin (w * i)));

	double maxOut = 0.0;
	for (int i = 0; i < measure; ++i)
	{
		const double out = f.process (static_cast<float> (amp * std::sin (w * (settle + i))));
		maxOut = std::max (maxOut, std::fabs (out));
	}

	return maxOut / amp;
}

/** Render one hit and return the block. */
std::vector<float> renderHit (FilterDrumDsp& dsp, double velocity, int samples)
{
	std::vector<float> left (samples, 0.f), right (samples, 0.f);
	dsp.trigger (velocity);
	dsp.render (left.data (), right.data (), samples);
	return left;
}

/** Drum 1's default patch, exactly as the parameter table defines it.

    THE NUMBERS ARE DUPLICATED FROM FilterDrumParams.cpp, which is
    normally the thing not to do - but this file may not include an SDK
    header and that table does. testDefaultsMatchTable() below is what
    stops the copy rotting. */
void applyDrum1Defaults (DrumVoice& v)
{
	v.setNoiseLevel (1.0);          // 100 %
	v.setCutoff (800.0);
	v.setResonance (0.96);          // 40 % of kMaxResonanceK
	v.setVcfAttack (0.001);         // 1 ms
	v.setVcfRelease (0.120);        // 120 ms
	v.setVcfAmount (3.6);           // 60 % of kMaxEnvOctaves
	v.setVcfVelocity (1.0);
	v.setVcaAttack (0.001);
	v.setVcaRelease (0.150);
	v.setVcaAmount (1.0);
	v.setVcaVelocity (1.0);
}

/** Drum 2's, which are deliberately NOT the same - it is voiced as the
    snap over drum 1's body. */
void applyDrum2Defaults (DrumVoice& v)
{
	v.setNoiseLevel (1.0);
	v.setCutoff (2400.0);
	v.setResonance (0.62 * kMaxResonanceK);
	v.setVcfAttack (0.0005);
	v.setVcfRelease (0.045);
	v.setVcfAmount (0.35 * kMaxEnvOctaves);
	v.setVcfVelocity (1.0);
	v.setVcaAttack (0.0005);
	v.setVcaRelease (0.060);
	v.setVcaAmount (1.0);
	v.setVcaVelocity (1.0);
}

void applyDefaultPatch (FilterDrumDsp& d)
{
	d.setOutputTrimDb (0.0);
	d.setMix (0.5);
	applyDrum1Defaults (d.drum1 ());
	applyDrum2Defaults (d.drum2 ());
}

/** Most of the suite predates the second drum and tests ONE voice. Those
    tests silence drum 2 and put the crossfader hard over, so what they
    measure is drum 1 alone at unity - the same thing they measured
    before the pair existed, so their numbers stay comparable. */
void soloDrum1 (FilterDrumDsp& d)
{
	applyDefaultPatch (d);
	d.setMix (1.0);                 // all drum 1
	d.drum2 ().setVcaAmount (0.0);  // and drum 2 silent even so
}

} // anonymous namespace

//------------------------------------------------------------------------
// 1. The shared conversions
//------------------------------------------------------------------------
static void testConversions ()
{
	std::printf ("dbToGain / gainToDb\n");

	// EXACTLY 1.0, not approximately: everything about the trim being
	// bit-identical rests on this one being exact, which is why
	// dbToGain special-cases zero instead of calling pow.
	check (dbToGain (0.0) == 1.0, "0 dB is exactly unity");

	checkClose (dbToGain (-24.0), 0.063095734, 1e-9, "-24 dB (bottom of range)");
	checkClose (dbToGain (-6.0),  0.501187234, 1e-9, "-6 dB (middle)");
	checkClose (dbToGain (12.0),  3.981071706, 1e-9, "+12 dB (top of range)");

	for (double db : { -24.0, -12.0, -6.0, 0.0, 6.0, 12.0 })
		checkClose (gainToDb (dbToGain (db)), db, 1e-9, "round trip at " + std::to_string (db));

	check (gainToDb (0.0) == -180.0, "zero gain reads -180 dB, not -inf");
}

//------------------------------------------------------------------------
// 2. THE VELOCITY LAW - the thing that was specified most precisely
//------------------------------------------------------------------------
static void testVelocityLaw ()
{
	std::printf ("velocity law\n");

	const double amount = 0.8;

	// FULL SENSITIVITY: the specified behaviour. Velocity 127 (which
	// VST3 delivers as 1.0) gives the full amount; velocity 0 gives
	// none.
	checkClose (velocityScaled (amount, 1.0, 1.0), 0.8, 1e-12, "sens 100%, vel 127 -> full amount");
	checkClose (velocityScaled (amount, 1.0, 0.0), 0.0, 1e-12, "sens 100%, vel 0 -> nothing");
	checkClose (velocityScaled (amount, 1.0, 0.5), 0.4, 1e-12, "sens 100%, half velocity -> half amount");

	// ZERO SENSITIVITY: velocity ignored, amount applies in full. This
	// is the case that makes the control a SENSITIVITY rather than a
	// second amount, and it is the half of the spec that was
	// ambiguous - so it is worth an explicit assertion at both
	// velocity extremes.
	checkClose (velocityScaled (amount, 0.0, 1.0), 0.8, 1e-12, "sens 0%, vel 127 -> full amount");
	checkClose (velocityScaled (amount, 0.0, 0.0), 0.8, 1e-12, "sens 0%, vel 0 -> STILL full amount");

	// Half sensitivity blends the two: at velocity 0 you get half the
	// amount, at 127 you get all of it.
	checkClose (velocityScaled (amount, 0.5, 0.0), 0.4, 1e-12, "sens 50%, vel 0 -> half amount");
	checkClose (velocityScaled (amount, 0.5, 1.0), 0.8, 1e-12, "sens 50%, vel 127 -> full amount");

	// Monotone in velocity at every sensitivity - a velocity curve that
	// ever goes backwards is unplayable.
	for (double s : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		double previous = -1e9;
		bool monotone = true;
		for (int v = 0; v <= 127; ++v)
		{
			const double y = velocityScaled (amount, s, v / 127.0);
			if (y < previous - 1e-12) monotone = false;
			previous = y;
		}
		check (monotone, "monotone in velocity at sensitivity " + std::to_string (s));
	}

	// SIGNED AMOUNTS survive it: the VCF amount is bipolar, and a
	// negative sweep must scale towards zero, not away from it.
	checkClose (velocityScaled (-6.0, 1.0, 0.5), -3.0, 1e-12, "a negative amount scales towards zero");

	// Out-of-range inputs are clamped rather than extrapolated. A host
	// is not supposed to send velocity 1.5, and if it does the answer
	// must not be 1.5x the amount.
	checkClose (velocityScaled (amount, 1.0, 2.0),  0.8, 1e-12, "velocity above 1 is clamped");
	checkClose (velocityScaled (amount, 1.0, -1.0), 0.0, 1e-12, "velocity below 0 is clamped");
	checkClose (velocityScaled (amount, 5.0, 0.0),  0.0, 1e-12, "sensitivity above 1 is clamped");

	//--------------------------------------------------------------------
	// NEGATIVE CONTROL. If velocityScaled ignored its sensitivity
	// argument - the single most likely way to get this wrong - then
	// sens 0 and sens 1 would agree at velocity 0. They must not.
	//--------------------------------------------------------------------
	check (velocityScaled (amount, 0.0, 0.0) != velocityScaled (amount, 1.0, 0.0),
	       "NEGATIVE CONTROL: sensitivity actually changes the result");
}

//------------------------------------------------------------------------
// 3. Envelope times MEAN something, at every sample rate
//------------------------------------------------------------------------
static void testEnvelopeTimes ()
{
	std::printf ("envelope times\n");

	for (double rate : kRates)
	{
		// ATTACK: time to reach 1.0. The definition in
		// AREnvelope::recompute, checked rather than trusted.
		for (double attack : { 0.001, 0.005, 0.050, 0.500 })
		{
			AREnvelope e;
			e.setSampleRate (rate);
			e.setAttack (attack);
			e.setRelease (10.0);        // long, so it cannot interfere
			e.reset ();
			e.trigger ();

			int n = 0;
			const int limit = static_cast<int> (rate * 2.0);
			while (e.stage () == AREnvelope::Stage::Attack && n < limit)
			{
				e.next ();
				++n;
			}

			const double measured = n / rate;
			// 2 % or one sample, whichever is larger - a 1 ms attack at
			// 44.1 k is only 44 samples, so a sample of quantisation is
			// 2 % all by itself.
			const double tol = std::max (attack * 0.02, 1.0 / rate);
			checkClose (measured, attack, tol,
			            "attack reaches 1.0 in " + std::to_string (attack) + " s at " + hz (rate));
			checkClose (e.level (), 1.0, 1e-6, "attack ends at exactly 1.0 at " + hz (rate));
		}

		// RELEASE: time to fall from 1.0 to ZERO, which is what the
		// knob means since the shape controls went in.
		//
		// IT USED TO MEAN "TIME TO -60 dB" and this assertion used to
		// check that, because the old one-pole release could only
		// approach zero. The shaped release lands on it. The curve
		// either side is the SAME curve - testShapedEnvelope measures
		// the two against each other and puts the difference at 0.001
		// everywhere - so nothing about the sound moved; what moved is
		// where the definition is pinned, from a point 60 dB down to
		// the end.
		//
		// The consequence, asserted below so it is on the record: the
		// shaped release passes -60 dB at 0.9 of its knob rather than at
		// 1.0. That is arithmetic, not drift. The old curve reached
		// 0.001 at T; the new one is that curve minus 0.001 and
		// renormalised, so it reaches 0.001 where the old reached
		// 0.001999 - and ln(1/0.001999)/ln(1000) is 0.8998.
		for (double release : { 0.010, 0.120, 1.000 })
		{
			AREnvelope e;
			e.setSampleRate (rate);
			e.setAttack (0.0);          // instant, so the release starts at 1.0
			e.setRelease (release);
			e.reset ();
			e.trigger ();
			e.next ();                  // completes the attack

			check (e.stage () == AREnvelope::Stage::Release,
			       "a zero attack goes straight to release at " + hz (rate));

			int toMinus60 = 0, n = 0;
			const int limit = static_cast<int> (rate * 5.0);
			while (e.stage () == AREnvelope::Stage::Release && n < limit)
			{
				if (e.level () > 0.001f)
					++toMinus60;
				e.next ();
				++n;
			}

			const double tol = std::max (release * 0.03, 2.0 / rate);
			checkClose (n / rate, release, tol,
			            "release reaches ZERO in " + std::to_string (release) + " s at " + hz (rate));
			checkClose (toMinus60 / rate, release * 0.8998, tol,
			            "and passes -60 dB at 0.9 of that - see the note above at "
			              + hz (rate));
		}
	}

	// IT ENDS, EXACTLY, and goes idle. An envelope that decays into
	// denormals forever keeps the voice "active", keeps the silence
	// flags clear and keeps every downstream plug-in awake for the life
	// of the session.
	{
		AREnvelope e;
		e.setSampleRate (48000.0);
		e.setAttack (0.0);
		e.setRelease (0.010);
		e.reset ();
		e.trigger ();
		for (int i = 0; i < 48000; ++i)
			e.next ();

		check (e.idle (), "the envelope goes idle rather than decaying forever");
		check (e.level () == 0.f, "and its level is exactly zero");
	}

	// TRIGGERED, NOT GATED - the deliberate departure from what "AR"
	// usually means. Nothing in the envelope waits for a note-off, so
	// a hit completes on its own.
	{
		AREnvelope e;
		e.setSampleRate (48000.0);
		e.setAttack (0.001);
		e.setRelease (0.020);
		e.reset ();
		e.trigger ();

		bool sawRelease = false;
		for (int i = 0; i < 48000; ++i)
		{
			e.next ();
			if (e.stage () == AREnvelope::Stage::Release) sawRelease = true;
		}
		check (sawRelease, "the release begins without any note-off");
		check (e.idle (), "and the hit completes on its own");
	}

	// RETRIGGER DOES NOT ZERO THE LEVEL. That is what stops a fast roll
	// clicking - the discontinuity, not the loudness, is what you hear.
	{
		AREnvelope e;
		e.setSampleRate (48000.0);
		e.setAttack (0.050);
		e.setRelease (0.200);
		e.reset ();
		e.trigger ();
		for (int i = 0; i < 4000; ++i) e.next ();    // part way up

		const float before = e.level ();
		check (before > 0.1f && before < 1.f, "mid-attack level is in between");

		e.trigger ();
		check (e.level () == before, "a retrigger continues from the current level");
	}
}

//------------------------------------------------------------------------
// 3b. The shape controls, and the envelope trace the panel draws
//
// THE ASSERTION THAT MATTERS MOST IS THE FIRST ONE: at the Exponential
// end, the release is the curve this plug-in had before the shape
// controls existed. The whole reason for choosing the RC family over a
// power law was that the old envelope is a member of it, and "is a
// member of it" is a claim, so there is a copy of the old one-pole in
// this file to measure against.
//------------------------------------------------------------------------

/** The envelope as it was before the shape controls: a one-pole attack
    aimed at 1.2, and a one-pole release calibrated to -60 dB that ran on
    to -100 dB before calling itself idle.

    A COPY, ON PURPOSE. It is here to be the thing the new envelope is
    compared against, and a copy cannot be changed by accident when the
    real one is. If it ever needs updating, the comparison it exists for
    has already failed. */
class OldEnvelope
{
public:
	OldEnvelope (double rate, double attack, double release)
	: mRate (rate)
	{
		mAttackCoeff  = 1.0 - std::exp (-std::log (6.0)    / (attack  * rate));
		mReleaseCoeff = 1.0 - std::exp (-std::log (1000.0) / (release * rate));
	}

	float next ()
	{
		if (mStage == 1)
		{
			mLevel += static_cast<float> ((1.2 - mLevel) * mAttackCoeff);
			if (mLevel >= 1.f) { mLevel = 1.f; mStage = 2; }
		}
		else if (mStage == 2)
		{
			mLevel -= mLevel * static_cast<float> (mReleaseCoeff);
			if (mLevel <= 1e-5f) { mLevel = 0.f; mStage = 0; }
		}
		return mLevel;
	}

	int stage () const { return mStage; }

private:
	double mRate, mAttackCoeff, mReleaseCoeff;
	float mLevel = 0.f;
	int mStage = 1;
};

static void testShapedEnvelope ()
{
	std::printf ("envelope shape controls\n");

	// -- the curve family itself ------------------------------------------
	check (std::fabs (shapedRise (0.0, kMaxCurve)) < 1e-12, "rise(0) is 0");
	check (std::fabs (shapedRise (1.0, kMaxCurve) - 1.0) < 1e-12, "rise(1) is exactly 1");
	check (std::fabs (shapedFall (1.0, kMaxCurve)) < 1e-12, "fall(1) is exactly 0");
	checkClose (shapedRise (0.5, 0.0), 0.5, 1e-12, "curve 0 is a straight line");

	check (shapedRise (0.5, kMaxCurve) > 0.9,
	       "the Exponential end is fast then slow");
	check (shapedRise (0.5, -kMaxCurve) < 0.1,
	       "the Logarithmic end is slow then fast");

	// NEGATIVE CONTROL: the two ends must not be the same picture. A
	// sign dropped in shapeToCurve would pass every endpoint check
	// above and make the knob do nothing either side of centre.
	check (std::fabs (shapedRise (0.5, kMaxCurve) - shapedRise (0.5, -kMaxCurve)) > 0.8,
	       "NEGATIVE CONTROL: the two ends are opposite curves, not the same one");

	check (shapeToCurve (-1.0) > 0.0 && shapeToCurve (1.0) < 0.0,
	       "the control's Exponential end is the curve's positive end");
	checkClose (shapeToCurve (0.0), 0.0, 1e-12, "the control's centre is no curve at all");

	{
		bool monotonic = true, bounded = true;
		for (int c = -8; c <= 8; ++c)
		{
			const double curve = kMaxCurve * c / 8.0;
			double prev = -1.0;
			for (int i = 0; i <= 1000; ++i)
			{
				const double y = shapedRise (i / 1000.0, curve);
				if (y < prev - 1e-12) monotonic = false;
				if (y < -1e-12 || y > 1.0 + 1e-12) bounded = false;
				prev = y;
			}
		}
		check (monotonic, "every shape is monotonic - no shape folds back on itself");
		check (bounded, "every shape stays inside 0..1");
	}

	{
		double worst = 0.0;
		for (int c = -8; c <= 8; ++c)
		{
			const double curve = kMaxCurve * c / 8.0;
			for (int i = 1; i < 1000; ++i)
			{
				const double x = i / 1000.0;
				worst = std::max (worst,
				                  std::fabs (shapedRiseInverse (shapedRise (x, curve), curve) - x));
			}
		}
		check (worst < 1e-9, "shapedRiseInverse undoes shapedRise at every shape");
	}

	// -- THE BIG ONE: the Exponential end is the old plug-in --------------
	for (double rate : kRates)
	{
		for (double release : { 0.045, 0.120, 0.150, 1.000 })
		{
			AREnvelope e;
			e.setSampleRate (rate);
			e.setAttack (0.001);
			e.setRelease (release);
			e.setAttackShape (-1.0);
			e.setReleaseShape (-1.0);
			e.reset ();
			e.trigger ();

			OldEnvelope old (rate, 0.001, release);

			while (e.stage () == AREnvelope::Stage::Attack) e.next ();
			while (old.stage () == 1) old.next ();

			double worst = 0.0;
			long n = 0;
			const long limit = static_cast<long> (rate * 10.0);
			while (e.stage () == AREnvelope::Stage::Release && n < limit)
			{
				worst = std::max (worst,
				                  static_cast<double> (std::fabs (e.next () - old.next ())));
				++n;
			}

			// 0.001 is the whole difference and it is the ENDPOINT
			// OFFSET: the old curve stopped at -60 dB and the new one
			// carries on to zero, so the new is below the old by at
			// most that, everywhere. -60 dB down on a drum hit.
			check (worst <= 0.0011,
			       "the Exponential release IS the old release at " + hz (rate)
			         + ", release " + std::to_string (release));
		}
	}

	// -- times are exact at every shape, which is what killed the tail ----
	for (double rate : kRates)
	{
		for (double shape : { -1.0, -0.5, 0.0, 0.5, 1.0 })
		{
			AREnvelope e;
			e.setSampleRate (rate);
			e.setAttack (0.050);
			e.setRelease (0.200);
			e.setAttackShape (shape);
			e.setReleaseShape (shape);
			e.reset ();
			e.trigger ();

			long a = 0;
			while (e.stage () == AREnvelope::Stage::Attack) { e.next (); ++a; }
			check (e.level () == 1.f, "the attack arrives at EXACTLY 1.0");

			long r = 0;
			while (e.stage () == AREnvelope::Stage::Release) { e.next (); ++r; }
			check (e.level () == 0.f, "the release ends at EXACTLY 0");
			check (e.idle (), "and goes idle rather than ringing on");

			const double tol = 2.0 / rate;
			checkClose (a / rate, 0.050, tol, "the attack takes its time at every shape");
			checkClose (r / rate, 0.200, tol, "the release takes its time at every shape");

			// THIS IS WHAT getTailSamples RELIES ON. The old envelope
			// ran 5/3 of its knob and the tail had to convert; this one
			// does not, and if that changes the tail starts truncating.
			check (r / rate < 0.200 * 1.05,
			       "the release does NOT run past its knob - getTailSamples depends on it");
		}
	}

	// -- THE WHOLE TRAJECTORY, not just its endpoints ---------------------
	//
	// Written because a mutation survived without it. Dropping the
	// normalising divide from the attack leaves the level at
	// 1 - e^-b instead of 1 at the top - and since next() assigns an
	// exact 1.0 when the phase runs out, every endpoint assertion still
	// passed. At the default curve that defect is 0.1 % and inaudible;
	// at a mid-range shape the denominator is 0.63, so the attack would
	// climb to 0.63 and then JUMP to 1.0. A click on every hit, invisible
	// to a test that only looks at the ends.
	for (double shape : { -1.0, -0.6, -0.2, 0.0, 0.2, 0.6, 1.0 })
	{
		const double rate = 48000.0;
		const double attack = 0.100, release = 0.200;

		AREnvelope e;
		e.setSampleRate (rate);
		e.setAttack (attack);
		e.setRelease (release);
		e.setAttackShape (shape);
		e.setReleaseShape (shape);
		e.reset ();
		e.trigger ();

		const double curve = shapeToCurve (shape);
		const long aTotal = static_cast<long> (attack * rate);
		const long rTotal = static_cast<long> (release * rate);

		double worstA = 0.0, worstR = 0.0, biggestStep = 0.0;
		float previous = 0.f;

		long n = 0;
		while (e.stage () == AREnvelope::Stage::Attack && n < aTotal + 8)
		{
			const float v = e.next ();
			++n;
			if (n < aTotal)
				worstA = std::max (worstA,
				    std::fabs (v - shapedRise (static_cast<double> (n) / aTotal, curve)));
			biggestStep = std::max (biggestStep,
			                        static_cast<double> (std::fabs (v - previous)));
			previous = v;
		}

		n = 0;
		while (e.stage () == AREnvelope::Stage::Release && n < rTotal + 8)
		{
			const float v = e.next ();
			++n;
			if (n < rTotal)
				worstR = std::max (worstR,
				    std::fabs (v - shapedFall (static_cast<double> (n) / rTotal, curve)));
			biggestStep = std::max (biggestStep,
			                        static_cast<double> (std::fabs (v - previous)));
			previous = v;
		}

		check (worstA < 1e-5, "the attack follows shapedRise the whole way up");
		check (worstR < 1e-5, "the release follows shapedFall the whole way down");

		// NO STEP ANYWHERE. A 100 ms attack and a 200 ms release at 48 k
		// are 4800 and 9600 samples; the steepest shape moves about
		// 0.005 of full scale in one of them. A discontinuity at a stage
		// boundary would be orders of magnitude larger, and it is a
		// click whatever produced it.
		check (biggestStep < 0.02,
		       "and nothing jumps - no discontinuity at the top or the end");
	}

	// -- the running product must not drift -------------------------------
	{
		const double rate = 48000.0;
		AREnvelope e;
		e.setSampleRate (rate);
		e.setAttack (0.001);
		e.setRelease (4.0);
		e.setReleaseShape (-1.0);
		e.reset ();
		e.trigger ();
		while (e.stage () == AREnvelope::Stage::Attack) e.next ();

		const long total = static_cast<long> (4.0 * rate);
		double worst = 0.0;
		long n = 0;
		while (e.stage () == AREnvelope::Stage::Release && n < total + 8)
		{
			const double v = e.next ();
			++n;
			worst = std::max (worst,
			                  std::fabs (v - shapedFall (static_cast<double> (n) / total, kMaxCurve)));
		}
		check (worst < 1e-6,
		       "the per-sample multiply tracks the closed form over a 4 s release");
	}

	// -- retrigger, at every shape ----------------------------------------
	for (double shape : { -1.0, 0.0, 1.0 })
	{
		AREnvelope e;
		e.setSampleRate (48000.0);
		e.setAttack (0.050);
		e.setRelease (1.0);
		e.setAttackShape (shape);
		e.setReleaseShape (shape);
		e.reset ();
		e.trigger ();
		while (e.stage () == AREnvelope::Stage::Attack) e.next ();
		for (int i = 0; i < 4800; ++i) e.next ();

		const float before = e.level ();
		check (before > 0.1f && before < 1.f, "mid-release level is in between");

		e.trigger ();
		check (std::fabs (e.level () - before) < 1e-6f,
		       "a retrigger continues from the current level at every shape");
		check (e.next () >= before, "and climbs from there rather than dropping");
	}

	// -- the trace the two panel displays are drawn from ------------------
	{
		constexpr int kPoints = 200;
		float vcfCurve[kPoints], vcaCurve[kPoints];

		ArSpec vcf; vcf.attack = 0.001; vcf.release = 0.120; vcf.height = 0.6;
		ArSpec vca; vca.attack = 0.001; vca.release = 0.150; vca.height = 0.8;

		double span = traceDrumEnvelopes (vcf, vca, vcfCurve, vcaCurve, kPoints);
		checkClose (span, vca.attack + vca.release, 1e-9,
		            "the trace span is the longer envelope's length");

		double peakVcf = 0.0, peakVca = 0.0;
		for (int i = 0; i < kPoints; ++i)
		{
			peakVcf = std::max (peakVcf, static_cast<double> (vcfCurve[i]));
			peakVca = std::max (peakVca, static_cast<double> (vcaCurve[i]));
		}
		checkClose (peakVcf, vcf.height, 0.02, "the VCF curve's height is its Amount");
		checkClose (peakVca, vca.height, 0.02, "the VCA curve's height is its Amount");

		// THE SHARED AXIS, which is the whole reason this is one call and
		// not two.
		ArSpec longVcf = vcf; longVcf.release = 2.000;
		span = traceDrumEnvelopes (longVcf, vca, vcfCurve, vcaCurve, kPoints);
		checkClose (span, longVcf.attack + longVcf.release, 1e-9,
		            "the span follows whichever envelope is longer");

		const int mid = kPoints / 2;
		check (vcaCurve[mid] <= 1e-4f, "the short VCA trace is dead by mid-axis");
		check (vcfCurve[mid] > 0.01f, "the long VCF trace is still alive at mid-axis");
		check (vcfCurve[mid] > 10.f * vcaCurve[mid],
		       "NEGATIVE CONTROL: the two traces are NOT on a shared scale by accident");

		// THE DISPLAY FOLLOWS THE SHAPE KNOBS. A trace that ignored them
		// would be a picture of a different envelope from the one
		// playing, which is worse than no picture.
		ArSpec expo = vca; expo.releaseShape = -1.0;
		ArSpec logo = vca; logo.releaseShape = +1.0;
		float a[kPoints], b[kPoints];
		traceDrumEnvelopes (expo, expo, a, vcaCurve, kPoints);
		traceDrumEnvelopes (logo, logo, b, vcaCurve, kPoints);
		// Exponential drops fast, so early in the release it is LOW;
		// Logarithmic hangs, so it is high. The first version of this
		// line had the comparison the wrong way round from its own
		// description, which is the reason to write the description.
		check (b[kPoints / 8] > a[kPoints / 8] + 0.1f,
		       "an Exponential release draws below a Logarithmic one early on");

		ArSpec lin = vca; lin.releaseShape = 0.0;
		traceDrumEnvelopes (lin, lin, a, vcaCurve, kPoints);
		// A linear release passes through half height at half its span.
		// The span here is attack + release with a 1 ms attack, so the
		// midpoint of the trace is very nearly the midpoint of the fall.
		check (std::fabs (a[kPoints / 2] - 0.5f * static_cast<float> (lin.height)) < 0.05f,
		       "a Linear release is drawn as a straight line");

		// A zero amount is a flat floor, not a curve at some other height.
		ArSpec silent = vca; silent.height = 0.0;
		traceDrumEnvelopes (vcf, silent, vcfCurve, vcaCurve, kPoints);
		double peak = 0.0;
		for (int i = 0; i < kPoints; ++i)
			peak = std::max (peak, static_cast<double> (vcaCurve[i]));
		check (peak == 0.0, "NEGATIVE CONTROL: Amount 0 draws a flat floor");

		// RESOLUTION. A 1 ms envelope has to be a curve and not a step.
		ArSpec tiny; tiny.attack = 0.0001; tiny.release = 0.001; tiny.height = 1.0;
		traceDrumEnvelopes (tiny, tiny, vcfCurve, vcaCurve, kPoints);
		int moving = 0;
		for (int i = 1; i < kPoints; ++i)
			if (std::fabs (vcfCurve[i] - vcfCurve[i - 1]) > 1e-4f)
				++moving;
		check (moving > kPoints / 4, "a 1 ms envelope still resolves into a curve");

		traceDrumEnvelopes (vcf, vca, nullptr, vcaCurve, kPoints);
		check (vcaCurve[0] >= 0.f, "one null curve does not stop the other");
		check (traceDrumEnvelopes (vcf, vca, vcfCurve, vcaCurve, 1) == 0.0,
		       "a trace of fewer than two points is refused");
	}
}

//------------------------------------------------------------------------
// 4. The MS-20 filter, against the transfer function it claims
//------------------------------------------------------------------------
static void testFilterResponse ()
{
	std::printf ("MS-20 filter response\n");

	for (double rate : { 44100.0, 48000.0, 96000.0 })
	{
		// DC GAIN IS 1, AND STAYS 1 AS RESONANCE RISES. That is the
		// signature of the MS-20's denominator,
		// s^2/wc^2 + (2-K) s/wc + 1: the resonance moves the DAMPING
		// and leaves the constant term alone, so the cutoff does not
		// shift and the low end does not change level. A filter whose
		// bass drops away as you turn up the peak has the feedback in
		// the wrong place.
		for (double k : { 0.0, 1.0, 1.9 })
			checkClose (filterMagnitude (1000.0, k, 10.0, rate), 1.0, 0.02,
			            "DC gain is 1 at K=" + std::to_string (k) + ", " + hz (rate));

		// AT ZERO RESONANCE IT IS TWO CASCADED ONE-POLES, so the gain
		// at the nominal cutoff is -6 dB, NOT -3 dB.
		//
		// This is worth being explicit about, because -3 dB is what one
		// reaches for and it would be wrong here: the OTA MS-20 is two
		// buffered first-order sections, K=0 leaves them critically
		// damped, and 1/sqrt(2) squared is 0.5.
		checkClose (filterMagnitude (1000.0, 0.0, 1000.0, rate), 0.5, 0.02,
		            "-6 dB at cutoff with no resonance (two cascaded poles), " + hz (rate));

		// 12 dB/OCTAVE ASYMPTOTICALLY.
		//
		// MEASURED WELL BELOW NYQUIST, and that restriction is the
		// point rather than a convenience. This filter is the bilinear
		// image of the analogue one, so it has zeros at z = -1 and its
		// response STEEPENS as it approaches Nyquist - it has to, since
		// it must reach zero there, which no -12 dB/octave line does.
		//
		// The first version of this test measured at 4 k and 8 k with
		// fs = 48 k and got -13.73 dB/octave. That was the filter being
		// correct and the test being wrong: 8 kHz is a third of the way
		// to Nyquist, where the warping is already worth 1.7 dB.
		//
		// 100 Hz cutoff with the measurement at 1 k and 2 k is more
		// than three octaves above cutoff, so the asymptote has taken
		// hold, and below a twelfth of Nyquist at the lowest rate
		// tested, so the warping has not.
		{
			const double a = filterMagnitude (100.0, 0.0, 1000.0, rate);
			const double b = filterMagnitude (100.0, 0.0, 2000.0, rate);
			const double slopeDbPerOctave = 20.0 * std::log10 (b / a);
			checkClose (slopeDbPerOctave, -12.0, 0.6,
			            "12 dB/octave above cutoff at " + hz (rate));
		}

		// RESONANCE PRODUCES A PEAK, and a bigger one as K rises. The
		// theoretical peak is Q = 1/(2-K), so K=1 gives 1.0 and K=1.9
		// gives 10.
		const double q0 = filterMagnitude (1000.0, 0.0, 1000.0, rate);
		const double q1 = filterMagnitude (1000.0, 1.0, 1000.0, rate);
		const double q2 = filterMagnitude (1000.0, 1.9, 1000.0, rate);

		check (q1 > q0 * 1.5, "K=1 peaks above the unresonant response at " + hz (rate));
		check (q2 > q1 * 3.0, "K=1.9 peaks far above K=1 at " + hz (rate));
		checkClose (q2, 10.0, 2.0, "K=1.9 peak is about Q=1/(2-K)=10 at " + hz (rate));
	}
}

//------------------------------------------------------------------------
// 5. Self-oscillation: it happens above K=2, it does NOT below, and it
//    is bounded
//------------------------------------------------------------------------
static void testSelfOscillation ()
{
	std::printf ("self-oscillation\n");

	// The threshold predicate the panel's lamp reads. K=2 is
	// Stinchcombe's k1*k2 >= 2 for the OTA revision.
	check (!selfOscillating (1.99), "the lamp is off just below K=2");
	check (selfOscillating (2.0),   "and on at K=2");
	check (selfOscillating (kMaxResonanceK), "and on at the top of the knob");

	for (double rate : { 44100.0, 48000.0, 96000.0 })
	{
		// Kick it with one impulse, let it run in silence, and see what
		// is left a quarter of a second later.
		auto ring = [rate] (double k) {
			Ms20Filter f;
			f.setSampleRate (rate);
			f.setCutoff (300.0);
			f.setResonance (k);
			f.reset ();

			f.process (1.f);
			for (int i = 0; i < static_cast<int> (rate * 0.25); ++i)
				f.process (0.f);

			double p = 0.0;
			for (int i = 0; i < static_cast<int> (rate * 0.05); ++i)
				p = std::max (p, std::fabs (static_cast<double> (f.process (0.f))));
			return p;
		};

		const double below = ring (1.6);
		const double at    = ring (2.0);
		const double above = ring (kMaxResonanceK);

		//----------------------------------------------------------------
		// THE NEGATIVE CONTROL, and on this test it is the whole point.
		// A filter that oscillated at every setting would pass the two
		// assertions after it. Below the threshold the ring MUST have
		// died away.
		//----------------------------------------------------------------
		check (below < 1e-4, "NEGATIVE CONTROL: at K=1.6 the ring dies away, " + hz (rate));

		check (at > 1e-3,    "at K=2.0 it is still going after 250 ms, " + hz (rate));
		check (above > 0.05, "at the top of the knob it sustains strongly, " + hz (rate));

		// AND IT IS BOUNDED. This is what the diode saturator is for,
		// and what saturating the whole damping term instead of just
		// the feedback would break - the oscillation would grow
		// without limit.
		check (above < 4.0, "the self-oscillation is bounded, not growing, " + hz (rate));
		check (std::isfinite (above), "and finite, " + hz (rate));
	}

	// The Newton solve converges: the residual of the implicit equation
	// is at the limit of double precision, at the worst combination the
	// parameters allow (top of the resonance knob, cutoff at the
	// ceiling).
	{
		Ms20Filter f;
		f.setSampleRate (44100.0);
		f.setCutoff (44100.0 * kMaxCutoffFraction);
		f.setResonance (kMaxResonanceK);
		f.reset ();

		double worst = 0.0;
		for (int i = 0; i < 20000; ++i)
		{
			f.process (static_cast<float> (0.9 * std::sin (i * 0.31)));
			worst = std::max (worst, std::fabs (f.lastResidual ()));
		}
		check (worst < 1e-9, "Newton residual stays below 1e-9 at the worst settings");
	}
}

//------------------------------------------------------------------------
// 6. The envelope actually moves the cutoff, by the stated amount
//------------------------------------------------------------------------
static void testCutoffModulation ()
{
	std::printf ("cutoff modulation\n");

	const double rate = 96000.0;   // high, so 6 octaves up from 800 Hz fits

	// AMOUNT ZERO MEANS NO MODULATION, at either end of the envelope.
	checkClose (cutoffWithEnv (800.0, 0.0, 0.0, rate), 800.0, 1e-9, "amount 0, env 0");
	checkClose (cutoffWithEnv (800.0, 0.0, 1.0, rate), 800.0, 1e-9, "amount 0, env 1");

	// EXPONENTIAL IN THE ENVELOPE: +1 octave doubles, -1 halves, and
	// the maximum is 2^6. Linear-in-Hz modulation would sound like the
	// sweep slows down as it falls, which is not what a filter does.
	checkClose (cutoffWithEnv (800.0, 1.0, 1.0, rate), 1600.0, 1e-6, "+1 octave doubles");
	checkClose (cutoffWithEnv (800.0, -1.0, 1.0, rate), 400.0, 1e-6, "-1 octave halves");
	checkClose (cutoffWithEnv (100.0, kMaxEnvOctaves, 1.0, rate), 6400.0, 1e-6,
	            "full amount is 6 octaves");
	checkClose (cutoffWithEnv (800.0, 3.6, 0.5, rate), 800.0 * std::pow (2.0, 1.8), 1e-6,
	            "half the envelope is half the octaves");

	// CLAMPED AT BOTH ENDS, here rather than at the call sites, so the
	// panel's readout and the filter cannot disagree about what a
	// sweep off the top of the range does.
	check (cutoffWithEnv (10000.0, 6.0, 1.0, 44100.0) <= 44100.0 * kMaxCutoffFraction + 1e-9,
	       "a sweep off the top is clamped to the ceiling");
	check (cutoffWithEnv (25.0, -6.0, 1.0, rate) >= kMinCutoffHz - 1e-9,
	       "a sweep off the bottom is clamped to the floor");

	// NEGATIVE CONTROL: if the amount were being ignored, these two
	// would be equal.
	check (cutoffWithEnv (800.0, 3.6, 1.0, rate) != cutoffWithEnv (800.0, 0.0, 1.0, rate),
	       "NEGATIVE CONTROL: the amount actually changes the cutoff");
}

//------------------------------------------------------------------------
// 7. The whole voice: velocity reaches the audio
//------------------------------------------------------------------------
static void testVoiceVelocity ()
{
	std::printf ("velocity reaches the audio\n");

	const double rate = 48000.0;
	const int block = static_cast<int> (rate * 0.5);

	auto hitPeak = [rate, block] (double velocity, double vcaSens) {
		FilterDrumDsp dsp;
		dsp.setSampleRate (rate);
		dsp.setMaxBlockSize (block);
		soloDrum1 (dsp);
		dsp.drum1 ().setVcaVelocity (vcaSens);
		dsp.reset ();
		return peak (renderHit (dsp, velocity, block));
	};

	// AT FULL SENSITIVITY velocity 0 is SILENT - the specified
	// behaviour taken all the way through to the output.
	check (hitPeak (0.0, 1.0) == 0.0, "sens 100%, velocity 0 renders exact silence");

	const double full = hitPeak (1.0, 1.0);
	const double half = hitPeak (0.5, 1.0);
	check (full > 0.0, "velocity 127 makes a sound");
	check (half > 0.0, "velocity 64 makes a sound");

	// Half velocity is about half the level. Not exactly, because the
	// noise is not the same noise - see the note in
	// FilterDrumDsp::trigger about why hits are deliberately not
	// identical - so this is a loose bound on a peak, not an equality.
	check (half < full * 0.75, "half velocity is quieter than full");
	check (half > full * 0.25, "but not silent");

	// AT ZERO SENSITIVITY velocity 0 is NOT silent, which is the whole
	// difference between a sensitivity and a second amount.
	check (hitPeak (0.0, 0.0) > 0.0, "sens 0%, velocity 0 still makes a sound");

	// A ZERO VCA AMOUNT IS SILENT however hard it is played.
	{
		FilterDrumDsp dsp;
		dsp.setSampleRate (rate);
		dsp.setMaxBlockSize (block);
		soloDrum1 (dsp);
		dsp.drum1 ().setVcaAmount (0.0);
		dsp.reset ();
		check (peak (renderHit (dsp, 1.0, block)) == 0.0, "VCA amount 0 is silent at full velocity");
	}

	// VELOCITY ALSO REACHES THE FILTER, separately from the level -
	// that is what the second sensitivity control is for. With the VCA
	// taken out of the comparison (sensitivity 0, so the level is the
	// same either way), two velocities must still give different
	// sounds, because the cutoff sweep differs.
	{
		auto render = [rate, block] (double velocity) {
			FilterDrumDsp dsp;
			dsp.setSampleRate (rate);
			dsp.setMaxBlockSize (block);
			soloDrum1 (dsp);
			dsp.drum1 ().setVcaVelocity (0.0);      // level independent of velocity
			dsp.drum1 ().setVcfVelocity (1.0);      // cutoff still velocity-scaled
			dsp.drum1 ().setVcfAmount (6.0);        // and a big sweep, so it is obvious
			dsp.reset ();
			return renderHit (dsp, velocity, block);
		};

		const double soft = peak (render (0.1));
		const double hard = peak (render (1.0));
		check (soft != hard, "VCF velocity changes the sound with the VCA held constant");
	}
}

//------------------------------------------------------------------------
// 7a. Noise Level, and the trap at its bottom end
//------------------------------------------------------------------------
static void testNoiseLevel ()
{
	std::printf ("noise level\n");

	const double rate = 48000.0;
	const int block = static_cast<int> (rate * 0.5);
	const int tailFrom = static_cast<int> (rate * 0.1);

	/** RMS of the part of the hit after 100 ms, which is where the
	    filter's own ringing has settled and what is left is what the
	    noise is still putting in. A peak measurement would be dominated
	    by the attack and would not separate the two. */
	auto tailRms = [tailFrom] (const std::vector<float>& b) {
		double sum = 0.0;
		int n = 0;
		for (size_t i = tailFrom; i < b.size (); ++i) { sum += b[i] * b[i]; ++n; }
		return (n > 0) ? std::sqrt (sum / n) : 0.0;
	};

	auto hit = [rate, block] (double noise, double k, double release) {
		FilterDrumDsp dsp;
		dsp.setSampleRate (rate);
		dsp.setMaxBlockSize (block);
		soloDrum1 (dsp);
		dsp.drum1 ().setNoiseLevel (noise);
		dsp.drum1 ().setResonance (k);
		dsp.drum1 ().setVcaRelease (release);
		dsp.reset ();
		return renderHit (dsp, 1.0, block);
	};

	// IT SCALES THE NOISE, monotonically.
	const double full = tailRms (hit (1.0,  0.96, 0.400));
	const double half = tailRms (hit (0.5,  0.96, 0.400));
	const double none = tailRms (hit (0.0,  0.96, 0.400));

	check (full > 0.0, "100 % noise sustains");
	check (half < full * 0.75, "50 % is quieter");
	check (half > full * 0.25, "but not silent");

	// About half, since the knob is a linear gain on the source and
	// the filter is linear at this resonance.
	checkClose (half / full, 0.5, 0.06, "50 % really is about half the amplitude");

	//--------------------------------------------------------------------
	// NEGATIVE CONTROL. At 0 % the SUSTAINED noise must be gone
	// entirely - if the knob were being ignored, this would match the
	// full reading.
	//--------------------------------------------------------------------
	check (none < full * 0.001, "NEGATIVE CONTROL: at 0 % the sustained noise is gone");

	//--------------------------------------------------------------------
	// AND WHAT THAT COSTS, NOW THAT THE NOISE IS THE ONLY EXCITATION.
	//
	// A linear filter fed exact zero from a zero state outputs exact
	// zero forever, however far past self-oscillation it is set. So
	// Noise Level 0 is SILENCE from a cold start, at every resonance -
	// it is an off switch, not a pure-tone setting, and the panel says
	// "silent" there rather than "0 %".
	//
	// These assertions are deliberately the opposite of the ones a
	// trigger ping used to satisfy. If a per-note excitation is ever
	// put back, they are the ones that should fail first and tell you
	// so.
	//--------------------------------------------------------------------
	check (peak (hit (0.0, 0.96, 0.150)) == 0.0,
	       "0 % noise is exact silence at the default resonance");
	check (peak (hit (0.0, kMaxResonanceK, 1.000)) == 0.0,
	       "0 % noise is exact silence even at full resonance");

	//--------------------------------------------------------------------
	// AND THE WRINKLE THAT MAKES THAT SILENCE INTERMITTENT.
	//
	// renderVoices does not advance the filter while the voice is idle,
	// so its state FREEZES between hits rather than decaying. Once an
	// oscillation has been started by noise, turning the knob to 0
	// leaves it running for the rest of the session - and the voice
	// only falls silent when the project is reloaded with the knob
	// already down.
	//
	// That is worth an executable assertion precisely because it is the
	// confusing case: "it worked until I reloaded" is a bug report
	// nobody can act on, and this is the line that explains it.
	//--------------------------------------------------------------------
	{
		FilterDrumDsp dsp;
		dsp.setSampleRate (rate);
		dsp.setMaxBlockSize (block);
		soloDrum1 (dsp);
		dsp.drum1 ().setResonance (kMaxResonanceK);
		dsp.drum1 ().setVcaRelease (0.150);
		dsp.reset ();

		std::vector<float> l (block, 0.f), r (block, 0.f);
		dsp.trigger (1.0);
		dsp.render (l.data (), r.data (), block);
		check (peak (l) > 0.0, "a hit at 100 % noise starts the oscillation");

		dsp.drum1 ().setNoiseLevel (0.0);
		std::fill (l.begin (), l.end (), 0.f);
		std::fill (r.begin (), r.end (), 0.f);
		dsp.trigger (1.0);
		dsp.render (l.data (), r.data (), block);
		check (peak (l) > 0.0,
		       "and with the knob then at 0 the self-oscillation keeps sounding");

		// The same settings from cold are silent - which is the whole
		// point. Same knobs, different history, different answer.
		FilterDrumDsp cold;
		cold.setSampleRate (rate);
		cold.setMaxBlockSize (block);
		soloDrum1 (cold);
		cold.drum1 ().setResonance (kMaxResonanceK);
		cold.drum1 ().setNoiseLevel (0.0);
		cold.reset ();
		check (peak (renderHit (cold, 1.0, block)) == 0.0,
		       "NEGATIVE CONTROL: the identical patch from cold is silent");
	}

	//--------------------------------------------------------------------
	// HOW LITTLE NOISE IS ENOUGH to get the oscillation going from cold.
	// Measured at 0.5 %, which is what makes a floor on the knob the
	// cheap alternative to a per-note excitation if the silence at 0
	// ever matters.
	//--------------------------------------------------------------------
	check (peak (hit (0.005, kMaxResonanceK, 1.000)) > 0.0,
	       "0.5 % noise is enough to start it from cold at full resonance");

}

//------------------------------------------------------------------------
// 8. The trim stage is still transparent
//------------------------------------------------------------------------
static void testUnityIsBitIdentical ()
{
	std::printf ("output trim passes the signal through untouched\n");

	const int kBlock = 512;

	std::vector<float> reference (kBlock);
	for (int i = 0; i < kBlock; ++i)
		reference[i] = static_cast<float> (std::sin (i * 0.037) * 0.9 + ((i % 7) - 3) * 1e-6);
	reference[0] =  1.f;
	reference[1] = -1.f;
	reference[2] =  1e-7f;

	for (double rate : kRates)
	{
		FilterDrumDsp dsp;
		dsp.setSampleRate (rate);
		dsp.setMaxBlockSize (kBlock);
		dsp.setOutputTrimDb (0.0);
		dsp.reset ();

		std::vector<float> left (reference), right (reference);
		dsp.applyTrim (left.data (), right.data (), kBlock);

		check (std::memcmp (left.data (), reference.data (), kBlock * sizeof (float)) == 0 &&
		       std::memcmp (right.data (), reference.data (), kBlock * sizeof (float)) == 0,
		       "bit-identical at 0 dB, " + hz (rate));
	}

	// NEGATIVE CONTROL: the same comparison at half gain must FAIL to
	// be identical. If memcmp were being handed the same pointer twice,
	// or applyTrim were a no-op, everything above would pass and mean
	// nothing.
	{
		FilterDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setMaxBlockSize (kBlock);
		dsp.setOutputTrimDb (gainToDb (0.5));
		dsp.reset ();

		std::vector<float> left (reference), right (reference);
		dsp.applyTrim (left.data (), right.data (), kBlock);

		check (std::memcmp (left.data (), reference.data (), kBlock * sizeof (float)) != 0,
		       "NEGATIVE CONTROL: half gain is NOT bit-identical");
		checkClose (left[0], 0.5, 1e-6, "half gain really halves full scale");
	}
}

//------------------------------------------------------------------------
// 9. Degenerate blocks, absurd settings, and the state guard
//------------------------------------------------------------------------
static void testRobustness ()
{
	std::printf ("degenerate blocks and absurd settings\n");

	FilterDrumDsp dsp;
	dsp.setSampleRate (48000.0);
	dsp.setMaxBlockSize (512);
	soloDrum1 (dsp);

	std::vector<float> left (512, 0.f), right (512, 0.f);

	// A PARAMETER-ONLY BLOCK is legal and arrives in practice: hosts
	// send numSamples == 0 to deliver automation between audible
	// blocks.
	dsp.render (left.data (), right.data (), 0);
	dsp.render (left.data (), right.data (), -1);
	dsp.applyTrim (left.data (), right.data (), 0);
	dsp.render (nullptr, right.data (), 512);
	dsp.render (left.data (), nullptr, 512);
	dsp.render (nullptr, nullptr, 512);
	check (true, "survived zero, negative and null-buffer blocks");

	dsp.setSampleRate (0.0);
	check (dsp.sampleRate () >= 1000.0, "an absurd sample rate falls back to something sane");

	dsp.setMaxBlockSize (0);
	dsp.render (left.data (), right.data (), 64);
	check (true, "a zero max block size does not crash render");

	//--------------------------------------------------------------------
	// THE FULL-EXTREMES SWEEP. Every parameter at each end of its range,
	// at every sample rate, driven for a quarter of a second, and
	// nothing may come out non-finite.
	//
	// This is the test that earns the guard() in FilterDrumDsp.cpp. A
	// single inf anywhere in a recursive state poisons every subsequent
	// sample for the life of the plug-in instance, silently - and the
	// combination that produces one is never the combination anyone
	// thought to try by hand.
	//--------------------------------------------------------------------
	int combinations = 0;
	bool allGood = true;

	for (double rate : kRates)
	{
		const int block = static_cast<int> (rate * 0.25);

		for (double noise : { 0.0, 1.0 })
		for (double cutoff : { kMinCutoffHz, 800.0, 20000.0 })
		for (double k : { 0.0, 2.0, kMaxResonanceK })
		for (double amount : { -kMaxEnvOctaves, 0.0, kMaxEnvOctaves })
		for (double attack : { 0.0001, 1.0 })
		for (double trim : { -24.0, 12.0 })
		{
			FilterDrumDsp d;
			d.setSampleRate (rate);
			d.setMaxBlockSize (block);
			d.setOutputTrimDb (trim);
			d.drum1 ().setNoiseLevel (noise);
			d.drum1 ().setCutoff (cutoff);
			d.drum1 ().setResonance (k);
			d.drum1 ().setVcfAttack (attack);
			d.drum1 ().setVcfRelease (0.120);
			d.drum1 ().setVcfAmount (amount);
			d.drum1 ().setVcfVelocity (1.0);
			d.drum1 ().setVcaAttack (attack);
			d.drum1 ().setVcaRelease (0.150);
			d.drum1 ().setVcaAmount (1.0);
			d.drum1 ().setVcaVelocity (1.0);
			d.reset ();

			const std::vector<float> out = renderHit (d, 1.0, block);
			++combinations;

			if (!allFinite (out) || peak (out) > 64.0)
				allGood = false;
		}
	}

	check (allGood, "all " + std::to_string (combinations) +
	       " extreme parameter combinations stay finite and bounded");

	// AND THE GUARD IS A NO-OP AT NORMAL SETTINGS, which is what
	// PORTING-GUIDE.md section 5 asks to be proved rather than
	// assumed: a default render must never come near the ceiling, so
	// the clamp cannot be changing the sound.
	{
		FilterDrumDsp d;
		d.setSampleRate (48000.0);
		d.setMaxBlockSize (24000);
		soloDrum1 (d);
		d.reset ();
		check (peak (renderHit (d, 1.0, 24000)) < kStateCeiling * 0.1,
		       "the default patch stays an order of magnitude below the state ceiling");
	}
}

//------------------------------------------------------------------------
// 10. Voice lifecycle - what the silence flags and the tail depend on
//------------------------------------------------------------------------
static void testVoiceLifecycle ()
{
	std::printf ("voice lifecycle\n");

	const double rate = 48000.0;

	FilterDrumDsp dsp;
	dsp.setSampleRate (rate);
	dsp.setMaxBlockSize (4800);
	soloDrum1 (dsp);
	dsp.drum1 ().setVcaRelease (0.050);
	dsp.reset ();

	check (!dsp.active (), "a voice that has never been played is inactive");

	std::vector<float> l (4800, 0.f), r (4800, 0.f);
	dsp.trigger (1.0);
	check (dsp.active (), "and active the moment it is triggered");

	// DRUM 1'S OWN LIFECYCLE, not the pair's. FilterDrumDsp::active() is
	// the OR of the two, and drum 2 is still running its own envelope
	// here even with its level at zero - so asking the pair would be
	// asking a different question from the one this test means.
	dsp.render (l.data (), r.data (), 4800);     // 100 ms, twice the release
	check (!dsp.drum1 ().active (), "and inactive again once the VCA has closed");

	// THE VCA ALONE DECIDES. A long VCF release with a short VCA one
	// must not keep the voice alive - nothing that happens to the
	// cutoff of a muted signal is audible, and keeping it active would
	// hold the silence flags clear for no reason.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (4800);
		soloDrum1 (d);
		d.drum1 ().setVcaRelease (0.010);
		d.drum1 ().setVcfRelease (4.000);
		d.reset ();
		d.trigger (1.0);
		d.render (l.data (), r.data (), 4800);
		check (!d.drum1 ().active (), "a long VCF release does not keep the voice alive");
	}

	// THE PAIR'S active() IS THE OR OF THE TWO, which is what the
	// processor's silence flag has to be: a bus flagged silent while
	// either drum is still sounding may be skipped by the host, and the
	// tail of that drum simply never arrives.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (4800);
		applyDefaultPatch (d);
		d.drum1 ().setVcaRelease (0.010);   // drum 1 closes almost at once
		d.drum2 ().setVcaRelease (1.000);   // drum 2 rings on
		d.reset ();
		d.trigger (1.0);
		d.render (l.data (), r.data (), 4800);

		check (!d.drum1 ().active (), "drum 1 has closed");
		check (d.drum2 ().active (),  "drum 2 has not");
		check (d.active (), "so the pair is still active - the OR, not the AND");
	}

	// AN IDLE VOICE RENDERS EXACT SILENCE, which is what makes the
	// silence flag honest.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (4800);
		soloDrum1 (d);
		d.reset ();
		std::vector<float> a (4800, 0.f), b (4800, 0.f);
		d.render (a.data (), b.data (), 4800);
		check (peak (a) == 0.0 && peak (b) == 0.0, "an untriggered voice renders exact silence");
	}

	// THE VOICE IS MONO: both channels identical, not two noise
	// sources. A stereo drum from decorrelated noise sounds wide and
	// weak, which is the opposite of what a kick wants.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (4800);
		soloDrum1 (d);
		d.reset ();
		std::vector<float> a (4800, 0.f), b (4800, 0.f);
		d.trigger (1.0);
		d.render (a.data (), b.data (), 4800);
		check (std::memcmp (a.data (), b.data (), 4800 * sizeof (float)) == 0,
		       "left and right are bit-identical - one mono voice");
		check (peak (a) > 0.0, "and it is not silence that makes them equal");
	}
}

//------------------------------------------------------------------------
// 11. The noise source
//------------------------------------------------------------------------
static void testNoise ()
{
	std::printf ("noise\n");

	Noise n;
	double sum = 0.0, sumsq = 0.0;
	double lo = 1e9, hi = -1e9;
	const int N = 200000;

	for (int i = 0; i < N; ++i)
	{
		const double v = n.next ();
		sum += v;
		sumsq += v * v;
		lo = std::min (lo, v);
		hi = std::max (hi, v);
	}

	const double mean = sum / N;
	const double rms  = std::sqrt (sumsq / N);

	// A DC OFFSET IN THE NOISE IS A CLICK on every hit, because the VCA
	// multiplies it by an envelope that starts and ends at zero.
	checkClose (mean, 0.0, 0.01, "zero mean - a DC offset would click");

	// Uniform on -1..1 has RMS 1/sqrt(3).
	checkClose (rms, 1.0 / std::sqrt (3.0), 0.01, "RMS matches a uniform -1..1 distribution");

	check (lo < -0.99 && hi > 0.99, "it uses the whole range");
	check (lo >= -1.0 && hi <= 1.0, "and does not exceed it");
}

//------------------------------------------------------------------------
// 12. The default patch's output level
//------------------------------------------------------------------------
static void measureDefaultLevel ()
{
	std::printf ("default patch level\n");

	const double rate = 48000.0;
	const int block = static_cast<int> (rate * 1.0);

	FilterDrumDsp dsp;
	dsp.setSampleRate (rate);
	dsp.setMaxBlockSize (block);
	soloDrum1 (dsp);
	dsp.reset ();

	std::vector<float> out = renderHit (dsp, 1.0, block);
	const double p = peakDbFS (out);
	std::printf ("  default patch, velocity 127:      %+.2f dBFS\n", p);

	// A DEFAULT PATCH THAT CLIPS MASKS EVERY OTHER FAULT in the signal
	// path and sends you chasing the wrong bug for a day. This is the
	// measurement the blank scaffold wrote down as -inf; it is a real
	// number now, and the window is narrow on purpose - it is what
	// holds kVoiceGain in place.
	check (p < -3.0, "the default patch leaves at least 3 dB of headroom");
	check (p > -18.0, "and is not so quiet that nobody will hear it");

	// THE LOUDEST THING THE PLUG-IN CAN DO, which is the self-
	// oscillating filter at the top of the resonance knob. Worth
	// measuring separately, because the diode knee is what sets it and
	// a change there moves this number without touching the default.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (block);
		soloDrum1 (d);
		d.drum1 ().setResonance (kMaxResonanceK);
		d.drum1 ().setVcaRelease (1.000);
		d.reset ();
		const double pk = peakDbFS (renderHit (d, 1.0, block));
		std::printf ("  full resonance, self-oscillating: %+.2f dBFS\n", pk);

		// THE LOUDEST THING THE PLUG-IN CAN DO AT UNITY TRIM, and it
		// must not clip either: a user who turns the resonance up has
		// not asked for distortion, and the diode squelch is supposed
		// to be the only nonlinearity they hear.
		check (pk < 0.0, "full resonance does not clip at unity trim");
		check (pk > -12.0, "and is louder than the default patch, as it should be");
	}

	// AND WITH THE TRIM AT ITS TOP, because +12 dB on top of the above
	// is the actual worst case a user can dial in.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (block);
		soloDrum1 (d);
		d.drum1 ().setResonance (kMaxResonanceK);
		d.setOutputTrimDb (12.0);
		d.reset ();
		const double pk = peakDbFS (renderHit (d, 1.0, block));
		std::printf ("  worst case, +12 dB trim:          %+.2f dBFS\n", pk);
		check (std::isfinite (pk), "the worst case is still a finite number");
	}

	// THE FASTEST VCA ATTACK, which used to be the worst case because a
	// per-note trigger ping peaked against an envelope that was still
	// opening. The ping is gone and this is no longer a hazard - it is
	// kept as a measurement because it is the setting somebody reaches
	// for when they want a click-y kick, and because a future
	// excitation would show up here first.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (block);
		soloDrum1 (d);
		d.drum1 ().setVcaAttack (0.0001);      // the knob's minimum
		d.reset ();
		const double pk = peakDbFS (renderHit (d, 1.0, block));
		std::printf ("  fastest VCA attack:               %+.2f dBFS\n", pk);
		check (pk < 0.0, "the fastest attack does not clip");
	}

	// NOISE LEVEL AT 0 IS SILENCE, printed alongside the others so the
	// cost of removing the trigger ping is visible in the same place
	// the levels are read.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (block);
		soloDrum1 (d);
		d.drum1 ().setNoiseLevel (0.0);
		d.reset ();
		const double pk = peakDbFS (renderHit (d, 1.0, block));
		std::printf ("  noise level 0, from cold:         %s\n",
		             std::isfinite (pk) ? "NOT SILENT - unexpected" : "silence");
		check (!std::isfinite (pk), "noise level 0 renders exact silence");
	}

	// render() must CLEAR the block it is given, not add to whatever
	// was left in it - a host reuses its buffers, so an instrument that
	// forgets this plays back the last block forever.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (1024);
		soloDrum1 (d);
		d.reset ();
		std::vector<float> dirtyL (1024, 0.5f), dirtyR (1024, -0.5f);
		d.render (dirtyL.data (), dirtyR.data (), 1024);
		check (peak (dirtyL) == 0.0 && peak (dirtyR) == 0.0,
		       "render clears the host's buffer rather than adding to it");
	}
}

//------------------------------------------------------------------------
// 12a. The pair: two drums, one note, one crossfader
//------------------------------------------------------------------------
static void testTheTwoDrums ()
{
	std::printf ("two drums\n");

	const double rate = 48000.0;
	const int n = static_cast<int> (rate * 0.4);

	auto render = [rate, n] (FilterDrumDsp& d) {
		std::vector<float> l (n, 0.f), r (n, 0.f);
		d.trigger (1.0);
		d.render (l.data (), r.data (), n);
		return l;
	};

	//--------------------------------------------------------------------
	// ONE NOTE STRIKES BOTH. With the fader hard to one end you hear
	// exactly one drum, and the OTHER end is a different sound - which
	// is only true if both were triggered.
	//--------------------------------------------------------------------
	std::vector<float> only1, only2;
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (n);
		applyDefaultPatch (d);
		d.setMix (1.0);
		d.reset ();
		only1 = render (d);
	}
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (n);
		applyDefaultPatch (d);
		d.setMix (0.0);
		d.reset ();
		only2 = render (d);
	}

	check (peak (only1) > 0.0, "drum 1 sounds with the fader at the top");
	check (peak (only2) > 0.0, "drum 2 sounds with the fader at the bottom - from the same note");

	// AND THEY ARE DIFFERENT SOUNDS. If the second drum were a copy of
	// the first - same seed, same settings - these two blocks would be
	// identical and the crossfader would do nothing audible.
	check (std::memcmp (only1.data (), only2.data (), n * sizeof (float)) != 0,
	       "NEGATIVE CONTROL: the two drums are not the same signal");

	//--------------------------------------------------------------------
	// THE NOISE IS DECORRELATED. Two generators started from one seed
	// produce the identical sequence, and summing identical signals is
	// not a layer - it is one signal 6 dB louder, which would measure
	// fine everywhere and sound like one drum.
	//
	// Checked with the two voices set IDENTICALLY, so the only thing
	// that can differ is the seed.
	//--------------------------------------------------------------------
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (n);
		applyDefaultPatch (d);
		applyDrum1Defaults (d.drum2 ());    // make drum 2 identical to drum 1
		d.setMix (1.0);
		d.reset ();
		const std::vector<float> a1 = render (d);

		FilterDrumDsp e;
		e.setSampleRate (rate);
		e.setMaxBlockSize (n);
		applyDefaultPatch (e);
		applyDrum1Defaults (e.drum2 ());
		e.setMix (0.0);
		e.reset ();
		const std::vector<float> b1 = render (e);

		check (std::memcmp (a1.data (), b1.data (), n * sizeof (float)) != 0,
		       "identically-set drums still differ - the noise seeds are not shared");

		// The correlation coefficient of the two, which is the direct
		// statement of the thing that matters: near 1 would mean the
		// sum is just a louder copy.
		double sa = 0.0, sb = 0.0, sab = 0.0;
		for (int i = 0; i < n; ++i)
		{
			sa  += static_cast<double> (a1[i]) * a1[i];
			sb  += static_cast<double> (b1[i]) * b1[i];
			sab += static_cast<double> (a1[i]) * b1[i];
		}
		const double corr = sab / (std::sqrt (sa * sb) + 1e-30);
		std::printf ("  correlation of two identically-set drums: %+.4f\n", corr);
		check (std::fabs (corr) < 0.2, "and they are substantially uncorrelated");
	}

	//--------------------------------------------------------------------
	// THE CROSSFADE LAW, at both ends and the centre.
	//--------------------------------------------------------------------
	checkClose (crossfadeGainDrum1 (1.0), 1.0, 1e-12, "mix 1 is all drum 1");
	checkClose (crossfadeGainDrum2 (1.0), 0.0, 1e-12, "and none of drum 2");
	checkClose (crossfadeGainDrum1 (0.0), 0.0, 1e-12, "mix 0 is none of drum 1");
	checkClose (crossfadeGainDrum2 (0.0), 1.0, 1e-12, "and all of drum 2");

	const double c = std::sqrt (0.5);
	checkClose (crossfadeGainDrum1 (0.5), c, 1e-12, "the centre is -3 dB on drum 1");
	checkClose (crossfadeGainDrum2 (0.5), c, 1e-12, "and -3 dB on drum 2");

	// CONSTANT POWER at every position. The two drums are uncorrelated,
	// so their powers add; a linear crossfade would dip about 3 dB in
	// the middle, which is the hole everyone has heard on a cheap mixer.
	for (int i = 0; i <= 100; ++i)
	{
		const double m = i / 100.0;
		const double g1 = crossfadeGainDrum1 (m);
		const double g2 = crossfadeGainDrum2 (m);
		if (std::fabs (g1 * g1 + g2 * g2 - 1.0) > 1e-9)
		{
			check (false, "constant power at mix " + std::to_string (m));
			break;
		}
	}
	check (true, "gainA^2 + gainB^2 == 1 at every one of 101 positions");

	// NEGATIVE CONTROL for that: a LINEAR crossfade would fail it, and
	// by a margin big enough to hear.
	{
		const double lin1 = 0.5, lin2 = 0.5;
		check (std::fabs (lin1 * lin1 + lin2 * lin2 - 1.0) > 0.4,
		       "NEGATIVE CONTROL: a linear fade would be 3 dB down at the centre");
	}

	// Out of range is clamped rather than extrapolated.
	checkClose (crossfadeGainDrum1 (2.0), 1.0, 1e-12, "mix above 1 is clamped");
	checkClose (crossfadeGainDrum1 (-1.0), 0.0, 1e-12, "mix below 0 is clamped");

	//--------------------------------------------------------------------
	// THE FADER IS SMOOTHED. A stepped cutoff turned out to be
	// inaudible, but a stepped GAIN is a step in the waveform, which is
	// a click - so the crossfader gets a smoother where the cutoff does
	// not need one.
	//--------------------------------------------------------------------
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (n);
		applyDefaultPatch (d);
		d.setMix (1.0);
		d.reset ();
		checkClose (d.currentGainDrum1 (), 1.0, 1e-6, "reset snaps the crossfade gains");

		// Move it hard over; the gain must NOT arrive in one sample.
		d.setMix (0.0);
		std::vector<float> l (64, 0.f), r (64, 0.f);
		d.trigger (1.0);
		d.render (l.data (), r.data (), 1);
		check (d.currentGainDrum1 () > 0.9,
		       "and a hard move does not arrive in a single sample");

		// 20 ms of glide, which is kMixSmoothingSeconds - by then a
		// one-pole is within 1/e of its target. 64 samples is 1.4 ms and
		// the first version of this assertion asked for 0.9 there, which
		// the smoother has no business reaching that fast.
		for (int b = 0; b < 16; ++b)
			d.render (l.data (), r.data (), 64);
		check (d.currentGainDrum1 () < 0.5, "but it is most of the way after 20 ms");
	}

	//--------------------------------------------------------------------
	// BOTH DRUMS AT ONCE IS LOUDER THAN EITHER ALONE, but not 6 dB
	// louder - which is the audible difference between a layer and a
	// doubled copy. Uncorrelated sources at -3 dB each sum to about the
	// same total power as one at unity.
	//--------------------------------------------------------------------
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (n);
		applyDefaultPatch (d);
		d.setMix (0.5);
		d.reset ();
		const std::vector<float> both = render (d);

		auto rms = [n] (const std::vector<float>& b) {
			double s = 0.0;
			for (float v : b) s += static_cast<double> (v) * v;
			return std::sqrt (s / n);
		};

		const double rBoth = rms (both), r1 = rms (only1), r2 = rms (only2);
		std::printf ("  RMS: drum 1 %+.2f dB, drum 2 %+.2f dB, blended %+.2f dB\n",
		             20.0 * std::log10 (r1), 20.0 * std::log10 (r2),
		             20.0 * std::log10 (rBoth));

		check (rBoth > 0.0, "the blend makes a sound");
		check (20.0 * std::log10 (rBoth / std::max (r1, r2)) < 4.0,
		       "the blend is not 6 dB of doubled copy");
	}
}

//------------------------------------------------------------------------
// 13. The constants the parameter table depends on
//------------------------------------------------------------------------
static void testDefaultsMatchTable ()
{
	std::printf ("constants the parameter table depends on\n");

	// applyDefaultPatch() above duplicates numbers from
	// FilterDrumParams.cpp, because this file may not include an SDK
	// header and that table does. These assertions are what stop the
	// copy rotting: each one ties a number in the table to the constant
	// in FilterDrumDsp.h that gives it meaning.
	checkClose (kMaxResonanceK, 2.4, 1e-12, "kMaxResonanceK is the table's resonance internalMax");
	checkClose (kMaxEnvOctaves, 6.0, 1e-12, "kMaxEnvOctaves is the table's VCF amount internalMax");
	checkClose (kMinCutoffHz, 20.0, 1e-12, "kMinCutoffHz is the table's cutoff plainMin");

	// 40 % of the resonance knob is the default, and it must be BELOW
	// the self-oscillation threshold: a plug-in that pings before you
	// touch anything is one nobody can hear the noise through.
	check (!selfOscillating (0.40 * kMaxResonanceK), "the default resonance does not self-oscillate");

	// And the top of the knob must be above it, or the lamp and the
	// ping are both unreachable.
	check (selfOscillating (kMaxResonanceK), "the top of the resonance knob does");

	// 60 % of the VCF amount knob, which is the default.
	checkClose (0.60 * kMaxEnvOctaves, 3.6, 1e-12, "the default VCF amount is 3.6 octaves");

	// Drum 2's defaults, which are deliberately NOT drum 1's - two drums
	// with identical settings are one drum 6 dB louder, and an
	// out-of-the-box patch where the pair does nothing would look broken.
	checkClose (0.62 * kMaxResonanceK, 1.488, 1e-9, "drum 2's resonance default");
	checkClose (0.35 * kMaxEnvOctaves, 2.1, 1e-9, "drum 2's VCF amount default");
	check (!selfOscillating (0.62 * kMaxResonanceK), "and it does not self-oscillate either");
}

//------------------------------------------------------------------------
// 14. The real default patch - BOTH drums, as a user first hears it
//------------------------------------------------------------------------
static void measurePairDefault ()
{
	std::printf ("the pair's default patch\n");

	const double rate = 48000.0;
	const int n = static_cast<int> (rate * 1.0);

	FilterDrumDsp d;
	d.setSampleRate (rate);
	d.setMaxBlockSize (n);
	applyDefaultPatch (d);       // both drums, mix at 50 %
	d.reset ();

	std::vector<float> l (n, 0.f), r (n, 0.f);
	d.trigger (1.0);
	d.render (l.data (), r.data (), n);

	const double p = peakDbFS (l);
	std::printf ("  both drums, mix 50 %%, velocity 127:  %+.2f dBFS\n", p);

	// THIS IS THE NUMBER A USER MEETS, and it is the one that has to
	// stay under 0. The single-drum measurement above is a component
	// test; this is the instrument.
	check (p < -3.0, "the pair's default patch leaves at least 3 dB of headroom");
	check (p > -18.0, "and is not so quiet that nobody will hear it");

	// The worst case a user can dial in: both drums self-oscillating,
	// blended, with the trim at its top.
	{
		FilterDrumDsp w;
		w.setSampleRate (rate);
		w.setMaxBlockSize (n);
		applyDefaultPatch (w);
		w.drum1 ().setResonance (kMaxResonanceK);
		w.drum2 ().setResonance (kMaxResonanceK);
		w.drum1 ().setVcaRelease (1.0);
		w.drum2 ().setVcaRelease (1.0);
		w.setOutputTrimDb (12.0);
		w.reset ();

		std::vector<float> a (n, 0.f), b (n, 0.f);
		w.trigger (1.0);
		w.render (a.data (), b.data (), n);
		const double pk = peakDbFS (a);
		std::printf ("  both self-oscillating, +12 dB trim: %+.2f dBFS\n", pk);
		check (std::isfinite (pk), "the worst case is still a finite number");
	}

	// AND BOTH DRUMS AT FULL RESONANCE WITH THE TRIM AT UNITY must not
	// clip - a user who turns both peaks up has not asked for
	// distortion, and the crossfader is what should keep it in bounds.
	{
		FilterDrumDsp w;
		w.setSampleRate (rate);
		w.setMaxBlockSize (n);
		applyDefaultPatch (w);
		w.drum1 ().setResonance (kMaxResonanceK);
		w.drum2 ().setResonance (kMaxResonanceK);
		w.drum1 ().setVcaRelease (1.0);
		w.drum2 ().setVcaRelease (1.0);
		w.reset ();

		std::vector<float> a (n, 0.f), b (n, 0.f);
		w.trigger (1.0);
		w.render (a.data (), b.data (), n);
		const double pk = peakDbFS (a);
		std::printf ("  both self-oscillating, unity trim:  %+.2f dBFS\n", pk);
		check (pk < 0.0, "both drums at full resonance do not clip at unity trim");
	}
}

//------------------------------------------------------------------------
int main ()
{
	std::printf ("FilterDrum DSP tests\n");
	std::printf ("--------------------\n");

	testConversions ();
	testVelocityLaw ();
	testEnvelopeTimes ();
	testShapedEnvelope ();
	testFilterResponse ();
	testSelfOscillation ();
	testCutoffModulation ();
	testVoiceVelocity ();
	testNoiseLevel ();
	testUnityIsBitIdentical ();
	testRobustness ();
	testVoiceLifecycle ();
	testNoise ();
	measureDefaultLevel ();
	testTheTwoDrums ();
	testDefaultsMatchTable ();
	measurePairDefault ();

	std::printf ("--------------------\n");
	std::printf ("%d checks, %d failures\n", gChecks, gFailures);

	return (gFailures == 0) ? 0 : 1;
}
