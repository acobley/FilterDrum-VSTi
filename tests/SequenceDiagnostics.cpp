//------------------------------------------------------------------------
// FilterDrum - sequence diagnostics
//
// NOT A TEST SUITE. DspTests.cpp asserts; this one MEASURES AND PRINTS,
// and it exists because of a gap that suite has: every assertion in it
// looks at ONE hit. A drum machine is not played one hit at a time, and
// the defects that survive a 228-check suite are the ones that only
// appear across a sequence - the second hit differing from the first,
// a level that creeps, a knob that steps while it is moved.
//
//   c++ -std=c++17 -O2 -I../source SequenceDiagnostics.cpp
//       ../source/FilterDrumDsp.cpp -o /tmp/seqdiag  &&  /tmp/seqdiag
//
// Read the numbers, do not trust a pass/fail - there isn't one. What
// each section is looking for is written above it.
//------------------------------------------------------------------------

#include "FilterDrumDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace FilterDrum;

//------------------------------------------------------------------------
namespace {

double peakOf (const std::vector<float>& b)
{
	float p = 0.f;
	for (float v : b) p = std::max (p, std::fabs (v));
	return p;
}

double rmsOf (const std::vector<float>& b)
{
	double s = 0.0;
	for (float v : b) s += static_cast<double> (v) * v;
	return std::sqrt (s / b.size ());
}

/** Fraction of the energy above about 2 kHz - a crude brightness, enough
    to see a filter sweep starting from the wrong place. */
double brightnessOf (const std::vector<float>& b, double rate)
{
	double lp = 0.0, high = 0.0, total = 0.0;
	const double a = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 2000.0 / rate);
	for (float v : b)
	{
		lp += a * (v - lp);
		const double h = v - lp;
		high += h * h;
		total += static_cast<double> (v) * v;
	}
	return (total > 0.0) ? high / total : 0.0;
}

void patch (FilterDrumDsp& d, double rate, int block,
            double noise, double k, double vcfRelease, double vcaRelease)
{
	d.setSampleRate (rate);
	d.setMaxBlockSize (block);
	d.setOutputTrimDb (0.0);

	// ONE DRUM AT A TIME. Everything in this file predates the second
	// voice and measures a single one: the fader goes hard to drum 1 and
	// drum 2 is silenced, so the numbers stay comparable with the ones
	// recorded before the pair existed. The pair has its own section at
	// the end.
	d.setMix (1.0);
	d.drum2 ().setVcaAmount (0.0);

	DrumVoice& v = d.drum1 ();
	v.setNoiseLevel (noise);
	v.setCutoff (800.0);
	v.setResonance (k);
	v.setVcfAttack (0.001);
	v.setVcfRelease (vcfRelease);
	v.setVcfAmount (3.6);
	v.setVcfVelocity (1.0);
	v.setVcaAttack (0.001);
	v.setVcaRelease (vcaRelease);
	v.setVcaAmount (1.0);
	v.setVcaVelocity (1.0);

	d.reset ();
}

} // anonymous namespace

//------------------------------------------------------------------------
// 1. Does the first hit match the ones after it?
//
// LOOKING FOR: the VCF envelope being left part way through its release.
// renderVoices returns early while the VCA envelope is idle, and BOTH
// envelopes are advanced inside that loop - so a VCF release longer than
// the VCA's never completes. It freezes, and every hit after the first
// starts its cutoff sweep from a non-zero level instead of from zero.
//
// The noise is turned off for this one so that what is left is only the
// difference the envelope makes.
//------------------------------------------------------------------------
static void firstHitVersusTheRest ()
{
	std::printf ("1. first hit vs the rest\n\n");

	// MEASURED WITH THE NOISE ON, AND BY RMS OVER SEVERAL HITS.
	//
	// The first version of this ran with the noise off, so the only
	// thing that could vary was the envelope - a clean measurement that
	// stopped working the moment the trigger ping was removed, because
	// noise off is now exact silence and the ratio came out nan.
	//
	// So the noise has to stay on, and the method has to beat it. Peak
	// wanders 2.5 dB hit to hit on a noise burst; RMS wanders under
	// 1 dB, and averaging hits 2-4 halves that again. The effect being
	// looked for is 1 to 7 dB, which clears that comfortably.

	const double rate = 48000.0;
	const int n = static_cast<int> (rate * 0.25);

	struct Case { double vcf, vca; const char* note; };
	const Case cases[] = {
		{0.120, 0.150, "default - VCF release shorter than VCA"},
		{1.000, 0.100, "VCF release longer than VCA"},
		{4.000, 0.050, "VCF release much longer"},
		{0.050, 1.000, "VCF much shorter (hits overlap - expected)"},
	};

	for (const Case& c : cases)
	{
		FilterDrumDsp d;
		patch (d, rate, n, 1.0, 1.90, c.vcf, c.vca);

		std::vector<float> l (n), r (n);
		double first = 0.0, firstBright = 0.0;
		double restSum = 0.0, restBright = 0.0;
		int rest = 0;

		for (int h = 0; h < 4; ++h)
		{
			std::fill (l.begin (), l.end (), 0.f);
			std::fill (r.begin (), r.end (), 0.f);
			d.trigger (1.0);
			d.render (l.data (), r.data (), n);

			if (h == 0) { first = rmsOf (l); firstBright = brightnessOf (l, rate); }
			else        { restSum += rmsOf (l); restBright += brightnessOf (l, rate); ++rest; }
		}

		const double mean = restSum / rest;
		const double db = 20.0 * std::log10 (mean / first);
		const double brightPct = 100.0 * ((restBright / rest) / firstBright - 1.0);

		std::printf ("   VCF %6.0f ms / VCA %6.0f ms   hits 2-4 vs hit 1: %+6.2f dB, "
		             "%+6.1f %% brighter  %-4s %s\n",
		             c.vcf * 1000.0, c.vca * 1000.0, db, brightPct,
		             (std::fabs (db) > 1.0) ? "<--" : "ok", c.note);
	}

	// THE FREEZE ITSELF, which is unambiguous where the audio is not.
	// Same gate as renderVoices: both envelopes advance only while the
	// VCA is not idle.
	std::printf ("\n   the VCF envelope's level at each trigger (VCA 50 ms, VCF 4000 ms):\n");
	{
		AREnvelope vca, vcf;
		vca.setSampleRate (rate);
		vcf.setSampleRate (rate);
		vca.setAttack (0.001);
		vca.setRelease (0.050);
		vcf.setAttack (0.001);
		vcf.setRelease (4.000);
		vca.reset ();
		vcf.reset ();

		for (int h = 1; h <= 3; ++h)
		{
			std::printf ("      hit %d: %.4f\n", h, vcf.level ());
			vca.trigger ();
			vcf.trigger ();
			for (int i = 0; i < static_cast<int> (rate * 0.25) && !vca.idle (); ++i)
			{
				vca.next ();
				vcf.next ();
			}
		}
	}

	std::printf ("\n   The freeze is real and unchanged - hit 1 starts its sweep from 0,\n");
	std::printf ("   every hit after it from ~0.87. But the ATTACK reaches 1.0 either way,\n");
	std::printf ("   so the two only differ for the length of the attack, and with the\n");
	std::printf ("   noise driving the filter that is worth about 0.4 dB - flat across\n");
	std::printf ("   every window from 2 ms to 250 ms.\n\n");
	std::printf ("   THIS CORRECTS AN EARLIER READING. Measured through the trigger ping\n");
	std::printf ("   it looked like 1.3 to 6.6 dB, because the ping landed on the one\n");
	std::printf ("   sample where the difference exists. With the ping gone it is small.\n");
	std::printf ("   Still worth fixing - an inconsistency with no upside - but it is not\n");
	std::printf ("   what would make something sound wrong.\n\n");
}

//------------------------------------------------------------------------
// 2. How steady are identical hits?
//
// LOOKING FOR: whether the deliberate non-determinism (the noise is not
// reseeded and the filter is not reset on note-on) is audible as
// unsteadiness.
//
// PEAK AND RMS BOTH, because they answer different questions. Peak
// varies a lot for any noise burst and says little about loudness; RMS
// is much closer to what the ear reports. If peak wanders and RMS does
// not, the hits sound even.
//------------------------------------------------------------------------
static void hitToHitConsistency ()
{
	std::printf ("2. hit-to-hit consistency, default patch\n\n");

	const double rate = 48000.0;
	const int n = static_cast<int> (rate * 0.25);

	FilterDrumDsp d;
	patch (d, rate, n, 1.0, 0.96, 0.120, 0.150);

	std::vector<float> l (n), r (n);
	double p0 = 0.0, r0 = 0.0, b0 = 0.0;
	double peakSpread = 0.0, rmsSpread = 0.0;

	for (int h = 0; h < 8; ++h)
	{
		std::fill (l.begin (), l.end (), 0.f);
		std::fill (r.begin (), r.end (), 0.f);
		d.trigger (1.0);
		d.render (l.data (), r.data (), n);

		const double p = peakOf (l), q = rmsOf (l), b = brightnessOf (l, rate);
		if (h == 0) { p0 = p; r0 = q; b0 = b; }

		const double pdb = 20.0 * std::log10 (p / p0);
		const double rdb = 20.0 * std::log10 (q / r0);
		peakSpread = std::max (peakSpread, std::fabs (pdb));
		rmsSpread  = std::max (rmsSpread,  std::fabs (rdb));

		std::printf ("   hit %d   peak %+6.2f dB   RMS %+6.2f dB   brightness %+6.1f %%\n",
		             h + 1, pdb, rdb, 100.0 * (b / b0 - 1.0));
	}

	std::printf ("\n   spread: peak %.2f dB, RMS %.2f dB\n", peakSpread, rmsSpread);
	std::printf ("   RMS is the one to judge by.\n\n");
}

//------------------------------------------------------------------------
// 3. How low can Noise Level go before the voice dies?
//
// LOOKING FOR: the bottom of the knob's useful travel. The noise is the
// voice's only excitation, so at 0 there is nothing to ring and the
// plug-in is silent; the question is where between 0 and 1 it stops
// being a usable setting, and whether that point moves with resonance.
//
// This section used to measure the trigger ping's waveform asymmetry.
// The ping was removed; the knob's bottom end is the live question that
// replaced it.
//------------------------------------------------------------------------
static void noiseFloor ()
{
	std::printf ("3. the bottom of the Noise Level knob\n\n");

	const double rate = 48000.0;
	const int n = static_cast<int> (rate * 0.5);

	std::printf ("   %-10s %12s %12s %12s\n", "noise", "K=0.96", "K=1.90", "K=2.40");

	for (double noise : { 0.0, 0.002, 0.005, 0.01, 0.05, 0.25, 1.0 })
	{
		std::printf ("   %8.1f%% ", noise * 100.0);
		for (double k : { 0.96, 1.90, kMaxResonanceK })
		{
			FilterDrumDsp d;
			patch (d, rate, n, noise, k, 0.120, 0.150);

			std::vector<float> l (n), r (n);
			std::fill (l.begin (), l.end (), 0.f);
			d.trigger (1.0);
			d.render (l.data (), r.data (), n);

			const double p = peakOf (l);
			if (p <= 0.0) std::printf ("%12s", "silent");
			else          std::printf ("%11.1f dB", 20.0 * std::log10 (p));
		}
		std::printf ("\n");
	}

	std::printf ("\n   Silence at 0 is exact, not small - a linear filter fed zero from\n");
	std::printf ("   a zero state stays at zero however high the resonance is set.\n\n");
}

//------------------------------------------------------------------------
// 4. Do the knobs marked "smoothed" actually step?
//
// LOOKING FOR: a discontinuity at block boundaries when a parameter is
// automated. The parameter table marks Cutoff, Resonance and Noise Level
// smoothed, but only the output trim has a Smoother - the others are
// written straight through, once per block.
//
// Measured on a PURE TONE (noise off, self-oscillating), because a
// stepped parameter is inaudible under noise and obvious under a tone.
// The second difference of a sine is small and smooth; a step shows as a
// spike at the block edge.
//------------------------------------------------------------------------
static void parameterStepping ()
{
	std::printf ("4. automation stepping on a pure tone\n\n");

	const double rate = 48000.0;
	const int blk = 128, blocks = 64;

	auto sweep = [rate, blk, blocks] (const char* what, int which) {
		FilterDrumDsp d;
		patch (d, rate, blk, 0.0, kMaxResonanceK, 10.0, 10.0);
		d.drum1 ().setCutoff (400.0);
		d.drum1 ().setVcfAmount (0.0);
		d.drum1 ().setVcfVelocity (0.0);
		d.drum1 ().setVcaVelocity (0.0);
		d.reset ();
		d.trigger (1.0);

		std::vector<float> l (blk), r (blk), all;
		for (int b = 0; b < blocks; ++b)
		{
			const double t = b / static_cast<double> (blocks);
			if (which == 0) d.drum1 ().setCutoff (400.0 * std::pow (4.0, t));
			if (which == 1) d.drum1 ().setResonance (2.0 + 0.4 * t);
			if (which == 2) d.drum1 ().setNoiseLevel (t);
			d.render (l.data (), r.data (), blk);
			all.insert (all.end (), l.begin (), l.end ());
		}

		double edge = 0.0, interior = 0.0;
		for (size_t i = 2; i < all.size (); ++i)
		{
			const double s = std::fabs (all[i] - 2.0 * all[i - 1] + all[i - 2]);
			if (static_cast<int> (i) % blk <= 1) edge = std::max (edge, s);
			else                                 interior = std::max (interior, s);
		}

		std::printf ("   %-12s  block edge %.6f   interior %.6f   ratio %5.2fx  %s\n",
		             what, edge, interior, edge / (interior + 1e-12),
		             (edge > interior * 1.5) ? "<-- STEPPED" : "continuous");
	};

	sweep ("Cutoff", 0);
	sweep ("Resonance", 1);
	sweep ("Noise Level", 2);

	std::printf ("\n   A TPT filter changes its coefficients without a discontinuity in\n");
	std::printf ("   the state, so a stepped cutoff need not zipper. Trust the ratio.\n\n");
}

//------------------------------------------------------------------------
int main ()
{
	std::printf ("FilterDrum sequence diagnostics\n");
	std::printf ("===============================\n\n");

	firstHitVersusTheRest ();
	hitToHitConsistency ();
	noiseFloor ();
	parameterStepping ();

	std::printf ("These are measurements, not assertions. Nothing here fails.\n");
	return 0;
}
