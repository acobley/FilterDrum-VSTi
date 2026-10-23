//------------------------------------------------------------------------
// tools/dump-layout.cpp
//
// Prints the Design 2.0 panel as JSON - the layout tables, every
// control's reading at the table defaults, and the screens' curves - so
// that tools/render-panel.py can draw docs/panel.png from THE PLUG-IN'S
// OWN TABLES AND DSP rather than from a second copy in Python.
//
//   c++ -std=c++17 -Isource -Iexternal/vst3sdk tools/dump-layout.cpp
//       source/FilterDrumParams.cpp source/FilterDrumDsp.cpp
//       source/FilterDrumTransport.cpp -o /tmp/dump-layout
//   /tmp/dump-layout [sampleRate] > layout.json
//
// render-panel.py compiles and runs it for you.
//
// THE ONE COPY: readingFor() below repeats FilterDrumEditor::readoutFor()'s
// formats, because that function needs VSTGUI and this must not. If a
// reading changes there, change it here; the picture is otherwise right
// about everything but that text.
//------------------------------------------------------------------------
#include "FilterDrumDsp.h"
#include "FilterDrumLayout.h"
#include "FilterDrumParams.h"
#include "FilterDrumTransport.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace FilterDrum;
using namespace FilterDrum::Layout;
using Steinberg::Vst::ParamID;

namespace {

double internalOf (int drum, ParamID base)
{
	const ParamID id = static_cast<ParamID> (base + ((drum == 2) ? kDrum2Offset : 0));
	return paramDef (id).toInternal (paramDef (id).defaultNormalized ());
}

std::string json (const std::string& s)
{
	std::string out = "\"";
	for (char c : s)
	{
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	return out + "\"";
}

std::string rect (const Rect& r)
{
	char t[64];
	std::snprintf (t, sizeof (t), "[%d,%d,%d,%d]", r.left, r.top, r.right, r.bottom);
	return t;
}

std::string curve (const float* v, int n)
{
	std::string s = "[";
	for (int i = 0; i < n; ++i)
	{
		char t[24];
		std::snprintf (t, sizeof (t), "%s%.4f", i ? "," : "", v[i]);
		s += t;
	}
	return s + "]";
}

/** FilterDrumEditor::readoutFor()'s formats - see the banner. */
std::string readingFor (ParamID tag)
{
	const ParamDef& def = paramDef (tag);
	const double n = def.defaultNormalized ();
	const double plain = def.toPlain (n);
	int drum = 0;
	ParamID base = tag;
	splitDrumParam (tag, drum, base);
	char t[64] = {};
	switch (base)
	{
		case kCutoff:
			if (plain >= 1000.0) std::snprintf (t, sizeof (t), "%.2f kHz", plain / 1000.0);
			else                 std::snprintf (t, sizeof (t), "%.0f Hz", plain);
			break;
		case kResonance:   std::snprintf (t, sizeof (t), "%.0f%%  K%.2f", plain, def.toInternal (n)); break;
		case kVcfAmount:   std::snprintf (t, sizeof (t), "%+.1f oct", def.toInternal (n)); break;
		case kOutputTrim:  std::snprintf (t, sizeof (t), "%+.1f dB", plain); break;
		case kMix:
			if (plain >= 99.5)     std::snprintf (t, sizeof (t), "D1 only");
			else if (plain <= 0.5) std::snprintf (t, sizeof (t), "D2 only");
			else                   std::snprintf (t, sizeof (t), "%.0f%%", plain);
			break;
		case kNoiseLevel:
			if (plain <= 0.0) std::snprintf (t, sizeof (t), "silent");
			else              std::snprintf (t, sizeof (t), "%.0f%%", plain);
			break;
		case kSeqRun:      std::snprintf (t, sizeof (t), "%s", n < 0.5 ? "off" : "armed"); break;
		default:
			if (def.type == ParamType::Log && std::strcmp (def.units, "ms") == 0)
			{
				if (plain < 10.0)       std::snprintf (t, sizeof (t), "%.2f ms", plain);
				else if (plain < 100.0) std::snprintf (t, sizeof (t), "%.1f ms", plain);
				else                    std::snprintf (t, sizeof (t), "%.0f ms", plain);
			}
			else if (std::strcmp (def.units, "%") == 0)
				std::snprintf (t, sizeof (t), "%.0f%%", plain);
			else
				std::snprintf (t, sizeof (t), "%.0f %s", plain, def.units);
			break;
	}
	return t;
}

} // anonymous namespace

int main (int argc, char** argv)
{
	const double rate = (argc > 1) ? std::atof (argv[1]) : 48000.0;
	std::string o = "{";

	o += "\"size\":[" + std::to_string (kEditorWidth) + "," + std::to_string (kEditorHeight) + "],";
	o += "\"header\":" + std::to_string (kHeaderHeight) + ",";

	o += "\"sections\":[";
	for (int i = 0; i < kNumSections; ++i)
	{
		const auto& s = kSections[i];
		o += std::string (i ? "," : "") + "{\"title\":" + json (s.title) + ",\"sub\":" + json (s.subtitle)
		     + ",\"badge\":" + json (s.badge) + ",\"accent\":" + std::to_string ((int) s.accent)
		     + ",\"centred\":" + (s.centred ? "true" : "false") + ",\"r\":" + rect (s.r) + "}";
	}
	o += "],\"controls\":[";
	for (int i = 0; i < kNumControls; ++i)
	{
		const auto& c = kControls[i];
		const ParamDef& def = paramDef (c.tag);
		o += std::string (i ? "," : "") + "{\"tag\":" + std::to_string (c.tag)
		     + ",\"kind\":" + std::to_string ((int) c.kind) + ",\"accent\":" + std::to_string ((int) c.accent)
		     + ",\"diameter\":" + std::to_string (c.diameter) + ",\"bipolar\":" + (c.bipolar ? "true" : "false")
		     + ",\"reversed\":" + (c.reversed ? "true" : "false")
		     + ",\"label\":" + json (c.label ? c.label : "") + ",\"value\":" + std::to_string (def.defaultNormalized ())
		     + ",\"reading\":" + json (readingFor (c.tag)) + ",\"r\":" + rect (controlRect (c));
		if (c.kind == Kind::Step)
			o += ",\"number\":" + std::to_string (c.tag - kStep1 + 1);
		o += "}";
	}
	o += "],\"divisions\":[";
	for (int i = 0; i < kLaunchDivisionCount; ++i)
		o += std::string (i ? "," : "") + json (divisionShortName (divisionFromIndex (i)));
	o += "],\"captions\":[";
	for (int i = 0; i < kNumCaptions; ++i)
	{
		const auto& c = kCaptions[i];
		o += std::string (i ? "," : "") + "{\"text\":" + json (c.text) + ",\"style\":" + std::to_string ((int) c.style)
		     + ",\"accent\":" + std::to_string ((int) c.accent) + ",\"centred\":" + (c.centred ? "true" : "false")
		     + ",\"r\":" + rect (c.r) + "}";
	}
	o += "],\"rules\":[";
	{
		int n = 0;
		for (const auto& r : kRules) o += std::string (n++ ? "," : "") + rect (r);
	}
	o += "],\"chevrons\":[";
	{
		int n = 0;
		for (const auto& p : kChevrons)
			o += std::string (n++ ? "," : "") + "[" + std::to_string (p.x) + "," + std::to_string (p.y) + "]";
	}

	// ---- the lanes: screens from the DSP's own trace functions -----------
	o += "],\"lanes\":[";
	for (int drum = 1; drum <= 2; ++drum)
	{
		const int top = laneTop (drum);
		const double cutoff = internalOf (drum, kCutoff);
		const double k = internalOf (drum, kResonance);
		const double oct = internalOf (drum, kVcfAmount);
		const double sens = internalOf (drum, kVcfVelocity);
		const double gain = internalOf (drum, kVcaAmount);
		const double vsens = internalOf (drum, kVcaVelocity);
		const double peakHz = cutoffWithEnv (cutoff, velocityScaled (oct, sens, 1.0), 1.0, rate);

		float knobs[kDisplayPoints], strike[kDisplayPoints];
		const double dotY = traceFilterResponse (cutoff, k, rate, kFilterMinHz, kFilterMaxHz, kFilterTopDb,
		                                         kFilterBottomDb, knobs, kDisplayPoints);
		traceFilterResponse (peakHz, k, rate, kFilterMinHz, kFilterMaxHz, kFilterTopDb, kFilterBottomDb,
		                     strike, kDisplayPoints);
		const double used = cutoffWithEnv (cutoff, 0.0, 0.0, rate);
		const double dotX = std::log (used / kFilterMinHz) / std::log (kFilterMaxHz / kFilterMinHz);

		ArSpec vcf, vca;
		vcf.attack = internalOf (drum, kVcfAttack);  vcf.release = internalOf (drum, kVcfRelease);
		vca.attack = internalOf (drum, kVcaAttack);  vca.release = internalOf (drum, kVcaRelease);
		float vcfC[kDisplayPoints], vcaC[kDisplayPoints];
		const double span = traceDrumEnvelopes (vcf, vca, vcfC, vcaC, kDisplayPoints);
		const float vcfScale = static_cast<float> (std::min (1.0, std::fabs (oct) / kMaxEnvOctaves));
		for (int i = 0; i < kDisplayPoints; ++i) { vcfC[i] *= vcfScale; vcaC[i] *= static_cast<float> (gain); }

		// The velocity line: velocityScaled(), as FilterDrumEditor::velocityLineFor().
		std::string vel = "velocity";
		const struct { const char* name; double v; } pts[] = { { "127", 1.0 }, { "64", 64.0 / 127.0 }, { "0", 0.0 } };
		for (const auto& p : pts)
		{
			const double vo = velocityScaled (oct, sens, p.v), vg = velocityScaled (gain, vsens, p.v);
			char t[64];
			if (std::fabs (vo) < 0.05 && vg < 0.005) std::snprintf (t, sizeof (t), "    %s: off", p.name);
			else std::snprintf (t, sizeof (t), "    %s: %+.1f oct \xC2\xB7 %.0f%%", p.name, vo, vg * 100.0);
			vel += t;
		}

		char peak[24], caption[32], ann[24];
		if (peakHz >= 1000.0) std::snprintf (peak, sizeof (peak), "strike %.1fk", peakHz / 1000.0);
		else                  std::snprintf (peak, sizeof (peak), "strike %.0f Hz", peakHz);
		std::snprintf (caption, sizeof (caption), "VCF %+.1f oct", oct);
		if (span >= 1.0) std::snprintf (ann, sizeof (ann), "%.2f s", span);
		else             std::snprintf (ann, sizeof (ann), "%.0f ms", span * 1000.0);

		o += std::string (drum > 1 ? "," : "") + "{\"filter\":" + rect (laneRect (Lane::kFilterScreen, top))
		     + ",\"env\":" + rect (laneRect (Lane::kEnvScreen, top))
		     + ",\"lamp\":" + rect (laneRect (Lane::kSelfOscLamp, top))
		     + ",\"velRect\":" + rect (laneRect (Lane::kVelocityLine, top))
		     + ",\"selfOsc\":" + (selfOscillating (k) ? "true" : "false")
		     + ",\"knobs\":" + curve (knobs, kDisplayPoints) + ",\"strike\":" + curve (strike, kDisplayPoints)
		     + ",\"dot\":[" + std::to_string (dotX) + "," + std::to_string (dotY) + "]"
		     + ",\"peak\":" + json (peak) + ",\"vcf\":" + curve (vcfC, kDisplayPoints) + ",\"vca\":" + curve (vcaC, kDisplayPoints)
		     + ",\"caption\":" + json (caption) + ",\"span\":" + json (ann) + ",\"velocity\":" + json (vel) + "}";
	}

	const double mix = paramDef (kMix).toInternal (paramDef (kMix).defaultNormalized ());
	char gains[48], engine[96];
	std::snprintf (gains, sizeof (gains), "%.2f  \xC2\xB7  %.2f", crossfadeGainDrum1 (mix), crossfadeGainDrum2 (mix));
	std::snprintf (engine, sizeof (engine), "Engine: %.0f Hz      cutoff ceiling %.0f Hz", rate, rate * kMaxCutoffFraction);
	std::string g (gains);
	// ".71", as gainText() in the editor
	for (size_t p = 0; (p = g.find ("0.", p)) != std::string::npos; ) { if (p == 0 || g[p - 1] == ' ') g.erase (p, 1); else ++p; }

	o += "],\"wordmark\":" + rect (kWordmark) + ",\"engineRect\":" + rect (kEngineLine) + ",\"engine\":" + json (engine)
	     + ",\"mixRect\":" + rect (kMixGainsLine) + ",\"mix\":" + json (g)
	     + ",\"strike1\":" + rect (kStrikeLamp1) + ",\"strike2\":" + rect (kStrikeLamp2)
	     + ",\"strikeVelRect\":" + rect (kStrikeVelLine) + "}";

	std::puts (o.c_str ());
	return 0;
}
