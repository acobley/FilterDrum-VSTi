//------------------------------------------------------------------------
// FilterDrum - panel layout tests (the black panel, Mirror Columns)
//
// The layout is DATA (source/FilterDrumLayout.h), which is what makes it
// testable from a session that cannot run a host: nothing here draws,
// it checks the tables. Needs ONE header-only SDK file (vsttypes.h, via
// FilterDrumParams.h), the parameter table, the readouts and what they
// call:
//
//   c++ -std=c++17 -O2 -Isource -Iexternal/vst3sdk tests/LayoutTests.cpp
//       source/FilterDrumParams.cpp source/FilterDrumReadouts.cpp
//       source/FilterDrumTransport.cpp -o /tmp/layouttests && /tmp/layouttests
//
// WHAT FITS IS MEASURED IN THE LETTERING. Every reading a knob can show
// is made by readoutText() - the editor's own function - at 1001 points
// across its range, and the widest is held to the knob's rectangle in the
// 70s sci-fi face's own metrics (Lettering::width). So a format changed
// in FilterDrumReadouts.cpp that no longer fits fails here, with no list
// of widest readings to keep by hand.
//
// THE WEAR (PanelWear.h, R2Synth's "Studio Worn"): the same marks every
// time, on the panel, off the glass, and too faint to hide a label.
//
// Also here: the two output-parameter encodings the panel decodes (the
// playhead, and the STRIKE lamps' kStrikeOut), because they live in
// FilterDrumParams.h and this is the suite that can include it.
//------------------------------------------------------------------------

#include "FilterDrumDsp.h"
#include "FilterDrumLayout.h"
#include "FilterDrumParams.h"
#include "FilterDrumReadouts.h"
#include "FilterDrumTransport.h"
#include "PanelWear.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

bool boxInside (const Box& b, const Rect& r)
{
	return b.left >= r.left && b.right <= r.right && b.top >= r.top && b.bottom <= r.bottom;
}

bool boxesOverlap (const Box& a, const Box& b)
{
	return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

Box toBox (const Rect& r) { return { double (r.left), double (r.top), double (r.right), double (r.bottom) }; }

std::string name (ParamID id)
{
	return isTableParam (id) ? std::string (paramDef (id).title) : ("id " + std::to_string (id));
}

std::string px (double v)
{
	char t[24];
	std::snprintf (t, sizeof (t), "%.1f", v);
	return t;
}

/** The label a control is drawn with - the editor's shortLabelFor()
    when the table gives none. Every control on this panel names its
    own. */
std::string labelOf (const Control& c)
{
	return c.label ? std::string (c.label) : std::string ();
}

/** THE WIDEST READING `tag` can show, by readoutText() at 1001 points. */
double widestReading (ParamID tag, std::string& widest)
{
	double w = 0.0;
	for (int i = 0; i <= 1000; ++i)
		for (bool running : { false, true })
		{
			const std::string t = readoutText (tag, i / 1000.0, running);
			const double tw = Lettering::width (t, kTextValue);
			if (tw > w)
			{
				w = tw;
				widest = t;
			}
		}
	return w;
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

/** Does a box keep clear of a selector's knob and its printed marks -
    a circle `radius` + 3 round its centre? */
bool clearsHub (const Box& b, const Selector& g)
{
	const double nx = std::clamp (g.cx, b.left, b.right);
	const double ny = std::clamp (g.cy, b.top, b.bottom);
	const double r = g.radius + 3.0;
	return (nx - g.cx) * (nx - g.cx) + (ny - g.cy) * (ny - g.cy) > r * r;
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
	for (int i = 0; i < static_cast<int> (kNumParams); ++i)
		check (seen[i] == 1, name (static_cast<ParamID> (i)) + " appears exactly once (got "
		                         + std::to_string (seen[i]) + ")");

	check (kNumControls == kNumParams - 16,
	       "as many controls as parameters, less the sixteen disables the pads carry");
}

//------------------------------------------------------------------------
static void testGeometry ()
{
	std::printf ("nothing overlaps, everything is inside its section\n");

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
	for (int i = 0; i < kNumScreens; ++i)
		items.push_back ({ bezelOf (kScreens[i]), "screen " + std::to_string (i) });
	for (int drum = 1; drum <= 2; ++drum)
	{
		const std::string d = " " + std::to_string (drum);
		items.push_back ({ columnRect (Column::kSelfOscLamp, drum), "self-osc lamp" + d });
		items.push_back ({ columnRect (Column::kStrikeLamp, drum), "strike lamp" + d });
		items.push_back ({ columnRect (Column::kVelocityLine, drum), "velocity line" + d });
	}
	for (const auto& cap : kCaptions)
		items.push_back ({ cap.r, std::string ("caption ") + cap.text });
	items.push_back ({ kMixGainsLine, "mix gains line" });
	items.push_back ({ kStrikeVelLine, "last-strike line" });

	for (size_t i = 0; i < items.size (); ++i)
	{
		const Section* s = sectionOf (items[i].r);
		check (s != nullptr, items[i].what + " is inside one section");
		// Below the section's title row - or, if it shares that row (the
		// strike lamps, the sequencer's note), clear of the badge, the
		// title box and the subtitle.
		if (s)
		{
			const bool belowTitle = items[i].r.top >= s->r.top + kSectionPadTop + kSectionTitleH + 4;
			double titleEnd = titleBox (*s).right;
			if (s->subtitle && *s->subtitle)
				titleEnd = subtitleLeft (*s) + Lettering::width (s->subtitle, kTextSubtitle);
			check (belowTitle || items[i].r.left >= titleEnd + 8,
			       items[i].what + " is clear of its section's title row");
		}
		for (size_t j = i + 1; j < items.size (); ++j)
			check (!overlaps (items[i].r, items[j].r), items[i].what + " does not overlap " + items[j].what);
	}

	// THE BLOCKS: inside their column, apart, round the column's knobs,
	// clear of the screens - and their captions cut into the top edge
	// clear of every control.
	for (size_t i = 0; i < sizeof (kBlocks) / sizeof (kBlocks[0]); ++i)
	{
		const Block& b = kBlocks[i];
		const Section* s = sectionOf (b.r);
		check (s != nullptr && s->badge && *s->badge, std::string ("block ") + b.caption + " is inside a drum column");
		for (size_t j = i + 1; j < sizeof (kBlocks) / sizeof (kBlocks[0]); ++j)
			check (!overlaps (b.r, kBlocks[j].r), std::string ("block ") + b.caption + " clears the next");
		for (const auto& scr : kScreens)
			check (!overlaps (b.r, bezelOf (scr)), std::string ("block ") + b.caption + " clears the screens");

		const double left = b.r.left + kBlockCaptionInset;
		const double w = Lettering::width (b.caption, kTextBlock);
		const double h = kTextBlock.height ();
		const Box cut { left - kBlockCaptionGap, b.r.top - h / 2.0, left + w + kBlockCaptionGap, b.r.top + h / 2.0 };
		check (cut.right <= b.r.right - 10, std::string ("block caption ") + b.caption + " fits its block");
		for (const auto& c : kControls)
			check (!boxesOverlap (cut, toBox (controlRect (c))),
			       std::string ("block caption ") + b.caption + " clears " + name (c.tag));
		for (int drum = 1; drum <= 2; ++drum)
			check (!boxesOverlap (cut, toBox (columnRect (Column::kSelfOscLamp, drum))),
			       std::string ("block caption ") + b.caption + " clears the self-osc lamps");
	}
	for (const auto& c : kControls)
	{
		int drum = 0;
		ParamID base = c.tag;
		if (!splitDrumParam (c.tag, drum, base))
			continue;
		int in = 0;
		for (const auto& b : kBlocks)
			in += inside (controlRect (c), b.r) ? 1 : 0;
		check (in == 1, name (c.tag) + " is inside exactly one block of its column");
	}

	// Rules sit inside a section and clear of every control.
	for (const auto& r : kRules)
	{
		check (sectionOf (r) != nullptr, "a rule is inside a section");
		for (const auto& c : kControls)
			check (!overlaps (r, controlRect (c)), "a rule clears " + name (c.tag));
	}

	// Chevrons: inside one section, or in the gap between two - touching
	// none - and clear of every control.
	for (const auto& p : kChevrons)
	{
		const Rect r { p.x - 5, p.y - 10, p.x + 5, p.y + 10 };
		check (inside (r, panel), "a chevron is on the panel");
		bool inOne = sectionOf (r) != nullptr, touchesAny = false;
		for (const auto& s : kSections)
			touchesAny = touchesAny || overlaps (r, s.r);
		check (inOne || !touchesAny, "a chevron is inside a section or between two, not across an edge");
		for (const auto& c : kControls)
			check (!overlaps (r, controlRect (c)), "a chevron clears " + name (c.tag));
	}
	// The two chevrons into the spine point at it.
	check (kChevrons[2].x > kDrum1Rect.right && kChevrons[2].x < kMixRect.left && !kChevrons[2].left,
	       "drum 1's VCA points right, into the spine");
	check (kChevrons[3].x > kMixRect.right && kChevrons[3].x < kDrum2Rect.left && kChevrons[3].left,
	       "drum 2's VCA points left, into the spine");
}

//------------------------------------------------------------------------
static void testLettering ()
{
	std::printf ("every word fits, in the lettering\n");

	// ---- the header -----------------------------------------------------
	const double wordmark = kWordmark.left + Lettering::width ("FILTER", kTextWordmark) + kWordmarkGap +
	                        Lettering::width ("DRUM", kTextWordmark);
	check (wordmark <= kWordmark.right, "the wordmark fits its place (" + px (wordmark) + ")");
	check (kTextWordmark.height () <= kHeaderHeight - 20, "the wordmark fits the header's height");
	for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
		check (Lettering::width (engineLineText (rate), kTextEngine) <= kEngineLine.width (),
		       "the engine line fits at " + px (rate));

	// ---- section titles ---------------------------------------------------
	for (const auto& s : kSections)
	{
		const Box b = titleBox (s);
		check (boxInside (b, s.r), std::string (s.title) + "'s title box is inside its section");
		check (b.top >= s.r.top + 4, std::string (s.title) + "'s title box is below its top edge");
		if (s.subtitle && *s.subtitle)
			check (subtitleLeft (s) + Lettering::width (s.subtitle, kTextSubtitle) <= s.r.right - 20,
			       std::string (s.title) + "'s subtitle fits");
		if (s.badge && *s.badge)
		{
			double bx = 0.0, by = 0.0;
			badgeCentre (s, bx, by);
			check (bx - kBadgeRadius >= s.r.left + 4 && bx + kBadgeRadius < b.left,
			       std::string (s.title) + "'s badge is before its title");
		}
		if (s.centred)
			check (std::fabs ((b.left + b.right) / 2.0 - (s.r.left + s.r.right) / 2.0) < 0.5,
			       std::string (s.title) + "'s title is centred");
	}

	// ---- knobs: the widest reading, and the label -------------------------
	for (const auto& c : kControls)
	{
		const std::string label = labelOf (c);
		switch (c.kind)
		{
			case Kind::Knob:
			{
				check (c.diameter + 4 <= c.width, name (c.tag) + "'s knob fits its width");
				std::string widest;
				const double w = widestReading (c.tag, widest);
				check (w <= c.width, name (c.tag) + "'s widest reading \"" + widest + "\" (" + px (w) + ") fits " +
				                         std::to_string (c.width));
				check (Lettering::width (label, kTextLabel) <= c.width, name (c.tag) + "'s label fits");
				break;
			}
			case Kind::Switch:
			{
				const int n = paramDef (c.tag).stepCount + 1;
				check (n <= kMaxPositions, name (c.tag) + " has positions the selector can print");
				const Selector g = selectorOf (c, n);
				const Rect r = controlRect (c);
				std::vector<Box> boxes;
				for (int p = 0; p < n; ++p)
				{
					const std::string pos = launchPositionName (p);
					const Box b = selectorLabelBox (g, p, pos);
					check (boxInside (b, r), name (c.tag) + "'s position " + pos + " is printed inside it");
					check (clearsHub (b, g), name (c.tag) + "'s position " + pos + " clears the knob");
					for (const auto& other : boxes)
						check (!boxesOverlap (b, other), name (c.tag) + "'s position " + pos + " clears its neighbour");
					boxes.push_back (b);
				}
				check (Lettering::width (label, kTextLabel) <= c.width, name (c.tag) + "'s name fits");
				check (g.cy + g.radius + 8.0 < g.nameCentreY - kTextLabel.height () / 2.0,
				       name (c.tag) + "'s name is under its knob");
				check (n == kLaunchDivisionCount, "Launch On has a position for every division");
				break;
			}
			case Kind::Toggle:
			{
				const double nameRight = c.x + 30.0 + Lettering::width (label, kTextButton);
				check (nameRight <= c.x + c.width - 6, name (c.tag) + "'s name fits its button");
				for (double v : { 0.0, 1.0 })
					for (bool running : { false, true })
						check (Lettering::width (readoutText (c.tag, v, running), kTextInfo) <= c.width,
						       name (c.tag) + "'s status fits");
				check (34 + 16 <= controlRect (c).height (), name (c.tag) + " has room for its status under it");
				break;
			}
			case Kind::Step:
				check (Lettering::width ("16", kTextStep) <= c.width - 8, "a step's number fits its key");
				check (kStepKeyBottom + 4 < kStepNumberY && kStepLedY + 4 < kStepKeyTop &&
				       kStepNumberY + 6 <= kStepHeight, "LED, key and number stack inside a step");
				break;
		}
	}

	// ---- the lamps: dot and name inside the rectangle -----------------------
	for (int drum = 1; drum <= 2; ++drum)
		for (const auto& lamp : { std::make_pair (Column::kSelfOscLamp, "SELF-OSC"),
		                          std::make_pair (Column::kStrikeLamp, "STRIKE") })
		{
			const Rect r = columnRect (lamp.first, drum);
			const double dotLeft = r.right - Lettering::width (lamp.second, kTextLabel) - 12.0 - 8.0;
			check (dotLeft >= r.left, std::string (lamp.second) + " lamp " + std::to_string (drum) + " fits");
		}

	// ---- the lines whose words change, at their widest ----------------------
	{
		double widest = 0.0;
		std::string worst;
		for (double oct : { -6.0, -3.6, 0.0, 3.6, 6.0 })
			for (double gain : { 0.0, 0.5, 1.0 })
				for (double sens : { 0.0, 0.5, 1.0 })
				{
					const InternalOf f = [&] (int, ParamID base) {
						switch (base)
						{
							case kVcfAmount:   return oct;
							case kVcaAmount:   return gain;
							case kVcfVelocity:
							case kVcaVelocity: return sens;
							default:           return 0.0;
						}
					};
					const std::string t = velocityLineText (f, 1);
					const double w = Lettering::width (t, kTextInfo);
					if (w > widest) { widest = w; worst = t; }
				}
		check (widest <= Column::kVelocityLine.width (),
		       "the widest velocity line \"" + worst + "\" (" + px (widest) + ") fits its column");
	}
	{
		double widest = 0.0;
		for (int i = 0; i <= 100; ++i)
			widest = std::max (widest, Lettering::width (mixGainsText (i / 100.0), kTextInfo));
		check (widest <= kMixGainsLine.width (), "the mix gains line fits the spine (" + px (widest) + ")");
	}
	check (Lettering::width (lastStrikeText (1.0), kTextInfo) <= kStrikeVelLine.width () &&
	       Lettering::width (lastStrikeText (-1.0), kTextInfo) <= kStrikeVelLine.width (),
	       "the last-strike line fits the spine");

	// ---- the screens' corners -------------------------------------------------
	for (int drum = 1; drum <= 2; ++drum)
	{
		const Rect f = kScreens[filterScreenIndex (drum)];
		const Rect e = kScreens[envScreenIndex (drum)];
		double peak = 0.0;
		for (double hz : { 20.0, 999.0, 9999.0, 21600.0, 86400.0 })
			peak = std::max (peak, Lettering::width (filterPeakText (hz), kTextScreen));
		check (16.0 + peak <= f.width (), "the filter screen's peak fits");
		double head = 0.0;
		for (double oct : { -6.0, 6.0 })
			for (double span : { 0.0123, 0.999, 9.99 })
				head = std::max (head, Lettering::width (envCaptionText (oct), kTextScreen) +
				                           Lettering::width (envSpanText (span), kTextScreen));
		check (16.0 + 12.0 + head <= e.width (), "the envelope screen's caption and span fit side by side");
	}

	// ---- the fixed captions -------------------------------------------------
	for (const auto& c : kCaptions)
	{
		const Lettering::Style& st = (c.style == CaptionStyle::Note) ? kTextNote
		                           : (c.style == CaptionStyle::Block) ? kTextBlock : kTextLabel;
		check (Lettering::width (c.text, st) <= c.r.width (), std::string ("caption ") + c.text + " fits");
		check (st.height () <= c.r.height () + 1, std::string ("caption ") + c.text + " fits its height");
	}

	// ---- the face itself ------------------------------------------------------
	for (char c = 'A'; c <= 'Z'; ++c)
		check (Lettering::glyphFor (static_cast<uint32_t> (c)) != nullptr, std::string ("the face has ") + c);
	for (char c = '0'; c <= '9'; ++c)
		check (Lettering::glyphFor (static_cast<uint32_t> (c)) != nullptr, std::string ("the face has ") + c);
	for (uint32_t c = 0; c < 0x2600; ++c)
		if (const Lettering::Glyph* g = Lettering::glyphFor (c))
		{
			const auto cmds = Lettering::parse (g->path);
			if (cmds.empty () || cmds.front ().op != 'M')
				check (false, "glyph U+" + std::to_string (c) + " parses");
			for (const auto& k : cmds)
				if (k.x < -1.0 || k.x > 6.0 || k.y < -1.0 || k.y > 7.5)
				{
					check (false, "glyph U+" + std::to_string (c) + " stays on its grid");
					break;
				}
		}
	// EVERY CHARACTER THE PANEL PRINTS has a glyph - no dots for letters.
	auto allDrawn = [] (const std::string& t) {
		for (uint32_t c : Lettering::codePoints (t))
			if (c != ' ' && Lettering::glyphFor (c) == nullptr)
				return false;
		return true;
	};
	for (const auto& s : kSections)
		check (allDrawn (s.title) && allDrawn (s.subtitle), std::string (s.title) + ": every character has a glyph");
	for (const auto& b : kBlocks)
		check (allDrawn (b.caption), std::string (b.caption) + ": every character has a glyph");
	for (const auto& c : kCaptions)
		check (allDrawn (c.text), std::string (c.text) + ": every character has a glyph");
	for (const auto& c : kControls)
		for (int i = 0; i <= 10; ++i)
			check (allDrawn (readoutText (c.tag, i / 10.0, false)) && allDrawn (labelOf (c)),
			       name (c.tag) + ": every character of its reading has a glyph");
	check (allDrawn (engineLineText (48000.0)) && allDrawn (mixGainsText (0.5)) && allDrawn (lastStrikeText (1.0)) &&
	       allDrawn (filterPeakText (9700.0)) && allDrawn (envCaptionText (-3.6)),
	       "the changing lines: every character has a glyph");

	check (std::fabs (Lettering::width ("SAW", kTextLabel) - 18.85 * 6.1 / 6.0) < 1e-9, "SAW is 19.2 px wide");
}

//------------------------------------------------------------------------
static void testColoursAndColumns ()
{
	std::printf ("colours, and the two columns the same\n");

	for (const auto& c : kControls)
		check (c.accent == expectedAccent (c.tag), name (c.tag) + " is in its part's colour");

	// DRUM 2 IS DRUM 1, kColumnOffset to the right: same y, same size,
	// same kind - every control level with its twin.
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
			check (twin->x == c.x + kColumnOffset && twin->y == c.y && twin->width == c.width &&
			       twin->diameter == c.diameter && twin->bipolar == c.bipolar && twin->accent == c.accent &&
			       twin->kind == c.kind,
			       name (c.tag) + " and its twin are the same control, one column apart");
			check (std::strcmp (twin->label, c.label) == 0, name (c.tag) + " and its twin share a label");
		}
	}
	check (kDrum2Rect.left == kDrum1Rect.left + kColumnOffset && kDrum2Rect.top == kDrum1Rect.top &&
	       kDrum2Rect.right == kDrum1Rect.right + kColumnOffset && kDrum2Rect.bottom == kDrum1Rect.bottom,
	       "the columns are the same box, one column apart");
	check (kScreens[2].left == kScreens[0].left + kColumnOffset && kScreens[3].left == kScreens[1].left + kColumnOffset,
	       "the screens likewise");

	// THE MIX SITS BETWEEN THE DRUMS: centred in the spine, left of it is
	// drum 1.
	for (const auto& c : kControls)
		if (c.tag == kMix)
		{
			const Rect r = controlRect (c);
			check (r.left + r.right == kMixRect.left + kMixRect.right, "the mix knob is centred between the drums");
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
static void testSelectorClicks ()
{
	std::printf ("the selector picks what it shows\n");

	for (const auto& c : kControls)
	{
		if (c.kind != Kind::Switch)
			continue;
		const int n = paramDef (c.tag).stepCount + 1;
		const Selector g = selectorOf (c, n);
		check (selectorPositionAt (g, g.cx, g.cy) == -1, "a click on the knob steps on");
		for (int p = 0; p < n; ++p)
		{
			const Box b = selectorLabelBox (g, p, launchPositionName (p));
			check (selectorPositionAt (g, (b.left + b.right) / 2.0, (b.top + b.bottom) / 2.0) == p,
			       "a click on " + launchPositionName (p) + " picks it");
			if (p > 0)
				check (g.angle[p] > g.angle[p - 1], "the positions run clockwise");
		}
		check (std::fabs (g.angle[0] + g.angle[n - 1]) < 1e-9, "the positions are symmetric about the top");
	}
}

//------------------------------------------------------------------------
static void testWear ()
{
	std::printf ("the wear\n");

	using namespace Wear;
	const PanelWear a = panelWear (), b = panelWear ();
	bool same = a.specks.size () == b.specks.size () && a.scratches.size () == b.scratches.size () &&
	            a.haze.size () == b.haze.size ();
	for (size_t i = 0; same && i < a.specks.size (); ++i)
		same = a.specks[i].x == b.specks[i].x && a.specks[i].y == b.specks[i].y && a.specks[i].alpha == b.specks[i].alpha;
	check (same, "the wear is the same every time it is made");
	check (a.haze.size () >= 50 && a.specks.size () >= 500 && a.scratches.size () >= 20 && !a.drifts.empty () &&
	       !a.fibres.empty (), "the panel has haze, drifts, specks, scratches and fibres");
	int knobsAndSelectors = 0;
	for (const auto& c : kControls)
		knobsAndSelectors += (c.kind == Kind::Knob || c.kind == Kind::Switch) ? 1 : 0;
	check (static_cast<int> (a.rings.size ()) == knobsAndSelectors, "every knob and the selector has dust round its foot");

	Random r1 (42), r2 (42);
	bool repeat = true;
	for (int i = 0; i < 100; ++i)
		repeat = repeat && r1.next () == r2.next ();
	check (repeat && Random (43).next () != Random (42).next (), "the random numbers repeat for a seed, and only for it");

	const Rect panel { 0, 0, kEditorWidth, kEditorHeight };
	auto onPanel = [&] (double x, double y) { return x >= 0 && y >= 0 && x <= kEditorWidth && y <= kEditorHeight; };
	auto onGlass = [&] (double x, double y, double pad) {
		for (const auto& s : kScreens)
		{
			const Rect bz = bezelOf (s);
			if (x >= bz.left - pad && x <= bz.right + pad && y >= bz.top - pad && y <= bz.bottom + pad)
				return true;
		}
		return false;
	};

	bool hazeOk = true, specksOk = true, linesOk = true, faint = true, driftsOk = true;
	for (const auto& h : a.haze)
		hazeOk = hazeOk && h.alpha <= kMaxHazeAlpha && h.r > 0;
	for (const auto& d : a.drifts)
		driftsOk = driftsOk && inside (d.r, panel) && d.alpha <= kMaxHazeAlpha &&
		           (std::abs (d.fromX) + std::abs (d.fromY)) == 1;
	for (const auto& f : a.rings)
		hazeOk = hazeOk && f.alpha <= 32 && f.inner < f.peak && f.peak < f.outer;
	for (const auto& sp : a.specks)
	{
		specksOk = specksOk && onPanel (sp.x, sp.y);
		for (const auto& s : kScreens)
			specksOk = specksOk && !(sp.x > s.left && sp.x < s.right && sp.y > s.top && sp.y < s.bottom);
		faint = faint && sp.alpha <= kMaxMarkAlpha && sp.r <= 0.7;
	}
	for (const auto* set : { &a.scratches, &a.fibres })
		for (const auto& l : *set)
		{
			faint = faint && l.alpha <= kMaxMarkAlpha && l.width <= 0.5;
			linesOk = linesOk && l.pts.size () >= 2;
			for (const auto& q : l.pts)
				linesOk = linesOk && onPanel (q.x, q.y) && !onGlass (q.x, q.y, 1.0);
		}
	check (hazeOk, "the haze and the foot rings are faint (alpha <= " + std::to_string (kMaxHazeAlpha) + ")");
	check (driftsOk, "every drift is on the panel, faint, and fades from one edge");
	check (specksOk, "every speck is on the panel and none on a screen's glass");
	check (linesOk, "every scratch and fibre is on the panel and stops short of the screens");
	check (faint, "no speck, scratch or fibre is opaque enough to hide a label");

	bool knobsOk = true, distinct = false;
	const KnobWear first = knobWear (1);
	for (int i = 0; i < kNumControls; ++i)
	{
		if (kControls[i].kind == Kind::Knob)
		{
			const KnobWear k = knobWear (i);
			knobsOk = knobsOk && !k.scuffs.empty () && !k.grit.empty () && k.sheen > 0 && k.sheen <= kMaxHazeAlpha;
			for (const auto& arc : k.scuffs)
				knobsOk = knobsOk && arc.radius > 0 && arc.radius <= 0.95 && arc.span > 0 && arc.span < 180 &&
				          arc.alpha <= kMaxMarkAlpha;
			for (const auto& g : k.grit)
				knobsOk = knobsOk && g.radius > 0 && g.radius < 1.0 && g.alpha <= kMaxMarkAlpha;
			if (i != 1 && !k.scuffs.empty () && k.scuffs[0].start != first.scuffs[0].start)
				distinct = true;
		}
		else if (kControls[i].kind == Kind::Switch)
			for (const auto& g : selectorGrit (i))
				knobsOk = knobsOk && g.radius > 0 && g.radius <= 1.0 && g.alpha <= kMaxMarkAlpha;
	}
	check (knobsOk, "every knob's marks are on its cap or in its knurling, and faint");
	check (distinct, "every knob wears differently");
	check (controlIndex (kCutoff) >= 0 && kControls[controlIndex (kCutoff)].tag == kCutoff && controlIndex (999999) == -1,
	       "a control's wear is found by its tag");

	int prints = 0;
	for (int i = 0; i < kNumScreens; ++i)
	{
		const GlassWear g = glassWear (i);
		const Rect s = kScreens[i];
		auto on = [&] (double x, double y) { return x >= s.left && x <= s.right && y >= s.top && y <= s.bottom; };
		bool ok = !g.dust.empty ();
		for (const auto& d : g.dust)
			ok = ok && on (d.x, d.y) && d.alpha <= kMaxMarkAlpha;
		for (const auto& l : g.fibres)
			for (const auto& q : l.pts)
				ok = ok && on (q.x, q.y);
		for (const auto& p : g.prints)
		{
			++prints;
			for (const auto& ridge : p.ridges)
			{
				ok = ok && ridge.alpha <= kMaxPrintAlpha;
				for (const auto& q : ridge.pts)
					ok = ok && on (q.x, q.y);
			}
		}
		check (ok, "screen " + std::to_string (i) + ": its glass's marks are on its glass, and faint");
	}
	check (prints == 2, "two fingerprints: drum 1's filter screen and drum 2's envelope screen");
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
static void testReadouts ()
{
	std::printf ("the readings\n");

	// The formats the panel shows, at the table's defaults - the ones in
	// Designs/FilterDrum-Polivoks-3-MirrorColumns.png.
	auto at = [] (ParamID id) { return readoutText (id, paramDef (id).defaultNormalized ()); };
	check (readoutText (kSeqRun, 0.0) == "off" && readoutText (kSeqRun, 1.0) == "armed" &&
	       readoutText (kSeqRun, 1.0, true) == "running", "Run reads off / armed / running");
	check (launchPositionName (0) == "Bar" && launchPositionName (4) == "1/16", "Launch On reads Bar .. 1/16");
	check (readoutText (kNoiseLevel, 0.0) == "silent", "noise at zero says the drum is silent");
	check (gainText (0.7071) == ".71" && gainText (1.0) == "1.00", "gains under one drop the zero");
	check (mixGainsText (0.5) == ".71  /  .71   constant power", "the mix line at the middle");
	check (filterPeakText (9700.0) == "peak 9.7k" && filterPeakText (800.0) == "peak 800 Hz", "the peak reads in k");
	check (lastStrikeText (-1.0) == "last strike  -" && lastStrikeText (1.0) == "last strike  vel 127",
	       "the last strike");
	check (!at (kCutoff).empty () && !at (kResonance).empty (), "every knob has a reading at its default");
}

//------------------------------------------------------------------------
int main ()
{
	std::printf ("FilterDrum layout tests\n");
	std::printf ("-----------------------\n");

	testEveryParameterOnce ();
	testGeometry ();
	testLettering ();
	testColoursAndColumns ();
	testSelectorClicks ();
	testWear ();
	testEncodings ();
	testReadouts ();

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
