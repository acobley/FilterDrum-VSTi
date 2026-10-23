//------------------------------------------------------------------------
// FilterDrum - the panel's views (the black panel, "Mirror Columns")
//
// CARRIED OVER FROM R2Synth 1.1 (source/R2SynthPanelViews.*, the black
// panel), so the two instruments share one look and one feel under the
// mouse: matte black, white printing, boxed sections, black knurled knobs
// with printed scales, a chicken-head rotary selector, small colour OLED
// screens, every letter in Andy's 70s sci-fi lettering
// (SciFiLettering.h), and the "Studio Worn" wear (PanelWear.h). Branch
// PolivoksUI, Oct 2026, from Designs/FilterDrum-Polivoks-3-MirrorColumns.png.
//
//   Backdrop      everything that never changes: the ground, the header
//                 and its wordmark, the corner screws, the boxed sections
//                 with their badges and titles, the blocks' thin boxes,
//                 the chevrons, the rules, the screens' black cut-outs
//                 and the fixed captions - all from FilterDrumLayout.h.
//                 One view, behind the rest, mouse-disabled.
//   Knob          R2Synth's: black, knurled, a white line, its printed
//                 scale, its reading and its label. (+ REVERSED, for the
//                 mix, + its own end marks - D1 and D2.)
//   RotarySwitch  R2Synth's chicken head, for up to five positions
//                 (LAUNCH ON).
//   Scope         R2Synth's OLED. (+ a DASHED companion - the filter at
//                 the peak, the VCF envelope under the VCA's.)
//   Lamp          a red lamp and its name: SELF-OSC, and the STRIKE
//                 lamps (+ a level, so a strike fades).
//   StepPad       ONE SEQUENCER STEP: a black key with a red lens lit for
//                 a hit, a white chaser LED above it for the playhead, its
//                 number under it.
//   ToggleButton  RUN: a black push button with a red lamp, its status
//                 under it.
//   TextLine      one line of lettering whose words change.
//
// THE SAME TWO RULES AS EVER. No bitmaps - everything is paths, the
// lettering included, so the panel is resolution-independent and
// resource/ needs no artwork. And no numbers made here: every reading
// comes in through a formatter the editor hands over (readoutText() in
// FilterDrumReadouts.cpp), and every curve through setCurves() from the
// shared trace functions in FilterDrumDsp.h.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumLayout.h"
#include "PanelWear.h"
#include "SciFiLettering.h"

#include "vstgui/vstgui.h"

#include <functional>
#include <string>
#include <vector>

namespace FilterDrum {
namespace Panel {

//------------------------------------------------------------------------
// The palette - R2Synth 1.1's tokens
//------------------------------------------------------------------------
namespace Palette {

const VSTGUI::CColor kGround     ( 18,  18,  18, 255);
const VSTGUI::CColor kHeader     ( 12,  12,  12, 255);
const VSTGUI::CColor kHeaderRule (233, 230, 220, 255);
const VSTGUI::CColor kSection    ( 21,  21,  21, 255);
const VSTGUI::CColor kSectionEdge(217, 214, 204, 255);   // box lines, chevrons, rings
const VSTGUI::CColor kBlockEdge  ( 85,  83,  78, 255);   // the blocks' thin boxes inside a column
const VSTGUI::CColor kInk        (244, 241, 232, 255);   // ALL the lettering: white on black
const VSTGUI::CColor kInkSoft    (185, 182, 174, 255);   // subtitles, notes, the info lines
const VSTGUI::CColor kInkDim     (143, 140, 133, 255);   // a selector's positions not set; keys not hit
const VSTGUI::CColor kInkFaint   ( 90,  88,  82, 255);   // a disabled step
const VSTGUI::CColor kDivider    ( 90,  88,  82, 255);
const VSTGUI::CColor kRule       ( 58,  58,  58, 255);
const VSTGUI::CColor kTick       (233, 230, 220, 255);   // the printed scales
const VSTGUI::CColor kRed        (226,  52,  43, 255);   // the position that is set
const VSTGUI::CColor kScrew      ( 58,  58,  58, 255);
const VSTGUI::CColor kKnobBody   ( 10,  10,  10, 255);
const VSTGUI::CColor kKnobRim    ( 47,  47,  47, 255);
const VSTGUI::CColor kKeyEdge    ( 58,  58,  58, 255);
const VSTGUI::CColor kBezel      (  5,   5,   5, 255);
const VSTGUI::CColor kBezelEdge  ( 60,  60,  60, 255);
const VSTGUI::CColor kScreen     (  0,   0,   0, 255);
const VSTGUI::CColor kGridDot    ( 42,  45,  51, 255);
const VSTGUI::CColor kScreenText (141, 150, 163, 255);
const VSTGUI::CColor kLampOn     (255,  59,  47, 255);
const VSTGUI::CColor kLampOff    ( 58,  21,  18, 255);
const VSTGUI::CColor kLensOff    ( 42,  14,  12, 255);   // a step key's red lens, unlit
const VSTGUI::CColor kChaser     (255, 253, 244, 255);   // the playhead's white LED

/** A part's SCREEN colour: filter blue, amp green, sequencer violet
    (and noise amber, which no screen uses today). */
VSTGUI::CColor accent (Layout::Accent a);

/** The same at `alpha` (0..255). */
VSTGUI::CColor accentAlpha (Layout::Accent a, uint8_t alpha);

} // namespace Palette

//------------------------------------------------------------------------
// The lettering
//------------------------------------------------------------------------
using Layout::Align;

/** DRAW A RUN OF LETTERING: `text` in `style`, its ink box vertically
    centred on `centreY`, starting at, centred on or ending at `x`.
    `strokeUnits` > 0 strokes the same glyphs at another weight without
    moving them - the wordmark's inline cut. */
void drawLettering (VSTGUI::CDrawContext* context, const std::string& text, const Lettering::Style& style,
                    const VSTGUI::CColor& colour, VSTGUI::CCoord x, VSTGUI::CCoord centreY,
                    Align align = Align::Left, double strokeUnits = -1.0);

/** A string in capitals. ASCII only is changed; UTF-8 passes through. */
std::string upper (const std::string& s);

//------------------------------------------------------------------------
/** Everything that never changes, from the layout tables, in one view
    behind the others. Mouse-disabled. */
class Backdrop : public VSTGUI::CView
{
public:
	explicit Backdrop (const VSTGUI::CRect& size);
	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (Backdrop, VSTGUI::CView)

private:
	/** The wear on the panel - over the printing, under the controls.
	    Only the marks that touch the area being redrawn are drawn, so a
	    knob turning does not redraw a thousand specks. */
	void drawWear (VSTGUI::CDrawContext* context);

	Wear::PanelWear mWear;
};

//------------------------------------------------------------------------
/** A ROTARY KNOB: black, knurled, a white line, a printed scale of
    eleven ticks over 270 degrees (0 and 10 at the ends of a big one, or
    - and + on a BIPOLAR knob, or the ends setEnds() gives).

    THE WHOLE RECTANGLE IS THE TARGET, reading and label included.
      drag          vertically; up turns it clockwise; 200 pixels is the range
      Shift         ten times finer, drag and wheel alike
      double-click  the parameter's default (setDefaultValue)
      wheel         one per cent a click

    REVERSED: the pointer is drawn for 1 - value, and up drags the value
    DOWN - so "up turns it clockwise" still holds. For the mix, whose
    100 % is drum 1 and whose drum 1 is on the left. */
class Knob : public VSTGUI::CControl
{
public:
	Knob (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	      Layout::Accent accent, int diameter, bool bipolar, bool reversed = false);

	void setLabel (const std::string& label) { mLabel = upper (label); }

	/** The reading, given the NORMALISED value - the editor hands over
	    readoutText() so the panel and the host cannot disagree. */
	void setFormatter (std::function<std::string (float)> formatter) { mFormatter = std::move (formatter); }

	/** The marks printed at the scale's two ends, instead of 0 / 10. */
	void setEnds (const std::string& low, const std::string& high) { mLow = low; mHigh = high; }

	/** Its wear (Wear::knobWear) - drawn relative to the pointer, so it
	    turns with the knob. None until set. */
	void setWear (const Wear::KnobWear& wear) { mWear = wear; }

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (Knob, VSTGUI::CControl)

	/** VSTGUI's degrees (0 = three o'clock, clockwise): 135 at 0, 270
	    (the top) at 0.5, 405 at 1. */
	static double angleOf (double normalized) { return 135.0 + 270.0 * normalized; }

private:
	/** Its part of the voice. Every knob on the black panel is black and
	    white, so nothing is coloured by it today; kept so a knob still
	    knows. */
	[[maybe_unused]] Layout::Accent mAccent;
	int  mDiameter;
	bool mBipolar;
	bool mReversed;
	std::string mLabel;
	std::string mLow;
	std::string mHigh;
	std::function<std::string (float)> mFormatter;
	Wear::KnobWear mWear;

	bool mDragging = false;
	VSTGUI::CCoord mLastY = 0.;
};

//------------------------------------------------------------------------
/** A ROTARY SELECTOR: a chicken-head knob, its positions printed round
    it (the one that is set in white with a red mark, the others dim),
    its name under it. Geometry from Layout::selectorOf(), which the tests
    read too.

      click a position's name (or anywhere off the knob towards it)
                 that position
      click the knob     the next position, round to the first again
      wheel      steps

    Positions are spread evenly over 0..1, the way an Enum parameter's
    normalised values are. */
class RotarySwitch : public VSTGUI::CControl
{
public:
	RotarySwitch (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	              const Layout::Control& layout);

	void setNames (const std::vector<std::string>& names);
	void setLabel (const std::string& label) { mLabel = upper (label); }

	/** Grit round the hub (Wear::selectorGrit), turning with it. */
	void setWear (const std::vector<Wear::PolarSpeck>& grit) { mGrit = grit; }

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (RotarySwitch, VSTGUI::CControl)

private:
	int count () const { return static_cast<int> (mNames.size ()); }
	int currentIndex () const;
	void select (int index);

	Layout::Control mLayout;
	Layout::Selector mGeometry;
	std::vector<std::string> mNames;
	std::string mLabel;
	std::vector<Wear::PolarSpeck> mGrit;
};

//------------------------------------------------------------------------
/** A SCREEN - a small colour OLED: a true-black glass with a dot grid, a
    caption top left, a figure top right in the trace's colour, and up to
    two traces 0..1 (y up, CLAMPED, not scaled). Its black cut-out in the
    panel is the Backdrop's.

    The FIRST trace is the main one - crisp, in its colour, with a slight
    bloom and, setFill (true), a fill that fades to black. The SECOND is
    a companion, thinner, in its own colour, and DASHED when
    setCompanionDashed (true): the filter at the peak of a hit; the VCF
    envelope under the VCA's. An optional white ring sits on the main
    trace (the cutoff). Mouse-disabled. */
class Scope : public VSTGUI::CView
{
public:
	explicit Scope (const VSTGUI::CRect& size);

	void setColours (const VSTGUI::CColor& main, const VSTGUI::CColor& companion);
	void setFill (bool fill) { mFill = fill; }
	void setCompanionDashed (bool dashed) { mCompanionDashed = dashed; }
	void setCurves (const float* main, const float* companion, int count);
	void setCaption (const std::string& caption) { mCaption = caption; }
	void setAnnotation (const std::string& annotation) { mAnnotation = annotation; }

	/** The ring, at (x, y) 0..1 of the plot; negative x for none. */
	void setMarker (double x, double y) { mMarkerX = x; mMarkerY = y; }

	/** The dust and fingerprints on its glass (Wear::glassWear), drawn
	    over everything - they are on the glass, not in the picture. */
	void setWear (const Wear::GlassWear& wear) { mWear = wear; }

	void draw (VSTGUI::CDrawContext* context) override;

	CLASS_METHODS (Scope, VSTGUI::CView)

private:
	VSTGUI::CRect plotRect () const;
	void drawTrace (VSTGUI::CDrawContext* context, const std::vector<float>& data,
	                const VSTGUI::CColor& colour, VSTGUI::CCoord width, bool fill, bool bloom,
	                bool dashed) const;

	VSTGUI::CColor mMain;
	VSTGUI::CColor mCompanion;
	bool mFill = false;
	bool mCompanionDashed = false;
	std::vector<float> mMainData;
	std::vector<float> mCompanionData;
	std::string mCaption;
	std::string mAnnotation;
	double mMarkerX = -1.0;
	double mMarkerY = 0.0;
	Wear::GlassWear mWear;
};

//------------------------------------------------------------------------
/** A LAMP: a red lamp in a black bezel and its name to the right, the
    pair right-aligned in the rectangle. Mouse-disabled.

    setOn() is the SELF-OSC lamp's whole interface. The STRIKE lamps use
    setLevel(), 0..1, so a hit fades rather than blinks; any level above
    zero is "on". */
class Lamp : public VSTGUI::CView
{
public:
	Lamp (const VSTGUI::CRect& size, const std::string& name);

	void setOn (bool on) { setLevel (on ? 1.f : 0.f); }
	void setLevel (float level);
	bool isOn () const { return mLevel > 0.f; }
	float level () const { return mLevel; }

	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (Lamp, VSTGUI::CView)

private:
	std::string mName;
	float mLevel = 0.f;
};

//------------------------------------------------------------------------
/** ONE SEQUENCER STEP: a black push-button key with a red lens, lit when
    the step is ON; a white chaser LED above it, lit while the PLAYHEAD
    is on it - the two separate on purpose, so a step that is off still
    shows the playhead passing, and a hit never looks like the playhead.
    Its number is printed under it. Click toggles.

    RIGHT-CLICK (or Ctrl-click) DISABLES IT - takes it out of the
    sequencer's cycle, which is how the bar plays 3/4 or 7/8. That is a
    second parameter, not this control's value, so the pad does not set
    it: it calls the handler and the editor makes the edit, then tells
    the pad through setDisabled - the same path a host's automation or a
    loaded project takes. A disabled step has no key: a dashed hollow
    where it was, a dash for its LED, its number faint, and its hit (if
    it has one) kept as the outline of a lens, so enabling it again shows
    what will come back. */
class StepPad : public VSTGUI::CControl
{
public:
	StepPad (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	         const std::string& number);

	void setPlayhead (bool here);
	bool playhead () const { return mPlayhead; }

	void setDisabled (bool disabled);
	bool disabled () const { return mDisabled; }

	/** Called on a right-click or Ctrl-click. */
	void setDisableHandler (std::function<void ()> handler) { mDisableHandler = std::move (handler); }

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;

	CLASS_METHODS (StepPad, VSTGUI::CControl)

private:
	std::string mNumber;
	bool mPlayhead = false;
	bool mDisabled = false;
	std::function<void ()> mDisableHandler;
};

//------------------------------------------------------------------------
/** RUN: a black push button the size of the top of its rectangle, a red
    lamp lit while it is on and its name; under it, a status line from a
    formatter (readoutText: "off" / "armed" / "running"). Click toggles. */
class ToggleButton : public VSTGUI::CControl
{
public:
	ToggleButton (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	              const std::string& name);

	void setFormatter (std::function<std::string (float)> formatter) { mFormatter = std::move (formatter); }

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;

	CLASS_METHODS (ToggleButton, VSTGUI::CControl)

	/** The button's height; the status line is under it. */
	static constexpr VSTGUI::CCoord kButtonHeight = 34.;

private:
	std::string mName;
	std::function<std::string (float)> mFormatter;
};

//------------------------------------------------------------------------
/** ONE LINE OF LETTERING whose words change - the engine line, the
    velocity lines, the mix gains, the last strike. Vertically centred in
    the view, aligned as asked. Mouse-disabled. */
class TextLine : public VSTGUI::CView
{
public:
	TextLine (const VSTGUI::CRect& size, const Lettering::Style& style, const VSTGUI::CColor& colour,
	          Align align = Align::Left);
	void setText (const std::string& text);
	const std::string& getText () const { return mText; }
	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (TextLine, VSTGUI::CView)

private:
	Lettering::Style mStyle;
	VSTGUI::CColor mColour;
	Align mAlign;
	std::string mText;
};

//------------------------------------------------------------------------
} // namespace Panel
} // namespace FilterDrum
