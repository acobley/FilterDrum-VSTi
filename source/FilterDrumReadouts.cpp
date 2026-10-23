//------------------------------------------------------------------------
// FilterDrum - every piece of text the panel shows (see the header)
//------------------------------------------------------------------------

#include "FilterDrumReadouts.h"
#include "FilterDrumDsp.h"
#include "FilterDrumTransport.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using Steinberg::Vst::ParamID;

namespace FilterDrum {

//------------------------------------------------------------------------
std::string gainText (double g)
{
	char text[16] = {};
	std::snprintf (text, sizeof (text), "%.2f", g);
	return (text[0] == '0') ? std::string (text + 1) : std::string (text);
}

//------------------------------------------------------------------------
std::string launchPositionName (int index)
{
	if (index <= 0)
		return "Bar";
	return divisionShortName (divisionFromIndex (index));
}

//------------------------------------------------------------------------
std::string readoutText (ParamID tag, double normalized, bool running)
{
	if (!isTableParam (tag))
		return std::string ();

	const ParamDef& def = paramDef (tag);
	const double plain = def.toPlain (normalized);

	char text[64] = {};

	int drum = 0;
	ParamID base = tag;
	splitDrumParam (tag, drum, base);

	switch (base)
	{
		case kCutoff:
			if (plain >= 1000.0)
				std::snprintf (text, sizeof (text), "%.2f kHz", plain / 1000.0);
			else
				std::snprintf (text, sizeof (text), "%.0f Hz", plain);
			break;

		case kResonance:
			// PER CENT AND K: the per cent is the knob, K is the number
			// the MS-20 literature quotes - self-oscillation at 2.
			std::snprintf (text, sizeof (text), "%.0f%% K%.2f", plain, def.toInternal (normalized));
			break;

		case kVcfAmount:
			// OCTAVES, signed, because octaves is what it does.
			std::snprintf (text, sizeof (text), "%+.1f oct", def.toInternal (normalized));
			break;

		case kOutputTrim:
			std::snprintf (text, sizeof (text), "%+.1f dB", plain);
			break;

		case kMix:
			if (plain >= 99.5)
				std::snprintf (text, sizeof (text), "D1 only");
			else if (plain <= 0.5)
				std::snprintf (text, sizeof (text), "D2 only");
			else
				std::snprintf (text, sizeof (text), "%.0f%%", plain);
			break;

		case kNoiseLevel:
			// AT ZERO IT SAYS SO IN WORDS: the noise is the voice's only
			// excitation, so at 0 the drum is silent however hard it is
			// played - and the panel should say that is the knob.
			if (plain <= 0.0)
				std::snprintf (text, sizeof (text), "silent");
			else
				std::snprintf (text, sizeof (text), "%.0f%%", plain);
			break;

		case kSeqRun:
			// Armed is the state worth showing: the pattern waits for its
			// launch line, which can be most of a bar away.
			std::snprintf (text, sizeof (text), "%s",
			               (normalized < 0.5) ? "off" : (running ? "running" : "armed"));
			break;

		case kSeqDivision:
			std::snprintf (text, sizeof (text), "%s",
			               launchPositionName (static_cast<int> (def.toInternal (normalized) + 0.5)).c_str ());
			break;

		default:
			if (def.type == ParamType::Log && std::strcmp (def.units, "ms") == 0)
			{
				if (plain < 10.0)
					std::snprintf (text, sizeof (text), "%.2f ms", plain);
				else if (plain < 100.0)
					std::snprintf (text, sizeof (text), "%.1f ms", plain);
				else
					std::snprintf (text, sizeof (text), "%.0f ms", plain);
			}
			else if (std::strcmp (def.units, "%") == 0)
				std::snprintf (text, sizeof (text), "%.0f%%", plain);
			else
				std::snprintf (text, sizeof (text), "%.0f %s", plain, def.units);
			break;
	}

	return std::string (text);
}

//------------------------------------------------------------------------
std::string velocityLineText (const InternalOf& internalOf, int drum)
{
	// velocityScaled() IS THE SHARED LAW - the call each voice latches
	// its amounts with. Velocity arrives from VST3 already normalised, so
	// 1.0 is MIDI 127.
	const double octaves = internalOf (drum, kVcfAmount);
	const double vcfSens = internalOf (drum, kVcfVelocity);
	const double gain    = internalOf (drum, kVcaAmount);
	const double vcaSens = internalOf (drum, kVcaVelocity);

	std::string line = "vel";
	const struct { const char* name; double v; const char* gap; } points[] = {
		{ "127", 1.0, " " }, { "64", 64.0 / 127.0, "    " }, { "0", 0.0, "    " } };

	for (const auto& p : points)
	{
		const double o = velocityScaled (octaves, vcfSens, p.v);
		const double g = velocityScaled (gain, vcaSens, p.v);
		char text[64] = {};
		if (std::fabs (o) < 0.05 && g < 0.005)
			std::snprintf (text, sizeof (text), "%s%s: off", p.gap, p.name);
		else
			std::snprintf (text, sizeof (text), "%s%s: %+.1f oct / %.0f%%", p.gap, p.name, o, g * 100.0);
		line += text;
	}
	return line;
}

//------------------------------------------------------------------------
std::string mixGainsText (double mix)
{
	// The two gains crossfadeGainDrum1/2() apply - the audio path's own
	// calls - so the numbers under the knob are the gains in use.
	return gainText (crossfadeGainDrum1 (mix)) + "  /  " + gainText (crossfadeGainDrum2 (mix)) + "   constant power";
}

//------------------------------------------------------------------------
std::string engineLineText (double sampleRate)
{
	// The cutoff CEILING is a fraction of the sample rate, so it is shown
	// next to the rate: at 44.1 k the Cutoff knob's top 20 kHz is not
	// reachable, and that is a question people ask rather than a bug.
	char text[96] = {};
	std::snprintf (text, sizeof (text), "Engine: %.0f Hz   /   cutoff ceiling %.0f Hz", sampleRate,
	               sampleRate * kMaxCutoffFraction);
	return text;
}

//------------------------------------------------------------------------
std::string filterPeakText (double hz)
{
	char text[24] = {};
	if (hz >= 1000.0)
		std::snprintf (text, sizeof (text), "peak %.1fk", hz / 1000.0);
	else
		std::snprintf (text, sizeof (text), "peak %.0f Hz", hz);
	return text;
}

std::string envCaptionText (double octaves)
{
	char text[32] = {};
	std::snprintf (text, sizeof (text), "VCF %+.1f oct", octaves);
	return text;
}

std::string envSpanText (double seconds)
{
	char text[24] = {};
	if (seconds >= 1.0)
		std::snprintf (text, sizeof (text), "%.2f s", seconds);
	else
		std::snprintf (text, sizeof (text), "%.0f ms", seconds * 1000.0);
	return text;
}

std::string lastStrikeText (double velocity)
{
	if (velocity < 0.0)
		return "last strike  -";
	char text[32] = {};
	std::snprintf (text, sizeof (text), "last strike  vel %d", static_cast<int> (velocity * 127.0 + 0.5));
	return text;
}

//------------------------------------------------------------------------
} // namespace FilterDrum
