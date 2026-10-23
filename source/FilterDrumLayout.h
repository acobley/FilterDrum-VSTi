//------------------------------------------------------------------------
// FilterDrum - the panel's layout, as data
//
// THE LAYOUT IS DATA, not positions typed into the middle of open(). It
// lives in this header, which includes NO VSTGUI, so two things can read
// the same numbers:
//
//   * FilterDrumEditor::open(), which builds the panel from the tables;
//   * tests/LayoutTests.cpp, which checks that nothing overlaps and
//     everything fits - the only way to test a layout from a session
//     that cannot run a host.
//
// "DESIGN 2.0" (branch Design2.0, Oct 2026), from
// Designs/FilterDrum-MainDesign.png: R2Synth's "signal flow" panel
// carried over to the drum. Concept A ("Twin Lanes") with Concept C's
// mix column.
//
//   +--------------------------------------------------------------+
//   | FILTERDRUM | engine line                                     |  header
//   +-----------------------------------------------------+--------+
//   | (1) DRUM 1                      velocity line       |  MIX   |
//   | NOISE > VCF cutoff res + envelope  > VCA  [screens] | knob   |
//   +-----------------------------------------------------+ output |
//   | (2) DRUM 2  - the same lane                         | strike |
//   +-----------------------------------------------------+--------+
//   | SEQUENCER  16 steps                  RUN  LAUNCH ON          |
//   +--------------------------------------------------------------+
//
// ONE COLOUR PER PART OF THE VOICE, carried by the knobs' value arcs:
// noise amber, filter blue, amp mint, sequencer violet. The mix column
// is the amp's mint because it is the output stage.
//
// THE TWO LANES ARE THE SAME LANE, one kLaneOffset apart, and every
// position inside a lane is written relative to the lane's top - so the
// two cannot drift, and the tests check that drum 2's controls are drum
// 1's moved down by exactly that.
//
// The panel is 1200 x 660.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumParams.h"

namespace FilterDrum {
namespace Layout {

//------------------------------------------------------------------------
struct Rect
{
	int left, top, right, bottom;

	constexpr int width () const { return right - left; }
	constexpr int height () const { return bottom - top; }
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
constexpr int kSectionRadius   = 10;

/** Under every knob: two pixels, the reading, the label. */
constexpr int kKnobGap       = 2;
constexpr int kKnobValueH    = 13;
constexpr int kKnobLabelH    = 12;
constexpr int kKnobTextBand  = kKnobGap + kKnobValueH + kKnobLabelH;

constexpr int kSwitchHeight = 48;

/** CHARACTER WIDTHS for the fit tests - generous averages for the faces
    the editor asks for (Menlo 10 for readings, Helvetica Neue bold 10
    in capitals for labels). */
constexpr int kValueCharWidth  = 6;
constexpr int kLabelCharWidth  = 7;

//------------------------------------------------------------------------
// Colours, by part of the voice
//------------------------------------------------------------------------
enum class Accent
{
	Noise,    // amber
	Filter,   // blue
	Amp,      // mint - the VCA, the mix and the output
	Seq       // violet
};

//------------------------------------------------------------------------
// Sections
//------------------------------------------------------------------------
struct Section
{
	const char* title;
	const char* subtitle;   // may be empty
	const char* badge;      // a numbered disc instead of the dot; may be empty
	Accent accent;
	Rect r;
	bool centred;           // the title centred over the section
};

constexpr int kLaneLeft   = 16;
constexpr int kLaneRight  = 986;
constexpr int kLane1Top   = 72;
constexpr int kLaneHeight = 222;
constexpr int kLaneOffset = 234;               // drum 2's lane is drum 1's, this much lower
constexpr int kLane2Top   = kLane1Top + kLaneOffset;

constexpr Rect kDrum1Rect { kLaneLeft, kLane1Top, kLaneRight, kLane1Top + kLaneHeight };
constexpr Rect kDrum2Rect { kLaneLeft, kLane2Top, kLaneRight, kLane2Top + kLaneHeight };
constexpr Rect kMixRect   { 998,  72,  1184, 528 };
constexpr Rect kSeqRect   { 16,   540, 1184, 644 };

constexpr Section kSections[] = {
	{ "DRUM 1",    "", "1", Accent::Noise, kDrum1Rect, false },
	{ "DRUM 2",    "", "2", Accent::Noise, kDrum2Rect, false },
	{ "MIX",       "", "",  Accent::Amp,   kMixRect,   true  },
	{ "SEQUENCER", "16 steps = one bar of 1/16ths  \xC2\xB7  right-click a step to skip it: 12 = 3/4, 14 = 7/8  \xC2\xB7  strikes both drums  \xC2\xB7  MIDI still triggers",
	                   "",  Accent::Seq,   kSeqRect,   false },
};
constexpr int kNumSections = static_cast<int> (sizeof (kSections) / sizeof (kSections[0]));

//------------------------------------------------------------------------
// Inside a lane, relative to its top-left corner. Drum 2 adds
// kLaneOffset to every y.
//------------------------------------------------------------------------
namespace Lane {

constexpr int kBigTop    = 100;   // cutoff, resonance: 60 across
constexpr int kNoiseTop  = 104;   // noise level: 52 across
constexpr int kSmallTop  = 118;   // the eight envelope knobs: 44 across
constexpr int kKnobMidY  = 130;   // the chevrons sit level with the big knobs

/** The two screens, stacked at the lane's right-hand end. */
constexpr Rect kFilterScreen { 760, 54,  956, 128 };
constexpr Rect kEnvScreen    { 760, 136, 956, 210 };

constexpr Rect kVelocityLine { 300, 14,  954, 30  };
constexpr Rect kSelfOscLamp  { 276, 53,  356, 71  };

} // namespace Lane

constexpr Rect laneRect (const Rect& r, int top)
{
	return { kLaneLeft + r.left, top + r.top, kLaneLeft + r.right, top + r.bottom };
}
constexpr int laneTop (int drum) { return drum == 2 ? kLane2Top : kLane1Top; }

//------------------------------------------------------------------------
// Controls
//------------------------------------------------------------------------
enum class Kind
{
	Knob,     // vertical drag
	Switch,   // one segment per named position
	Step,     // a sequencer step: click toggles
	Toggle    // Run: click toggles, with a lamp and a status line
};

struct Control
{
	Steinberg::Vst::ParamID tag;
	Kind   kind;
	Accent accent;
	int    x, y;        // top left of the control's rectangle
	int    width;
	int    diameter;    // knobs only
	bool   bipolar;     // the arc grows from the top, not from the left end
	const char* label;  // nullptr: shortLabel() - the title less its section
	int    height = 0;  // everything but knobs
	bool   reversed = false;   // drawn and dragged high-to-low (the mix: D1 on the left)
};

constexpr Rect controlRect (const Control& c)
{
	return { c.x, c.y, c.x + c.width,
	         c.y + ((c.kind == Kind::Knob) ? c.diameter + kKnobTextBand : c.height) };
}

/** One knob in a lane, by its centre x within the lane. */
constexpr Control laneKnob (Steinberg::Vst::ParamID tag, Accent accent, int drum, int centreX,
                            int top, int width, int diameter, bool bipolar, const char* label)
{
	return { tag, Kind::Knob, accent, kLaneLeft + centreX - width / 2, laneTop (drum) + top,
	         width, diameter, bipolar, label };
}

#define FILTERDRUM_LANE(D, OFF)                                                                         \
	laneKnob (kNoiseLevel  + OFF, Accent::Noise,  D, 52,  Lane::kNoiseTop, 64, 52, false, "Level"),     \
	laneKnob (kCutoff      + OFF, Accent::Filter, D, 152, Lane::kBigTop,   80, 60, false, "Cutoff"),    \
	laneKnob (kResonance   + OFF, Accent::Filter, D, 236, Lane::kBigTop,   84, 60, false, "Resonance"), \
	laneKnob (kVcfAttack   + OFF, Accent::Filter, D, 306, Lane::kSmallTop, 56, 44, false, "Attack"),    \
	laneKnob (kVcfRelease  + OFF, Accent::Filter, D, 362, Lane::kSmallTop, 56, 44, false, "Release"),   \
	laneKnob (kVcfAmount   + OFF, Accent::Filter, D, 418, Lane::kSmallTop, 56, 44, true,  "Amount"),    \
	laneKnob (kVcfVelocity + OFF, Accent::Filter, D, 474, Lane::kSmallTop, 56, 44, false, "Velocity"),  \
	laneKnob (kVcaAttack   + OFF, Accent::Amp,    D, 552, Lane::kSmallTop, 56, 44, false, "Attack"),    \
	laneKnob (kVcaRelease  + OFF, Accent::Amp,    D, 608, Lane::kSmallTop, 56, 44, false, "Release"),   \
	laneKnob (kVcaAmount   + OFF, Accent::Amp,    D, 664, Lane::kSmallTop, 56, 44, false, "Amount"),    \
	laneKnob (kVcaVelocity + OFF, Accent::Amp,    D, 720, Lane::kSmallTop, 56, 44, false, "Velocity")

/** The sequencer's steps: four groups of four. */
constexpr int kStepTop    = 580;
constexpr int kStepWidth  = 44;
constexpr int kStepHeight = 48;
constexpr int stepLeft (int i) { return 32 + i * 50 + (i / 4) * 14; }

#define FILTERDRUM_STEP(I) \
	Control { static_cast<Steinberg::Vst::ParamID> (kStep1 + I), Kind::Step, Accent::Seq, \
	          stepLeft (I), kStepTop, kStepWidth, 0, false, "", kStepHeight }

/** EVERY PARAMETER ON THE PANEL, once each - the tests hold it to that. */
constexpr Control kControls[] = {
	FILTERDRUM_LANE (1, 0),
	FILTERDRUM_LANE (2, kDrum2Offset),

	// ---- the mix column ----------------------------------------------
	// REVERSED: the parameter's 100 % is all drum 1, and drum 1 is on the
	// LEFT of the knob as it is labelled ("D1 < > D2"), so turning
	// towards D1 is turning anticlockwise.
	{ kMix,        Kind::Knob, Accent::Amp, 1031, 138, 120, 80, true,  "D1 / D2", 0, true },
	{ kOutputTrim, Kind::Knob, Accent::Amp, 1053, 318, 76,  56, false, "Output" },

	// ---- the sequencer -----------------------------------------------
	FILTERDRUM_STEP (0),  FILTERDRUM_STEP (1),  FILTERDRUM_STEP (2),  FILTERDRUM_STEP (3),
	FILTERDRUM_STEP (4),  FILTERDRUM_STEP (5),  FILTERDRUM_STEP (6),  FILTERDRUM_STEP (7),
	FILTERDRUM_STEP (8),  FILTERDRUM_STEP (9),  FILTERDRUM_STEP (10), FILTERDRUM_STEP (11),
	FILTERDRUM_STEP (12), FILTERDRUM_STEP (13), FILTERDRUM_STEP (14), FILTERDRUM_STEP (15),

	{ kSeqRun,      Kind::Toggle, Accent::Seq, 888, kStepTop, 96,  0, false, "Run",       kStepHeight },
	{ kSeqDivision, Kind::Switch, Accent::Seq, 996, kStepTop, 174, 0, false, "Launch On", kStepHeight },
};
constexpr int kNumControls = static_cast<int> (sizeof (kControls) / sizeof (kControls[0]));

#undef FILTERDRUM_LANE
#undef FILTERDRUM_STEP

//------------------------------------------------------------------------
// The screens
//------------------------------------------------------------------------
/** Points per curve: one per pixel of the screens, near enough. */
constexpr int kDisplayPoints = 200;

/** The filter screen's axes: 20 Hz..20 kHz, +18..-42 dB. */
constexpr double kFilterMinHz    = 20.0;
constexpr double kFilterMaxHz    = 20000.0;
constexpr double kFilterTopDb    = 18.0;
constexpr double kFilterBottomDb = -42.0;

//------------------------------------------------------------------------
// Fixed lettering and rules
//------------------------------------------------------------------------
enum class CaptionStyle
{
	Block,    // a block's name, small capitals in its colour
	Field,    // a control's heading, small capitals, grey
	Note      // ordinary text, grey
};

struct Caption
{
	const char* text;
	CaptionStyle style;
	Accent accent;
	Rect r;
	bool centred = false;
};

#define FILTERDRUM_LANE_CAPTIONS(T)                                                                          \
	Caption { "NOISE",                         CaptionStyle::Block, Accent::Noise,  laneRect ({ 22,  56, 100, 68 }, T) }, \
	Caption { "VCF \xC2\xB7 MS-20 LOWPASS",    CaptionStyle::Block, Accent::Filter, laneRect ({ 118, 56, 270, 68 }, T) }, \
	Caption { "ENVELOPE",                      CaptionStyle::Field, Accent::Filter, laneRect ({ 356, 80, 424, 92 }, T), true }, \
	Caption { "VCA",                           CaptionStyle::Block, Accent::Amp,    laneRect ({ 530, 56, 600, 68 }, T) }

constexpr Caption kCaptions[] = {
	FILTERDRUM_LANE_CAPTIONS (kLane1Top),
	FILTERDRUM_LANE_CAPTIONS (kLane2Top),

	{ "D1  \xE2\x80\xB9 \xE2\x80\xBA  D2", CaptionStyle::Field, Accent::Amp, { 1006, 112, 1176, 124 }, true },
	{ "constant power",                    CaptionStyle::Note,  Accent::Amp, { 1006, 268, 1176, 282 }, true },
	{ "STRIKE",                            CaptionStyle::Field, Accent::Amp, { 1006, 436, 1176, 448 }, true },

	{ "LAUNCH ON",                         CaptionStyle::Field, Accent::Seq, { 996, 564, 1170, 576 } },
};
constexpr int kNumCaptions = static_cast<int> (sizeof (kCaptions) / sizeof (kCaptions[0]));

#undef FILTERDRUM_LANE_CAPTIONS

/** Hairlines: under each lane's title, either side of ENVELOPE, and
    between the mix column's three parts. */
constexpr Rect kRules[] = {
	laneRect ({ 16,  42, 954, 43 }, kLane1Top),
	laneRect ({ 290, 86, 350, 87 }, kLane1Top),
	laneRect ({ 430, 86, 492, 87 }, kLane1Top),
	laneRect ({ 16,  42, 954, 43 }, kLane2Top),
	laneRect ({ 290, 86, 350, 87 }, kLane2Top),
	laneRect ({ 430, 86, 492, 87 }, kLane2Top),
	{ 1014, 300, 1168, 301 },
	{ 1014, 420, 1168, 421 },
};

/** The chevrons: noise > filter > amp, in each lane. */
constexpr Point kChevrons[] = {
	{ kLaneLeft + 100, kLane1Top + Lane::kKnobMidY },
	{ kLaneLeft + 512, kLane1Top + Lane::kKnobMidY },
	{ kLaneLeft + 100, kLane2Top + Lane::kKnobMidY },
	{ kLaneLeft + 512, kLane2Top + Lane::kKnobMidY },
};

//------------------------------------------------------------------------
// The lines whose text changes, and the lamps
//------------------------------------------------------------------------
constexpr Rect kWordmark   { 20,  0,  210, kHeaderHeight };
constexpr Rect kEngineLine { 230, 20, 830, 36 };

constexpr Rect kMixGainsLine  { 1006, 252, 1176, 266 };
constexpr Rect kStrikeLamp1   { 1040, 458, 1088, 476 };
constexpr Rect kStrikeLamp2   { 1096, 458, 1144, 476 };
constexpr Rect kStrikeVelLine { 1006, 484, 1176, 498 };

//------------------------------------------------------------------------
// The arithmetic, asserted rather than trusted
//------------------------------------------------------------------------
static_assert (kLane2Top + kLaneHeight == kMixRect.bottom, "the lanes span what the mix column spans");
static_assert (kLane1Top == kMixRect.top, "and start where it starts");
static_assert (kSeqRect.bottom + kMargin == kEditorHeight, "the sequencer ends a margin above the bottom");
static_assert (kMixRect.right + kMargin == kEditorWidth, "the mix column ends a margin from the right");
static_assert (stepLeft (15) + kStepWidth < 888, "the sixteenth step stops short of Run");
static_assert (Lane::kEnvScreen.bottom < kLaneHeight - 8, "the envelope screen fits its lane");
static_assert (kEditorWidth == 1200 && kEditorHeight == 660,
               "the panel changed size - update the banner and this assert together");

//------------------------------------------------------------------------
} // namespace Layout
} // namespace FilterDrum
