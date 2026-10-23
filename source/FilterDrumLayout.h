//------------------------------------------------------------------------
// FilterDrum - the panel's layout, as data
//
// THE LAYOUT IS DATA, not positions typed into the middle of open(). It
// lives in this header, which includes NO VSTGUI, so three things can
// read the same numbers:
//
//   * FilterDrumEditor::open(), which builds the panel from the tables;
//   * tests/LayoutTests.cpp, which checks that nothing overlaps and
//     everything fits - the only way to test a layout from a session
//     that cannot run a host;
//   * tools/dump-layout.cpp + tools/render-panel.py, which draw
//     docs/panel.png from these tables.
//
// THE BLACK PANEL, "MIRROR COLUMNS" (branch PolivoksUI, Oct 2026), from
// Designs/FilterDrum-Polivoks-3-MirrorColumns.png: R2Synth 1.1's look -
// matte black, white printing, boxed sections, black knurled knobs with
// printed scales, a chicken-head selector, small colour OLED screens,
// Andy's 70s sci-fi lettering (SciFiLettering.h) and the "Studio Worn"
// wear (PanelWear.h) - on a new arrangement chosen for use:
//
//   +--------------------------------------------------------------+
//   | FILTER DRUM | engine line                                    |  header
//   +-------------------+--------------+---------------------------+
//   | (1) DRUM 1  STRIKE|     MIX      | (2) DRUM 2  STRIKE        |
//   | [filter] [env]    |  < D1   D2 > | [filter] [env]            |
//   | NOISE > VCF       |   mix knob   | NOISE > VCF               |
//   | VCF ENVELOPE      |   output     | VCF ENVELOPE              |
//   | VCA            >  | RUN  LAUNCH  | <  VCA                    |
//   | velocity line     | last strike  | velocity line             |
//   +-------------------+--------------+---------------------------+
//   | SEQUENCER  16 step keys, four groups of four                  |
//   +--------------------------------------------------------------+
//
//   * THE DRUMS ARE COLUMNS, the signal flowing DOWN each, and the two
//     columns are the same column kColumnOffset apart: every control
//     sits level with its twin, so the drums are compared - and matched
//     - by eye across the panel. The tests hold drum 2 to drum 1 moved
//     right by exactly that.
//   * THE MIX KNOB SITS BETWEEN THE DRUMS IT MIXES, in a spine with the
//     output and the transport: turn it left toward drum 1, right toward
//     drum 2 (it is REVERSED - the parameter's 100 % is all drum 1).
//   * Inside a column, each block of the voice - NOISE > VCF, the VCF
//     envelope, the VCA - has its own printed box (kBlocks), and the
//     envelope knobs get twice the spacing they had in Design 2.0.
//   * The STRIKE lamps sit in each drum's title row.
//   * LAUNCH ON is a five-position rotary selector; RUN a lit push
//     button; the step keys are black push-buttons with a red lens lit
//     for a hit and a white chaser LED above for the playhead.
//
// The Accent of a control names its part of the voice - the tests hold
// every control to its part's - and picks the colour of that part's
// SCREEN traces; the panel itself is black and white, with red for what
// is set or lit.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumParams.h"
#include "SciFiLettering.h"

#include <cmath>
#include <string>

namespace FilterDrum {
namespace Layout {

//------------------------------------------------------------------------
/** A rectangle, left/top/right/bottom, in panel pixels. */
struct Rect
{
	int left, top, right, bottom;

	constexpr int width () const { return right - left; }
	constexpr int height () const { return bottom - top; }
};

/** The same in fractional pixels - where a run of lettering lands. */
struct Box
{
	double left, top, right, bottom;
};

struct Point { int x, y; };

//------------------------------------------------------------------------
// The panel
//------------------------------------------------------------------------
constexpr int kEditorWidth  = 1200;
constexpr int kEditorHeight = 660;
constexpr int kMargin       = 16;

constexpr int kHeaderHeight = 56;

/** Inside a section: the title row, and the padding round it. */
constexpr int kSectionPadX     = 14;
constexpr int kSectionPadTop   = 12;
constexpr int kSectionTitleH   = 18;
constexpr int kSectionRadius   = 2;

/** Under every knob: two pixels, the reading, the label. */
constexpr int kKnobGap       = 2;
constexpr int kKnobValueH    = 13;
constexpr int kKnobLabelH    = 12;
constexpr int kKnobTextBand  = kKnobGap + kKnobValueH + kKnobLabelH;

/** A rotary selector's rectangle, top to bottom: a band for the printed
    position names over the knob, the knob, and its name at the foot. */
constexpr int kSelectorTopBand = 26;
constexpr int kSelectorNameH   = 12;

/** How far the black cut-out round each screen reaches past it. */
constexpr int kScreenBezel = 3;

/** A drum's badge: the ring with its number, before the title box. */
constexpr int kBadgeRadius = 10;
constexpr int kBadgeWidth  = 28;   // ring and the gap after it

//------------------------------------------------------------------------
// The lettering - SciFiLettering.h's face at the panel's sizes (R2Synth's
// sizes; cap heights are 0.64 of the font sizes the design was drawn
// with). The wordmark is smaller than R2Synth's: FILTER DRUM is longer.
//------------------------------------------------------------------------
constexpr Lettering::Style kTextWordmark { 21.0, 1.7, 1.2 };
constexpr Lettering::Style kTextTitle    { 8.3, Lettering::kBold };
constexpr Lettering::Style kTextSubtitle { 5.8, Lettering::kRegular };
constexpr Lettering::Style kTextLabel    { 6.1, Lettering::kBold };     // knob and selector names, lamps
constexpr Lettering::Style kTextValue    { 6.4, Lettering::kRegular };  // every reading
constexpr Lettering::Style kTextBlock    { 6.7, Lettering::kBold };     // a block's caption
constexpr Lettering::Style kTextNote     { 5.8, Lettering::kRegular };
constexpr Lettering::Style kTextEngine   { 6.7, Lettering::kRegular };
constexpr Lettering::Style kTextInfo     { 5.8, Lettering::kRegular };  // velocity, gains, last strike
constexpr Lettering::Style kTextScreen   { 6.1, Lettering::kRegular };
constexpr Lettering::Style kTextBadge    { 7.7, Lettering::kBold };
constexpr Lettering::Style kTextTick     { 4.8, Lettering::kRegular };  // 0 / 10 by a knob's scale
constexpr Lettering::Style kTextButton   { 7.7, Lettering::kBold };     // RUN
constexpr Lettering::Style kTextStep     { 6.1, Lettering::kBold };     // a step key's number

//------------------------------------------------------------------------
// Sections
//------------------------------------------------------------------------
enum class Accent
{
	Noise,    // amber on a screen - the noise has none today
	Filter,   // blue
	Amp,      // green - the VCA, the mix and the output
	Seq       // violet
};

struct Section
{
	const char* title;
	const char* subtitle;   // may be empty
	const char* badge;      // a numbered ring before the title; may be empty
	Accent accent;
	Rect r;
	bool centred;           // the title box centred over the section
};

constexpr int kColumnLeft   = 16;
constexpr int kColumnWidth  = 432;
constexpr int kColumnOffset = 736;              // drum 2's column is drum 1's, this much further right
constexpr int kColumnTop    = 68;
constexpr int kColumnBottom = 504;

constexpr int columnLeft (int drum) { return kColumnLeft + ((drum == 2) ? kColumnOffset : 0); }

constexpr Rect kDrum1Rect { columnLeft (1), kColumnTop, columnLeft (1) + kColumnWidth, kColumnBottom };
constexpr Rect kDrum2Rect { columnLeft (2), kColumnTop, columnLeft (2) + kColumnWidth, kColumnBottom };
constexpr Rect kMixRect   { 460, kColumnTop, 740, kColumnBottom };
constexpr Rect kSeqRect   { 16,  516, 1184, 644 };

constexpr Section kSections[] = {
	{ "DRUM 1",    "", "1", Accent::Noise, kDrum1Rect, false },
	{ "MIX",       "", "",  Accent::Amp,   kMixRect,   true  },
	{ "DRUM 2",    "", "2", Accent::Noise, kDrum2Rect, false },
	{ "SEQUENCER", "16 steps = one bar of 1/16ths  /  strikes both drums  /  MIDI still triggers",
	                   "",  Accent::Seq,   kSeqRect,   false },
};
constexpr int kNumSections = static_cast<int> (sizeof (kSections) / sizeof (kSections[0]));

/** The middle of a section's title row. */
constexpr double titleRowCentre (const Rect& section)
{
	return section.top + kSectionPadTop + kSectionTitleH / 2.0 + 1.0;
}

/** Where a badge's ring is centred. */
inline void badgeCentre (const Section& s, double& x, double& y)
{
	x = s.r.left + kSectionPadX + kBadgeRadius - 1.0;
	y = titleRowCentre (s.r);
}

/** THE TITLE BOX: the section's name in a white-outlined box, 8 px of
    black each side of the lettering - after the badge if there is one,
    or centred over the section. */
inline Box titleBox (const Section& s)
{
	const double w = 16.0 + Lettering::width (s.title, kTextTitle);
	const double centre = titleRowCentre (s.r);
	double left = s.r.left + kSectionPadX - 2;
	if (s.centred)
		left = (s.r.left + s.r.right) / 2.0 - w / 2.0;
	else if (s.badge && *s.badge)
		left += kBadgeWidth;
	return { left, centre - 11.0, left + w, centre + 11.0 };
}

/** Where the subtitle starts: 12 px after the title box. */
inline double subtitleLeft (const Section& s)
{
	return titleBox (s).right + 12.0;
}

//------------------------------------------------------------------------
// Inside a column: x relative to the column's left edge, y absolute (the
// two columns share every y).
//------------------------------------------------------------------------
namespace Column {

constexpr Rect kStrikeLamp   { 330, 81,  418, 99  };
constexpr Rect kFilterScreen { 14,  108, 214, 180 };
constexpr Rect kEnvScreen    { 224, 108, 418, 180 };
constexpr Rect kSelfOscLamp  { 318, 231, 410, 249 };
constexpr Rect kVelocityLine { 12,  487, 420, 501 };

/** The three blocks of the voice, top to bottom. */
constexpr Rect kVoiceBlock { 12, 198, 420, 294 };
constexpr Rect kEnvBlock   { 12, 308, 420, 388 };
constexpr Rect kVcaBlock   { 12, 402, 420, 482 };

/** Knob rows: the TOP of each knob (the rectangle's top). */
constexpr int kNoiseTop   = 216;   // 48 across
constexpr int kFilterTop  = 214;   // cutoff, resonance: 52 across
constexpr int kEnvTop     = 316;   // the VCF envelope: 40 across
constexpr int kVcaTop     = 410;   // the VCA: 40 across

/** The four knobs of a row, by centre x: twice Design 2.0's spacing. */
constexpr int kRowX[4] = { 66, 166, 266, 366 };

/** NOISE > CUTOFF, level with the knobs' centres. */
constexpr int kChevronX = 101;
constexpr int kVoiceMidY = kFilterTop + 26;

} // namespace Column

constexpr Rect columnRect (const Rect& r, int drum)
{
	return { columnLeft (drum) + r.left, r.top, columnLeft (drum) + r.right, r.bottom };
}

//------------------------------------------------------------------------
// Controls
//------------------------------------------------------------------------
enum class Kind
{
	Knob,     // Panel::Knob - vertical drag
	Switch,   // Panel::RotarySwitch - one printed position per name
	Step,     // Panel::StepPad - a sequencer step: click toggles
	Toggle    // Panel::ToggleButton - Run: click toggles, a lamp and a status line
};

struct Control
{
	Steinberg::Vst::ParamID tag;
	Kind   kind;
	Accent accent;
	int    x, y;        // top left of the control's rectangle
	int    width;
	int    diameter;    // the knob - for a selector, its chicken head's circle
	bool   bipolar;     // knobs: the scale reads - .. + rather than 0 .. 10
	const char* label;  // nullptr: shortLabel() - the title less its section
	int    height = 0;  // everything but knobs
	bool   reversed = false;   // drawn and dragged high-to-low (the mix: D1 on the left)
	int    pivotX = 0;  // selectors only: the knob's centre; 0 = the middle
};

constexpr Rect controlRect (const Control& c)
{
	return { c.x, c.y, c.x + c.width,
	         c.y + ((c.kind == Kind::Knob) ? c.diameter + kKnobTextBand : c.height) };
}

/** One knob in a column, by its centre x within the column. */
constexpr Control columnKnob (Steinberg::Vst::ParamID tag, Accent accent, int drum, int centreX,
                              int top, int width, int diameter, bool bipolar, const char* label)
{
	return { tag, Kind::Knob, accent, columnLeft (drum) + centreX - width / 2, top,
	         width, diameter, bipolar, label };
}

#define FILTERDRUM_COLUMN(D, OFF)                                                                                          \
	columnKnob (kNoiseLevel  + OFF, Accent::Noise,  D, 56,               Column::kNoiseTop,  72,  48, false, "Noise"),     \
	columnKnob (kCutoff      + OFF, Accent::Filter, D, 158,              Column::kFilterTop, 84,  52, false, "Cutoff"),    \
	columnKnob (kResonance   + OFF, Accent::Filter, D, 262,              Column::kFilterTop, 100, 52, false, "Resonance"), \
	columnKnob (kVcfAttack   + OFF, Accent::Filter, D, Column::kRowX[0], Column::kEnvTop,    92,  40, false, "Attack"),    \
	columnKnob (kVcfRelease  + OFF, Accent::Filter, D, Column::kRowX[1], Column::kEnvTop,    92,  40, false, "Release"),   \
	columnKnob (kVcfAmount   + OFF, Accent::Filter, D, Column::kRowX[2], Column::kEnvTop,    92,  40, true,  "Amount"),    \
	columnKnob (kVcfVelocity + OFF, Accent::Filter, D, Column::kRowX[3], Column::kEnvTop,    92,  40, false, "Velocity"),  \
	columnKnob (kVcaAttack   + OFF, Accent::Amp,    D, Column::kRowX[0], Column::kVcaTop,    92,  40, false, "Attack"),    \
	columnKnob (kVcaRelease  + OFF, Accent::Amp,    D, Column::kRowX[1], Column::kVcaTop,    92,  40, false, "Release"),   \
	columnKnob (kVcaAmount   + OFF, Accent::Amp,    D, Column::kRowX[2], Column::kVcaTop,    92,  40, false, "Amount"),    \
	columnKnob (kVcaVelocity + OFF, Accent::Amp,    D, Column::kRowX[3], Column::kVcaTop,    92,  40, false, "Velocity")

/** The sequencer's steps: four groups of four black keys. A pad's
    rectangle holds, top to bottom, its chaser LED, the key and its
    number (Panel::StepPad). */
constexpr int kStepTop    = 552;
constexpr int kStepWidth  = 58;
constexpr int kStepHeight = 82;
constexpr int stepLeft (int i) { return 49 + i * 66 + (i / 4) * 18; }

/** Inside a step pad, from its top. */
constexpr int kStepLedY      = 10;    // the chaser LED's centre
constexpr int kStepKeyTop    = 20;
constexpr int kStepKeyBottom = 62;
constexpr int kStepNumberY   = 73;    // the number's centre

#define FILTERDRUM_STEP(I) \
	Control { static_cast<Steinberg::Vst::ParamID> (kStep1 + I), Kind::Step, Accent::Seq, \
	          stepLeft (I), kStepTop, kStepWidth, 0, false, "", kStepHeight }

/** The spine's centre. */
constexpr int kSpineX = (kMixRect.left + kMixRect.right) / 2;   // 600

/** EVERY PARAMETER ON THE PANEL, once each - the tests hold it to that.
    (A step's DISABLE is carried by its step's pad - see StepPad.) */
constexpr Control kControls[] = {
	FILTERDRUM_COLUMN (1, 0),
	FILTERDRUM_COLUMN (2, kDrum2Offset),

	// ---- the spine ------------------------------------------------------
	// REVERSED: the parameter's 100 % is all drum 1, and drum 1 is on the
	// LEFT of the knob - the left of the panel - so turning towards D1 is
	// turning anticlockwise.
	{ kMix,        Kind::Knob, Accent::Amp, kSpineX - 52, 126, 104, 92, true,  "D1 / D2", 0, true },
	{ kOutputTrim, Kind::Knob, Accent::Amp, kSpineX - 45, 276, 90,  56, false, "Output" },

	{ kSeqRun,      Kind::Toggle, Accent::Seq, 484, 392, 90,  0,  false, "Run",       54 },
	{ kSeqDivision, Kind::Switch, Accent::Seq, 598, 384, 120, 32, false, "Launch On", 82, false, 658 },

	// ---- the sequencer ----------------------------------------------------
	FILTERDRUM_STEP (0),  FILTERDRUM_STEP (1),  FILTERDRUM_STEP (2),  FILTERDRUM_STEP (3),
	FILTERDRUM_STEP (4),  FILTERDRUM_STEP (5),  FILTERDRUM_STEP (6),  FILTERDRUM_STEP (7),
	FILTERDRUM_STEP (8),  FILTERDRUM_STEP (9),  FILTERDRUM_STEP (10), FILTERDRUM_STEP (11),
	FILTERDRUM_STEP (12), FILTERDRUM_STEP (13), FILTERDRUM_STEP (14), FILTERDRUM_STEP (15),
};
constexpr int kNumControls = static_cast<int> (sizeof (kControls) / sizeof (kControls[0]));

#undef FILTERDRUM_COLUMN
#undef FILTERDRUM_STEP

//------------------------------------------------------------------------
// The rotary selector
//------------------------------------------------------------------------
/** A selector's geometry: its knob, where its positions point (degrees
    from twelve o'clock, clockwise) and the radius its position names hang
    from. Two positions point 45 either side; three add straight up; five
    - LAUNCH ON - fan 100 either side, 50 apart. */
constexpr int kMaxPositions = 5;

struct Selector
{
	double cx, cy, radius, labelRadius;
	int positions;
	double angle[kMaxPositions];
	double nameCentreY;
};

inline Selector selectorOf (const Control& c, int positions)
{
	const Rect r = controlRect (c);
	Selector s {};
	s.radius = c.diameter / 2.0;
	s.cx = (c.pivotX > 0) ? c.pivotX : (r.left + r.right) / 2.0;
	s.cy = r.top + kSelectorTopBand + s.radius;
	s.labelRadius = s.radius + 12.0;
	s.positions = (positions < 2) ? 2 : (positions > kMaxPositions ? kMaxPositions : positions);
	const double spread = (s.positions <= 3) ? 90.0 : 200.0;   // first to last
	for (int i = 0; i < s.positions; ++i)
		s.angle[i] = -spread / 2.0 + spread * i / (s.positions - 1);
	s.nameCentreY = r.bottom - kSelectorNameH / 2.0;
	return s;
}

/** A point at `degrees` (from twelve o'clock, clockwise) and `radius`. */
inline void selectorPoint (const Selector& s, double radius, double degrees, double& x, double& y)
{
	const double t = degrees * 3.14159265358979323846 / 180.0;
	x = s.cx + radius * std::sin (t);
	y = s.cy - radius * std::cos (t);
}

/** WHERE A POSITION'S NAME IS PRINTED: hung from labelRadius along its
    angle - above the knob for the one straight up, ending short of the
    line on the left, starting past it on the right. */
inline Box selectorLabelBox (const Selector& s, int index, const std::string& text)
{
	const double t = s.angle[index];
	double px = 0.0, py = 0.0;
	selectorPoint (s, s.labelRadius, t, px, py);
	const double w = Lettering::width (text, kTextLabel);
	const double h = kTextLabel.height ();
	double left = 0.0, centreY = 0.0;
	if (std::fabs (t) < 10.0)
	{
		left = px - w / 2.0;
		centreY = py - 8.0;
	}
	else
	{
		left = (t < 0.0) ? px - 2.0 - w : px + 2.0;
		centreY = py + ((std::fabs (t) < 80.0) ? 2.0 : 0.0);
	}
	return { left, centreY - h / 2.0, left + w, centreY + h / 2.0 };
}

/** THE POSITION A CLICK PICKS: on the knob itself, -1 (the knob steps
    on); anywhere else in the selector, the position whose angle is
    nearest the click's. */
inline int selectorPositionAt (const Selector& s, double x, double y)
{
	const double dx = x - s.cx, dy = y - s.cy;
	if (dx * dx + dy * dy <= s.radius * s.radius)
		return -1;
	const double t = std::atan2 (dx, -dy) * 180.0 / 3.14159265358979323846;
	int best = 0;
	for (int i = 1; i < s.positions; ++i)
		if (std::fabs (s.angle[i] - t) < std::fabs (s.angle[best] - t))
			best = i;
	return best;
}

//------------------------------------------------------------------------
// The screens
//------------------------------------------------------------------------
/** Drum 1's filter and envelope, then drum 2's - the order the wear and
    the editor index them in. */
constexpr Rect kScreens[] = {
	columnRect (Column::kFilterScreen, 1), columnRect (Column::kEnvScreen, 1),
	columnRect (Column::kFilterScreen, 2), columnRect (Column::kEnvScreen, 2),
};
constexpr int kNumScreens = static_cast<int> (sizeof (kScreens) / sizeof (kScreens[0]));

constexpr int filterScreenIndex (int drum) { return (drum == 2) ? 2 : 0; }
constexpr int envScreenIndex (int drum)    { return (drum == 2) ? 3 : 1; }

/** A screen with its black cut-out round it. */
constexpr Rect bezelOf (const Rect& r)
{
	return { r.left - kScreenBezel, r.top - kScreenBezel, r.right + kScreenBezel, r.bottom + kScreenBezel };
}

/** Points per curve: one every pixel on the 200-pixel screens. */
constexpr int kDisplayPoints = 200;

/** The filter screen's axes: 20 Hz..20 kHz, +18..-42 dB. */
constexpr double kFilterMinHz    = 20.0;
constexpr double kFilterMaxHz    = 20000.0;
constexpr double kFilterTopDb    = 18.0;
constexpr double kFilterBottomDb = -42.0;

//------------------------------------------------------------------------
// Fixed printing: the blocks, captions, rules, chevrons, screws
//------------------------------------------------------------------------
/** A block of the voice: a thin box, its caption cut into its top edge. */
struct Block
{
	const char* caption;
	Rect r;
};

/** The caption's left, and the gap cut for it either side. */
constexpr int kBlockCaptionInset = 10;
constexpr int kBlockCaptionGap   = 4;

#define FILTERDRUM_BLOCKS(D)                                                               \
	Block { "NOISE \xE2\x86\x92 VCF - MS-20 LOWPASS", columnRect (Column::kVoiceBlock, D) }, \
	Block { "VCF ENVELOPE",                         columnRect (Column::kEnvBlock, D) },   \
	Block { "VCA",                                  columnRect (Column::kVcaBlock, D) }

constexpr Block kBlocks[] = { FILTERDRUM_BLOCKS (1), FILTERDRUM_BLOCKS (2) };

#undef FILTERDRUM_BLOCKS

enum class CaptionStyle
{
	Block,    // bold
	Field,    // a control's heading, bold
	Note      // ordinary text, soft
};

enum class Align
{
	Left,
	Centre,
	Right
};

struct Caption
{
	const char* text;
	CaptionStyle style;
	Accent accent;
	Rect r;
	Align align = Align::Left;
};

constexpr Caption kCaptions[] = {
	// Either side of the mix knob: which way is which drum.
	{ "\xE2\x80\xB9 DRUM 1", CaptionStyle::Note, Accent::Amp, { 476, 112, 544, 124 }, Align::Centre },
	{ "DRUM 2 \xE2\x80\xBA", CaptionStyle::Note, Accent::Amp, { 656, 112, 724, 124 }, Align::Centre },

	// The gesture the pads hide, printed where the pads are.
	{ "right-click a step to skip it  /  14 steps = 7/8", CaptionStyle::Note, Accent::Seq,
	  { 900, 532, 1170, 544 }, Align::Right },
};
constexpr int kNumCaptions = static_cast<int> (sizeof (kCaptions) / sizeof (kCaptions[0]));

/** Hairlines: across the spine, between the output and the transport. */
constexpr Rect kRules[] = {
	{ 476, 372, 724, 373 },
};

/** The chevrons: the signal goes this way - NOISE > VCF in each column,
    and out of each column's VCA into the spine. */
struct Chevron
{
	int x, y;
	bool left;   // pointing left
};

constexpr Chevron kChevrons[] = {
	{ columnLeft (1) + Column::kChevronX, Column::kVoiceMidY, false },
	{ columnLeft (2) + Column::kChevronX, Column::kVoiceMidY, false },
	{ kDrum1Rect.right + 6, 442, false },
	{ kDrum2Rect.left - 6,  442, true  },
};

/** The screws in the panel's four corners. */
constexpr Point kScrews[] = { { 8, 8 }, { kEditorWidth - 8, 8 }, { 8, kEditorHeight - 8 },
                              { kEditorWidth - 8, kEditorHeight - 8 } };

//------------------------------------------------------------------------
// The lines whose text changes, and the lamps
//------------------------------------------------------------------------
/** The header: the wordmark, a divider, the engine line. */
constexpr Rect kWordmark      { 22,  0,  258, kHeaderHeight };
constexpr int  kWordmarkGap   = 10;   // between FILTER and DRUM
constexpr int  kHeaderDivider = 268;
constexpr Rect kEngineLine    { 282, 20, 830, 36 };

/** The spine's two lines: the gains the mix applies, and the last strike. */
constexpr Rect kMixGainsLine  { 476, 250, 724, 264 };
constexpr Rect kStrikeVelLine { 476, 480, 724, 494 };

//------------------------------------------------------------------------
// The arithmetic, asserted rather than trusted
//------------------------------------------------------------------------
static_assert (kDrum1Rect.right < kMixRect.left && kMixRect.right < kDrum2Rect.left, "column, spine, column");
static_assert (kMixRect.left - kDrum1Rect.right == kDrum2Rect.left - kMixRect.right, "the spine is centred");
static_assert (kDrum2Rect.right + kMargin == kEditorWidth, "drum 2 ends a margin from the right");
static_assert (kSeqRect.bottom + kMargin == kEditorHeight, "the sequencer ends a margin above the bottom");
static_assert (kColumnBottom < kSeqRect.top, "the columns end above the sequencer");
static_assert (Column::kVelocityLine.bottom < kColumnBottom, "the velocity line fits its column");
static_assert (Column::kVcaTop + 40 + kKnobTextBand <= Column::kVcaBlock.bottom, "the VCA row fits its block");
static_assert (Column::kEnvTop + 40 + kKnobTextBand <= Column::kEnvBlock.bottom, "the envelope row fits its block");
static_assert (Column::kFilterTop + 52 + kKnobTextBand <= Column::kVoiceBlock.bottom, "the filter row fits its block");
static_assert (stepLeft (15) + kStepWidth <= kSeqRect.right - 30, "the sixteenth step fits the strip");
static_assert (kStepTop + kStepHeight <= kSeqRect.bottom - 8, "the steps fit the strip");
static_assert (kWordmark.right < kHeaderDivider && kHeaderDivider < kEngineLine.left,
               "wordmark, divider, engine line");
static_assert (kEditorWidth == 1200 && kEditorHeight == 660,
               "the panel changed size - update the banner, docs/panel.png and this assert together");

//------------------------------------------------------------------------
} // namespace Layout
} // namespace FilterDrum
