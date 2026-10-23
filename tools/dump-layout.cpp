//------------------------------------------------------------------------
// tools/dump-layout.cpp - the panel as JSON, for tools/render-panel.py
//
// WHY A C++ TOOL AND NOT A PARSER. Everything the picture needs - the
// positions, the sections, the text on every control, the four screens'
// curves - is printed here by the SAME code the editor runs:
// FilterDrumLayout.h for where things are, FilterDrumReadouts.cpp for what
// they say, traceFilterResponse() and traceDrumEnvelopes() for the
// curves. The Python script only draws. So the picture cannot drift from
// the panel without this failing to compile first. (Design 2.0's version
// kept a second copy of the editor's formats; the readouts moving out of
// the editor ended that.)
//
// The values are the parameter table's DEFAULTS, so docs/panel.png is the
// panel as it opens on a fresh instance, at 48 k unless told otherwise.
//
// THE BLACK PANEL (PolivoksUI, Oct 2026): the lettering's styles, every
// glyph of the face PARSED (SciFiLettering.h - so the script strokes the
// same paths and keeps no copy of the table), each section's title box,
// the selector's geometry and where its position names are printed, and
// every mark PanelWear.h makes, under "wear".
//
//   c++ -std=c++17 -Isource -Iexternal/vst3sdk tools/dump-layout.cpp
//       source/FilterDrumParams.cpp source/FilterDrumReadouts.cpp
//       source/FilterDrumDsp.cpp source/FilterDrumTransport.cpp
//       -o /tmp/dump-layout
//   /tmp/dump-layout [sampleRate] > layout.json
//
// render-panel.py compiles and runs it for you.
//------------------------------------------------------------------------

#include "FilterDrumDsp.h"
#include "FilterDrumLayout.h"
#include "FilterDrumParams.h"
#include "FilterDrumReadouts.h"
#include "FilterDrumTransport.h"
#include "PanelWear.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace FilterDrum;
using namespace FilterDrum::Layout;
using Steinberg::Vst::ParamID;

namespace {

std::string quoted (const std::string& s)
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

std::string num (double v)
{
	char t[32];
	std::snprintf (t, sizeof (t), "%.3f", v);
	return t;
}

std::string box (const Box& b)
{
	return "[" + num (b.left) + "," + num (b.top) + "," + num (b.right) + "," + num (b.bottom) + "]";
}

const char* accentName (Accent a)
{
	switch (a)
	{
		case Accent::Noise:  return "\"noise\"";
		case Accent::Filter: return "\"filter\"";
		case Accent::Amp:    return "\"amp\"";
		case Accent::Seq:    return "\"seq\"";
	}
	return "\"noise\"";
}

const char* styleName (CaptionStyle s)
{
	switch (s)
	{
		case CaptionStyle::Block: return "\"block\"";
		case CaptionStyle::Field: return "\"field\"";
		case CaptionStyle::Note:  return "\"note\"";
	}
	return "\"note\"";
}

const char* alignName (Align a)
{
	return (a == Align::Left) ? "\"left\"" : (a == Align::Right) ? "\"right\"" : "\"centre\"";
}

const char* kindName (Kind k)
{
	switch (k)
	{
		case Kind::Knob:   return "\"knob\"";
		case Kind::Switch: return "\"switch\"";
		case Kind::Step:   return "\"step\"";
		case Kind::Toggle: return "\"toggle\"";
	}
	return "\"knob\"";
}

std::string style (const Lettering::Style& s)
{
	return "{\"cap\": " + num (s.cap) + ", \"weight\": " + num (s.weight) + ", \"track\": " + num (s.track) + "}";
}

std::string commands (const char* path)
{
	std::string out = "[";
	bool first = true;
	for (const auto& c : Lettering::parse (path))
	{
		out += first ? "" : ",";
		first = false;
		out += "[\"" + std::string (1, c.op) + "\"," + num (c.x1) + "," + num (c.y1) + "," + num (c.x) + "," + num (c.y) + "]";
	}
	return out + "]";
}

std::string curve (const float* v, int n)
{
	std::string out = "[";
	char t[24];
	for (int i = 0; i < n; ++i)
	{
		std::snprintf (t, sizeof (t), "%s%.4f", i ? "," : "", v[i]);
		out += t;
	}
	return out + "]";
}

std::string pts (const std::vector<Wear::Pt>& v)
{
	std::string out = "[";
	char t[48];
	for (size_t i = 0; i < v.size (); ++i)
	{
		std::snprintf (t, sizeof (t), "%s%.3f,%.3f", i ? "," : "", v[i].x, v[i].y);
		out += t;
	}
	return out + "]";
}

std::string lines (const std::vector<Wear::Line>& v)
{
	std::string out = "[";
	for (size_t i = 0; i < v.size (); ++i)
		out += std::string (i ? "," : "") + "{\"w\": " + num (v[i].width) + ", \"a\": " +
		       std::to_string (v[i].alpha) + ", \"pts\": " + pts (v[i].pts) + "}";
	return out + "]";
}

std::string specks (const std::vector<Wear::Speck>& v)
{
	std::string out = "[";
	for (size_t i = 0; i < v.size (); ++i)
		out += std::string (i ? "," : "") + "[" + num (v[i].x) + "," + num (v[i].y) + "," + num (v[i].r) + "," +
		       std::to_string (v[i].alpha) + "]";
	return out + "]";
}

std::string polar (const std::vector<Wear::PolarSpeck>& v)
{
	std::string out = "[";
	for (size_t i = 0; i < v.size (); ++i)
		out += std::string (i ? "," : "") + "[" + num (v[i].radius) + "," + num (v[i].angle) + "," + num (v[i].r) +
		       "," + std::to_string (v[i].alpha) + "]";
	return out + "]";
}

std::string rgb (const Wear::Rgb& c)
{
	return "[" + std::to_string (c.r) + "," + std::to_string (c.g) + "," + std::to_string (c.b) + "]";
}

void printWear ()
{
	using namespace Wear;
	const PanelWear w = panelWear ();
	std::printf ("  \"wear\": {\n");
	std::printf ("    \"colours\": {\"dust\": %s, \"speck\": %s, \"scratch\": %s, \"scuff\": %s, \"knurlDust\": %s, "
	             "\"glassDust\": %s, \"print\": %s, \"sheen\": %s},\n",
	             rgb (kDust).c_str (), rgb (kSpeck).c_str (), rgb (kScratch).c_str (), rgb (kScuff).c_str (),
	             rgb (kKnurlDust).c_str (), rgb (kGlassDust).c_str (), rgb (kPrint).c_str (), rgb (kSheen).c_str ());

	std::string haze = "[";
	for (size_t i = 0; i < w.haze.size (); ++i)
		haze += std::string (i ? "," : "") + "[" + num (w.haze[i].x) + "," + num (w.haze[i].y) + "," +
		        num (w.haze[i].r) + "," + std::to_string (w.haze[i].alpha) + "]";
	std::printf ("    \"haze\": %s],\n", haze.c_str ());

	std::string drifts = "[";
	for (size_t i = 0; i < w.drifts.size (); ++i)
		drifts += std::string (i ? "," : "") + "[" + rect (w.drifts[i].r) + "," + std::to_string (w.drifts[i].fromX) +
		          "," + std::to_string (w.drifts[i].fromY) + "," + std::to_string (w.drifts[i].alpha) + "]";
	std::printf ("    \"drifts\": %s],\n", drifts.c_str ());

	std::string rings = "[";
	for (size_t i = 0; i < w.rings.size (); ++i)
	{
		const auto& f = w.rings[i];
		rings += std::string (i ? "," : "") + "[" + num (f.cx) + "," + num (f.cy) + "," + num (f.inner) + "," +
		         num (f.peak) + "," + num (f.outer) + "," + std::to_string (f.alpha) + "]";
	}
	std::printf ("    \"rings\": %s],\n", rings.c_str ());
	std::printf ("    \"scratches\": %s,\n", lines (w.scratches).c_str ());
	std::printf ("    \"specks\": %s,\n", specks (w.specks).c_str ());
	std::printf ("    \"fibres\": %s,\n", lines (w.fibres).c_str ());

	// one entry per control, in kControls order: a knob's wear, the
	// selector's grit, nothing for a key
	std::printf ("    \"controls\": [\n");
	for (int i = 0; i < kNumControls; ++i)
	{
		const char* end = (i + 1 < kNumControls) ? "," : "";
		if (kControls[i].kind == Kind::Knob)
		{
			const KnobWear k = knobWear (i);
			std::string scuffs = "[";
			for (size_t j = 0; j < k.scuffs.size (); ++j)
			{
				const auto& a = k.scuffs[j];
				scuffs += std::string (j ? "," : "") + "[" + num (a.radius) + "," + num (a.start) + "," +
				          num (a.span) + "," + num (a.width) + "," + std::to_string (a.alpha) + "]";
			}
			std::printf ("      {\"scuffs\": %s], \"grit\": %s, \"sheen\": %d, \"knurl\": %d}%s\n", scuffs.c_str (),
			             polar (k.grit).c_str (), k.sheen, k.knurlAlpha, end);
		}
		else if (kControls[i].kind == Kind::Switch)
			std::printf ("      {\"grit\": %s}%s\n", polar (selectorGrit (i)).c_str (), end);
		else
			std::printf ("      {}%s\n", end);
	}
	std::printf ("    ],\n");

	// one entry per screen, in kScreens order
	std::printf ("    \"glass\": [\n");
	for (int i = 0; i < kNumScreens; ++i)
	{
		const GlassWear g = glassWear (i);
		std::string prints = "[";
		for (size_t j = 0; j < g.prints.size (); ++j)
			prints += std::string (j ? "," : "") + lines (g.prints[j].ridges);
		std::printf ("      {\"dust\": %s, \"fibres\": %s, \"prints\": %s]}%s\n", specks (g.dust).c_str (),
		             lines (g.fibres).c_str (), prints.c_str (), (i + 1 < kNumScreens) ? "," : "");
	}
	std::printf ("    ]\n  },\n");
}

double internalDefault (int drum, ParamID base)
{
	const ParamID id = static_cast<ParamID> (base + ((drum == 2) ? kDrum2Offset : 0));
	return paramDef (id).toInternal (paramDef (id).defaultNormalized ());
}

std::string textLine (const Rect& r, const std::string& text, const char* styleKey, Align a)
{
	return "{\"rect\": " + rect (r) + ", \"text\": " + quoted (text) + ", \"style\": \"" + styleKey +
	       "\", \"align\": " + alignName (a) + "}";
}

} // anonymous namespace

int main (int argc, char** argv)
{
	const double rate = (argc > 1) ? std::atof (argv[1]) : 48000.0;
	const int n = kDisplayPoints;

	std::printf ("{\n");
	std::printf ("  \"width\": %d, \"height\": %d, \"headerHeight\": %d, \"sectionRadius\": %d,\n", kEditorWidth,
	             kEditorHeight, kHeaderHeight, kSectionRadius);
	std::printf ("  \"knobGap\": %d, \"knobValueH\": %d, \"knobLabelH\": %d, \"screenBezel\": %d,\n", kKnobGap,
	             kKnobValueH, kKnobLabelH, kScreenBezel);
	std::printf ("  \"wordmark\": %s, \"wordmarkGap\": %d, \"headerDivider\": %d, \"badgeRadius\": %d,\n",
	             rect (kWordmark).c_str (), kWordmarkGap, kHeaderDivider, kBadgeRadius);
	std::printf ("  \"blockCaptionInset\": %d, \"blockCaptionGap\": %d,\n", kBlockCaptionInset, kBlockCaptionGap);
	std::printf ("  \"step\": {\"led\": %d, \"keyTop\": %d, \"keyBottom\": %d, \"number\": %d},\n", kStepLedY,
	             kStepKeyTop, kStepKeyBottom, kStepNumberY);

	// ---- the lettering: the styles, and every glyph parsed -----------------
	std::printf ("  \"styles\": {\"wordmark\": %s, \"title\": %s, \"subtitle\": %s, \"label\": %s, \"value\": %s,\n"
	             "             \"block\": %s, \"note\": %s, \"engine\": %s, \"info\": %s, \"screen\": %s,\n"
	             "             \"badge\": %s, \"tick\": %s, \"button\": %s, \"step\": %s},\n",
	             style (kTextWordmark).c_str (), style (kTextTitle).c_str (), style (kTextSubtitle).c_str (),
	             style (kTextLabel).c_str (), style (kTextValue).c_str (), style (kTextBlock).c_str (),
	             style (kTextNote).c_str (), style (kTextEngine).c_str (), style (kTextInfo).c_str (),
	             style (kTextScreen).c_str (), style (kTextBadge).c_str (), style (kTextTick).c_str (),
	             style (kTextButton).c_str (), style (kTextStep).c_str ());
	std::printf ("  \"space\": %s,\n  \"glyphs\": {\n", num (Lettering::kSpace).c_str ());
	for (uint32_t c = 1; c < 0x2600; ++c)
		if (const Lettering::Glyph* g = Lettering::glyphFor (c))
			std::printf ("    \"%u\": [%s, %s],\n", c, num (g->advance).c_str (), commands (g->path).c_str ());
	std::printf ("    \"missing\": [%s, %s]\n  },\n", num (Lettering::missingGlyph ().advance).c_str (),
	             commands (Lettering::missingGlyph ().path).c_str ());

	printWear ();

	std::printf ("  \"screws\": [");
	for (size_t i = 0; i < sizeof (kScrews) / sizeof (kScrews[0]); ++i)
		std::printf ("%s[%d,%d]", i ? "," : "", kScrews[i].x, kScrews[i].y);
	std::printf ("],\n");

	std::printf ("  \"sections\": [\n");
	for (int i = 0; i < kNumSections; ++i)
	{
		const Section& s = kSections[i];
		double bx = 0.0, by = 0.0;
		badgeCentre (s, bx, by);
		std::printf ("    {\"title\": %s, \"subtitle\": %s, \"badge\": %s, \"badgeCentre\": [%s,%s], \"rect\": %s, "
		             "\"titleBox\": %s, \"subtitleLeft\": %s}%s\n",
		             quoted (s.title).c_str (), quoted (s.subtitle).c_str (), quoted (s.badge).c_str (),
		             num (bx).c_str (), num (by).c_str (), rect (s.r).c_str (), box (titleBox (s)).c_str (),
		             num (subtitleLeft (s)).c_str (), (i + 1 < kNumSections) ? "," : "");
	}
	std::printf ("  ],\n");

	std::printf ("  \"blocks\": [");
	for (size_t i = 0; i < sizeof (kBlocks) / sizeof (kBlocks[0]); ++i)
		std::printf ("%s{\"caption\": %s, \"rect\": %s}", i ? "," : "", quoted (kBlocks[i].caption).c_str (),
		             rect (kBlocks[i].r).c_str ());
	std::printf ("],\n");

	std::printf ("  \"chevrons\": [");
	for (size_t i = 0; i < sizeof (kChevrons) / sizeof (kChevrons[0]); ++i)
		std::printf ("%s[%d,%d,%s]", i ? "," : "", kChevrons[i].x, kChevrons[i].y, kChevrons[i].left ? "true" : "false");
	std::printf ("],\n");

	std::printf ("  \"rules\": [");
	for (size_t i = 0; i < sizeof (kRules) / sizeof (kRules[0]); ++i)
		std::printf ("%s%s", i ? "," : "", rect (kRules[i]).c_str ());
	std::printf ("],\n");

	std::printf ("  \"captions\": [\n");
	for (int i = 0; i < kNumCaptions; ++i)
		std::printf ("    {\"text\": %s, \"style\": %s, \"align\": %s, \"rect\": %s}%s\n",
		             quoted (kCaptions[i].text).c_str (), styleName (kCaptions[i].style),
		             alignName (kCaptions[i].align), rect (kCaptions[i].r).c_str (), (i + 1 < kNumCaptions) ? "," : "");
	std::printf ("  ],\n");

	// ---- the controls, at their defaults ------------------------------------
	std::printf ("  \"controls\": [\n");
	for (int i = 0; i < kNumControls; ++i)
	{
		const Control& c = kControls[i];
		const double v = paramDef (c.tag).defaultNormalized ();
		std::string names = "[";
		std::string selector = "null";
		if (c.kind == Kind::Switch)
		{
			const int positions = paramDef (c.tag).stepCount + 1;
			const Selector g = selectorOf (c, positions);
			std::string angles = "[", boxes = "[";
			for (int p = 0; p < positions; ++p)
			{
				names += std::string (p ? "," : "") + quoted (launchPositionName (p));
				angles += std::string (p ? "," : "") + num (g.angle[p]);
				boxes += std::string (p ? "," : "") + box (selectorLabelBox (g, p, launchPositionName (p)));
			}
			selector = "{\"cx\": " + num (g.cx) + ", \"cy\": " + num (g.cy) + ", \"radius\": " + num (g.radius) +
			           ", \"angles\": " + angles + "], \"labelBoxes\": " + boxes + "], \"nameY\": " +
			           num (g.nameCentreY) + "}";
		}
		names += "]";
		std::string extra;
		if (c.kind == Kind::Step)
		{
			const int step = static_cast<int> (c.tag - kStep1);
			const double dis = paramDef (stepDisableParam (step)).defaultNormalized ();
			extra = ", \"number\": " + std::to_string (step + 1) + ", \"disabled\": " + (dis >= 0.5 ? "true" : "false");
		}
		if (c.tag == kMix)
			extra = ", \"ends\": [\"D1\", \"D2\"]";
		std::printf ("    {\"kind\": %s, \"accent\": %s, \"rect\": %s, \"diameter\": %d, \"bipolar\": %s, "
		             "\"reversed\": %s, \"value\": %.4f, \"text\": %s, \"label\": %s, \"names\": %s, \"selector\": %s%s}%s\n",
		             kindName (c.kind), accentName (c.accent), rect (controlRect (c)).c_str (), c.diameter,
		             c.bipolar ? "true" : "false", c.reversed ? "true" : "false", v,
		             quoted (readoutText (c.tag, v, false)).c_str (), quoted (c.label ? c.label : "").c_str (),
		             names.c_str (), selector.c_str (), extra.c_str (), (i + 1 < kNumControls) ? "," : "");
	}
	std::printf ("  ],\n");

	// ---- the lamps and the lines whose words change ---------------------------
	const InternalOf internal = [] (int drum, ParamID base) { return internalDefault (drum, base); };
	std::printf ("  \"lamps\": [\n");
	for (int drum = 1; drum <= 2; ++drum)
		std::printf ("    {\"rect\": %s, \"name\": \"SELF-OSC\", \"on\": %s},\n    {\"rect\": %s, \"name\": \"STRIKE\", "
		             "\"on\": false}%s\n",
		             rect (columnRect (Column::kSelfOscLamp, drum)).c_str (),
		             selfOscillating (internalDefault (drum, kResonance)) ? "true" : "false",
		             rect (columnRect (Column::kStrikeLamp, drum)).c_str (), drum == 1 ? "," : "");
	std::printf ("  ],\n");
	std::printf ("  \"lines\": [\n    %s,\n    %s,\n    %s,\n    %s,\n    %s\n  ],\n",
	             textLine (kEngineLine, engineLineText (rate), "engine", Align::Left).c_str (),
	             textLine (columnRect (Column::kVelocityLine, 1), velocityLineText (internal, 1), "info", Align::Centre).c_str (),
	             textLine (columnRect (Column::kVelocityLine, 2), velocityLineText (internal, 2), "info", Align::Centre).c_str (),
	             textLine (kMixGainsLine, mixGainsText (paramDef (kMix).toInternal (paramDef (kMix).defaultNormalized ())),
	                       "info", Align::Centre).c_str (),
	             textLine (kStrikeVelLine, lastStrikeText (-1.0), "info", Align::Centre).c_str ());

	// ---- the four screens, from the DSP's own trace functions ------------
	std::printf ("  \"screens\": [\n");
	for (int drum = 1; drum <= 2; ++drum)
	{
		const double cutoff = internalDefault (drum, kCutoff);
		const double k = internalDefault (drum, kResonance);
		const double oct = internalDefault (drum, kVcfAmount);
		const double sens = internalDefault (drum, kVcfVelocity);
		const double gain = internalDefault (drum, kVcaAmount);
		const double peakHz = cutoffWithEnv (cutoff, velocityScaled (oct, sens, 1.0), 1.0, rate);

		float knobs[kDisplayPoints], peak[kDisplayPoints];
		const double dotY = traceFilterResponse (cutoff, k, rate, kFilterMinHz, kFilterMaxHz, kFilterTopDb,
		                                         kFilterBottomDb, knobs, n);
		traceFilterResponse (peakHz, k, rate, kFilterMinHz, kFilterMaxHz, kFilterTopDb, kFilterBottomDb, peak, n);
		const double used = cutoffWithEnv (cutoff, 0.0, 0.0, rate);
		const double dotX = std::log (used / kFilterMinHz) / std::log (kFilterMaxHz / kFilterMinHz);

		ArSpec vcf, vca;
		vcf.attack = internalDefault (drum, kVcfAttack);  vcf.release = internalDefault (drum, kVcfRelease);
		vca.attack = internalDefault (drum, kVcaAttack);  vca.release = internalDefault (drum, kVcaRelease);
		float vcfC[kDisplayPoints], vcaC[kDisplayPoints];
		const double span = traceDrumEnvelopes (vcf, vca, vcfC, vcaC, n);
		const float vcfScale = static_cast<float> (std::min (1.0, std::fabs (oct) / kMaxEnvOctaves));
		for (int i = 0; i < n; ++i) { vcfC[i] *= vcfScale; vcaC[i] *= static_cast<float> (gain); }

		std::printf ("    {\"rect\": %s, \"accent\": \"filter\", \"companionAccent\": \"filter\", \"caption\": \"\", "
		             "\"annotation\": %s, \"marker\": [%.4f, %.4f], \"main\": %s, \"companion\": %s},\n",
		             rect (kScreens[filterScreenIndex (drum)]).c_str (), quoted (filterPeakText (peakHz)).c_str (),
		             dotX, dotY, curve (knobs, n).c_str (), curve (peak, n).c_str ());
		std::printf ("    {\"rect\": %s, \"accent\": \"amp\", \"companionAccent\": \"filter\", \"caption\": %s, "
		             "\"annotation\": %s, \"marker\": [-1, 0], \"main\": %s, \"companion\": %s}%s\n",
		             rect (kScreens[envScreenIndex (drum)]).c_str (), quoted (envCaptionText (oct)).c_str (),
		             quoted (envSpanText (span)).c_str (), curve (vcaC, n).c_str (), curve (vcfC, n).c_str (),
		             drum == 1 ? "," : "");
	}
	std::printf ("  ]\n");
	std::printf ("}\n");
	return 0;
}
