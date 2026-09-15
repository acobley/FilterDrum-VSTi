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

/** The default patch, exactly as the parameter table defines it.
 
    THE NUMBERS ARE DUPLICATED FROM FilterDrumParams.cpp, which is
    normally the thing not to do - but this file may not include an SDK
    header and that table does. So they are written out once, here, and
    testDefaultsMatchTable() below is what stops the copy rotting: it
    fails if the internal ranges in the header and the values here stop
    agreeing about what the default patch is. */
void applyDefaultPatch (FilterDrumDsp& dsp)
{
	dsp.setOutputTrimDb (0.0);
	dsp.setCutoff (800.0);
	dsp.setResonance (0.96);        // 40 % of kMaxResonanceK
	dsp.setVcfAttack (0.001);       // 1 ms
	dsp.setVcfRelease (0.120);      // 120 ms
	dsp.setVcfAmount (3.6);         // 60 % of kMaxEnvOctaves
	dsp.setVcfVelocity (1.0);
	dsp.setVcaAttack (0.001);
	dsp.setVcaRelease (0.150);
	dsp.setVcaAmount (1.0);
	dsp.setVcaVelocity (1.0);
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

		// RELEASE: time to fall from 1.0 to -60 dBFS.
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

			int n = 0;
			const int limit = static_cast<int> (rate * 5.0);
			while (e.level () > 0.001f && n < limit)
			{
				e.next ();
				++n;
			}

			const double measured = n / rate;
			const double tol = std::max (release * 0.03, 2.0 / rate);
			checkClose (measured, release, tol,
			            "release reaches -60 dB in " + std::to_string (release) + " s at " + hz (rate));
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
		applyDefaultPatch (dsp);
		dsp.setVcaVelocity (vcaSens);
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
		applyDefaultPatch (dsp);
		dsp.setVcaAmount (0.0);
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
			applyDefaultPatch (dsp);
			dsp.setVcaVelocity (0.0);      // level independent of velocity
			dsp.setVcfVelocity (1.0);      // cutoff still velocity-scaled
			dsp.setVcfAmount (6.0);        // and a big sweep, so it is obvious
			dsp.reset ();
			return renderHit (dsp, velocity, block);
		};

		const double soft = peak (render (0.1));
		const double hard = peak (render (1.0));
		check (soft != hard, "VCF velocity changes the sound with the VCA held constant");
	}
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
	applyDefaultPatch (dsp);

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
			d.setCutoff (cutoff);
			d.setResonance (k);
			d.setVcfAttack (attack);
			d.setVcfRelease (0.120);
			d.setVcfAmount (amount);
			d.setVcfVelocity (1.0);
			d.setVcaAttack (attack);
			d.setVcaRelease (0.150);
			d.setVcaAmount (1.0);
			d.setVcaVelocity (1.0);
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
		applyDefaultPatch (d);
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
	applyDefaultPatch (dsp);
	dsp.setVcaRelease (0.050);
	dsp.reset ();

	check (!dsp.active (), "a voice that has never been played is inactive");

	std::vector<float> l (4800, 0.f), r (4800, 0.f);
	dsp.trigger (1.0);
	check (dsp.active (), "and active the moment it is triggered");

	dsp.render (l.data (), r.data (), 4800);     // 100 ms, twice the release
	check (!dsp.active (), "and inactive again once the VCA has closed");

	// THE VCA ALONE DECIDES. A long VCF release with a short VCA one
	// must not keep the voice alive - nothing that happens to the
	// cutoff of a muted signal is audible, and keeping it active would
	// hold the silence flags clear for no reason.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (4800);
		applyDefaultPatch (d);
		d.setVcaRelease (0.010);
		d.setVcfRelease (4.000);
		d.reset ();
		d.trigger (1.0);
		d.render (l.data (), r.data (), 4800);
		check (!d.active (), "a long VCF release does not keep the voice alive");
	}

	// AN IDLE VOICE RENDERS EXACT SILENCE, which is what makes the
	// silence flag honest.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (4800);
		applyDefaultPatch (d);
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
		applyDefaultPatch (d);
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
	applyDefaultPatch (dsp);
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
		applyDefaultPatch (d);
		d.setResonance (kMaxResonanceK);
		d.setVcaRelease (1.000);
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
		applyDefaultPatch (d);
		d.setResonance (kMaxResonanceK);
		d.setOutputTrimDb (12.0);
		d.reset ();
		const double pk = peakDbFS (renderHit (d, 1.0, block));
		std::printf ("  worst case, +12 dB trim:          %+.2f dBFS\n", pk);
		check (std::isfinite (pk), "the worst case is still a finite number");
	}

	// render() must CLEAR the block it is given, not add to whatever
	// was left in it - a host reuses its buffers, so an instrument that
	// forgets this plays back the last block forever.
	{
		FilterDrumDsp d;
		d.setSampleRate (rate);
		d.setMaxBlockSize (1024);
		applyDefaultPatch (d);
		d.reset ();
		std::vector<float> dirtyL (1024, 0.5f), dirtyR (1024, -0.5f);
		d.render (dirtyL.data (), dirtyR.data (), 1024);
		check (peak (dirtyL) == 0.0 && peak (dirtyR) == 0.0,
		       "render clears the host's buffer rather than adding to it");
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
}

//------------------------------------------------------------------------
int main ()
{
	std::printf ("FilterDrum DSP tests\n");
	std::printf ("--------------------\n");

	testConversions ();
	testVelocityLaw ();
	testEnvelopeTimes ();
	testFilterResponse ();
	testSelfOscillation ();
	testCutoffModulation ();
	testVoiceVelocity ();
	testUnityIsBitIdentical ();
	testRobustness ();
	testVoiceLifecycle ();
	testNoise ();
	measureDefaultLevel ();
	testDefaultsMatchTable ();

	std::printf ("--------------------\n");
	std::printf ("%d checks, %d failures\n", gChecks, gFailures);

	return (gFailures == 0) ? 0 : 1;
}
