//------------------------------------------------------------------------
// FilterDrum - panel layout tests (Design 2.0)
//
// The layout is DATA (source/FilterDrumLayout.h), which is what makes it
// testable from a session that cannot run a host: nothing here draws,
// it checks the tables. Needs ONE header-only SDK file (vsttypes.h, via
// FilterDrumParams.h) and the parameter table:
//
//   c++ -std=c++17 -O2 -Isource -Iexternal/vst3sdk tests/LayoutTests.cpp
//       source/FilterDrumParams.cpp -o /tmp/layouttests && /tmp/layouttests
//
// Also here: the two output-parameter encodings the panel decodes
// (the playhead, and the STRIKE lamps' kStrikeOut), because they live in
// FilterDrumParams.h and this is the suite that can include it.
//------------------------------------------------------------------------

#include "FilterDrumLayout.h"
#include "FilterDrumParams.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace FilterDrum;
using namespace FilterDrum::Layout;
using Steinberg::Vst::ParamID;

namespace {

int gChecks = 0;
int gFailures = 0;

bool gQuiet = false;

void check (bool condition, const std::string& what)
{
	++gChecks;
	if (!condition)
	{
		++gFailures;
		if (!gQuiet)
			std::printf ("  FAIL  %s\n", what.c_str ());
	}
}

bool overlaps (const Rect& a, const Rect& b)
{
	return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

bool inside (const Rect& inner, const Rect& outer)
{
	return inner.left >= outer.left && inner.right <= outer.right &&
	       inner.top >= outer.top && inner.bottom <= outer.bottom;
}

std::string name (ParamID id)
{
	return isTableParam (id) ? std::string (paramDef (id).title) : ("id " + std::to_string (id));
}

/** The text band a knob's reading and label may use: its rectangle and
    four pixels either side (Panel::Knob draws them that wide). */
int textWidth (const Control& c) { return c.width + 8; }

/** THE WIDEST READING each control can show, written out by hand from
    readoutFor() in FilterDrumEditor.cpp. That function needs VSTGUI, so
    this suite cannot call it; the list is the next best thing, and
    a reading added there without a line here is what the "every knob
    has a widest reading" check below catches. */
const char* widestReading (ParamID id)
{
	int drum = 0;
	ParamID base = id;
	splitDrumParam (id, drum, base);
	switch (base)
	{
		case kCutoff:       return "20.00 kHz";
		case kResonance:    return "100%  K2.40";
		case kVcfAttack:
		case kVcaAttack:    return "1000 ms";
		case kVcfRelease:
		case kVcaRelease:   return "4000 ms";
		case kVcfAmount:    return "+6.0 oct";
		case kVcfVelocity:
		case kVcaAmount:
		case kVcaVelocity:  return "100%";
		case kNoiseLevel:   return "silent";
		case kMix:          return "D1 only";
		case kOutputTrim:   return "+12.0 dB";
		default:            return nullptr;
	}
}

Accent expectedAccent (ParamID id)
{
	int drum = 0;
	ParamID base = id;
	if (splitDrumParam (id, drum, base))
	{
		switch (base)
		{
			case kNoiseLevel:  return Accent::Noise;
			case kVcaAttack:
			case kVcaRelease:
			case kVcaAmount:
			case kVcaVelocity: return Accent::Amp;
			default:           return Accent::Filter;
		}
	}
	if (id == kMix || id == kOutputTrim)
		return Accent::Amp;
	return Accent::Seq;
}

const Section* sectionOf (const Rect& r)
{
	for (const auto& s : kSections)
		if (inside (r, s.r))
			return &s;
	return nullptr;
}

} // anonymous namespace

//------------------------------------------------------------------------
static void testEveryParameterOnce ()
{
	std::printf ("every parameter on the panel, once\n");

	std::vector<int> seen (kNumParams, 0);
	for (const auto& c : kControls)
	{
		check (isTableParam (c.tag), "control tag " + std::to_string (c.tag) + " is a table parameter");
		check (stepOfDisableParam (c.tag) < 0,
		       name (c.tag) + " is not a control of its own - its step pad carries it");
		if (isTableParam (c.tag))
			++seen[c.tag];
		// A STEP PAD CARRIES TWO PARAMETERS: its step (click) and that
		// step's disable (right-click). See StepPad.
		if (c.kind == Kind::Step)
			++seen[stepDisableParam (static_cast<int> (c.tag - kStep1))];
	}
	for (int i = 0; i < kNumParams; ++i)
		check (seen[i] == 1, name (static_cast<ParamID> (i)) + " appears exactly once (got "
		                         + std::to_string (seen[i]) + ")");

	check (kNumControls == kNumParams - 16,
	       "as many controls as parameters, less the sixteen disables the pads carry");
}

//------------------------------------------------------------------------
static void testGeometry ()
{
	std::printf ("nothing overlaps, everything fits\n");

	const Rect panel { 0, 0, kEditorWidth, kEditorHeight };

	// Sections: inside the panel, below the header, apart.
	for (int i = 0; i < kNumSections; ++i)
	{
		const auto& s = kSections[i];
		check (inside (s.r, panel), std::string (s.title) + " is inside the panel");
		check (s.r.top >= kHeaderHeight + 8, std::string (s.title) + " is below the header");
		for (int j = i + 1; j < kNumSections; ++j)
			check (!overlaps (s.r, kSections[j].r),
			       std::string (s.title) + " does not overlap " + kSections[j].title);
	}

	// Everything with a rectangle, with what it is called.
	struct Item { Rect r; std::string what; };
	std::vector<Item> items;
	for (const auto& c : kControls)
		items.push_back ({ controlRect (c), name (c.tag) });
	for (int drum = 1; drum <= 2; ++drum)
	{
		const std::string d = " " + std::to_string (drum);
		items.push_back ({ laneRect (Lane::kFilterScreen, laneTop (drum)), "filter screen" + d });
		items.push_back ({ laneRect (Lane::kEnvScreen, laneTop (drum)), "envelope screen" + d });
		items.push_back ({ laneRect (Lane::kSelfOscLamp, laneTop (drum)), "self-osc lamp" + d });
		items.push_back ({ laneRect (Lane::kVelocityLine, laneTop (drum)), "velocity line" + d });
	}
	for (const auto& cap : kCaptions)
		items.push_back ({ cap.r, std::string ("caption ") + cap.text });
	items.push_back ({ kMixGainsLine, "mix gains line" });
	items.push_back ({ kStrikeLamp1, "strike lamp 1" });
	items.push_back ({ kStrikeLamp2, "strike lamp 2" });
	items.push_back ({ kStrikeVelLine, "strike velocity line" });

	for (size_t i = 0; i < items.size (); ++i)
	{
		const Section* s = sectionOf (items[i].r);
		check (s != nullptr, items[i].what + " is inside one section");
		// The section's title row is its own.
		// Below the section's title row - or, if it shares that row (the
		// velocity lines, LAUNCH ON), clear of the title and subtitle,
		// at generous widths for Helvetica Neue bold 13 and regular 12.
		if (s)
		{
			const bool belowTitle = items[i].r.top >= s->r.top + kSectionPadTop + kSectionTitleH;
			const int titleEnd = s->r.left + kSectionPadX + 30
			                     + static_cast<int> (std::strlen (s->title)) * 10
			                     + (s->subtitle && *s->subtitle ? 14 + static_cast<int> (std::strlen (s->subtitle)) * 6 : 0);
			check (belowTitle || items[i].r.left >= titleEnd,
			       items[i].what + " is clear of its section's title row");
		}
		for (size_t j = i + 1; j < items.size (); ++j)
			check (!overlaps (items[i].r, items[j].r), items[i].what + " does not overlap " + items[j].what);
	}

	// Rules and chevrons sit inside a section and clear of every control.
	for (const auto& r : kRules)
	{
		check (sectionOf (r) != nullptr, "a rule is inside a section");
		for (const auto& c : kControls)
			check (!overlaps (r, controlRect (c)), "a rule clears " + name (c.tag));
	}
	for (const auto& p : kChevrons)
	{
		const Rect r { p.x - 4, p.y - 9, p.x + 4, p.y + 9 };
		check (sectionOf (r) != nullptr, "a chevron is inside a section");
		for (const auto& c : kControls)
			check (!overlaps (r, controlRect (c)), "a chevron clears " + name (c.tag));
	}
}

//------------------------------------------------------------------------
static void testColoursAndLanes ()
{
	std::printf ("colours, and the two lanes the same\n");

	for (const auto& c : kControls)
	{
		check (c.accent == expectedAccent (c.tag), name (c.tag) + " is in its part's colour");

		if (c.kind == Kind::Knob)
		{
			check (c.diameter + 4 <= c.width, name (c.tag) + "'s knob fits its width");
			const char* widest = widestReading (c.tag);
			check (widest != nullptr, name (c.tag) + " has a widest reading listed");
			if (widest)
				check (static_cast<int> (std::strlen (widest)) * kValueCharWidth <= textWidth (c),
				       name (c.tag) + "'s widest reading \"" + widest + "\" fits");
		}
		if (c.label && *c.label)
			check (static_cast<int> (std::strlen (c.label)) * kLabelCharWidth <= textWidth (c) + 4
			         || c.kind == Kind::Toggle || c.kind == Kind::Switch,
			       name (c.tag) + "'s label fits");
	}

	// DRUM 2 IS DRUM 1, kLaneOffset lower: same x, same size, same kind.
	for (const auto& c : kControls)
	{
		int drum = 0;
		ParamID base = c.tag;
		if (!splitDrumParam (c.tag, drum, base) || drum != 1)
			continue;
		const Control* twin = nullptr;
		for (const auto& d : kControls)
			if (d.tag == c.tag + kDrum2Offset)
				twin = &d;
		check (twin != nullptr, name (c.tag) + " has a drum 2 twin");
		if (twin)
		{
			check (twin->x == c.x && twin->y == c.y + kLaneOffset && twin->width == c.width &&
			       twin->diameter == c.diameter && twin->bipolar == c.bipolar && twin->accent == c.accent,
			       name (c.tag) + " and its twin are the same control, one lane apart");
			check (std::strcmp (twin->label, c.label) == 0, name (c.tag) + " and its twin share a label");
		}
	}

	// The VCF amount is bipolar (it is signed); the mix is bipolar and
	// reversed (D1 on the left); nothing else is either.
	for (const auto& c : kControls)
	{
		int drum = 0;
		ParamID base = c.tag;
		splitDrumParam (c.tag, drum, base);
		const bool shouldBip = (base == kVcfAmount) || (c.tag == kMix);
		check (c.bipolar == shouldBip, name (c.tag) + (shouldBip ? " is" : " is not") + " bipolar");
		check (c.reversed == (c.tag == kMix), name (c.tag) + (c.tag == kMix ? " is" : " is not") + " reversed");
	}
}

//------------------------------------------------------------------------
static void testEncodings ()
{
	std::printf ("output parameter encodings\n");

	// THE STRIKE: velocity round-trips to the nearest MIDI step...
	for (int v = 0; v <= 127; ++v)
		for (unsigned serial : { 1u, 2u })
		{
			const double n = strikeToNormalized (v / 127.0, serial);
			check (n > 0.0 && n <= 1.0, "strike value in range");
			check (std::fabs (strikeVelocityFromNormalized (n) - v / 127.0) < 1e-9,
			       "strike velocity " + std::to_string (v) + " round-trips");
		}

	// ...two identical hits in a row are two DIFFERENT values, so a host
	// that forwards only changes still lights the lamp twice...
	for (unsigned serial = 1; serial < 6; ++serial)
		check (strikeToNormalized (1.0, serial) != strikeToNormalized (1.0, serial + 1),
		       "consecutive identical hits differ");

	// ...and nothing has been struck before anything has.
	check (strikeVelocityFromNormalized (0.0) < 0.0, "the value at rest is not a strike");
	check (strikeToNormalized (0.0, 2) > 0.0, "a velocity-0 hit is still not the value at rest");

	// THE PLAYHEAD, which the pads decode.
	for (int step = -1; step < 16; ++step)
		check (playheadFromNormalized (playheadToNormalized (step)) == step,
		       "playhead " + std::to_string (step) + " round-trips");
	check (kStrikeOut != kPlayheadOut && kStrikeOut != kBypass, "the strike has its own id");
	check (!isTableParam (kStrikeOut), "and it is not in the table, so it is never saved");
}

//------------------------------------------------------------------------
int main ()
{
	std::printf ("FilterDrum layout tests\n");
	std::printf ("-----------------------\n");

	testEveryParameterOnce ();
	testGeometry ();
	testColoursAndLanes ();
	testEncodings ();

	// THE HARNESS CAN FAIL: a deliberately overlapping pair must be seen.
	{
		const int before = gFailures;
		gQuiet = true;
		check (!overlaps (Rect { 0, 0, 10, 10 }, Rect { 5, 5, 15, 15 }), "(expected failure)");
		gQuiet = false;
		const bool sawIt = gFailures == before + 1;
		gFailures = before;
		--gChecks;
		check (sawIt, "the harness reports a failure when there is one");
	}

	std::printf ("-----------------------\n");
	std::printf ("%d checks, %d failures\n", gChecks, gFailures);
	return (gFailures == 0) ? 0 : 1;
}
