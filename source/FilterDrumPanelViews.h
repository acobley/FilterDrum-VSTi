//------------------------------------------------------------------------
// FilterDrum - the Design 2.0 panel's views
//
// CARRIED OVER FROM R2Synth (source/R2SynthPanelViews.*, branch
// UpdatedUI), whose "signal flow" panel this design is built on, so the
// two instruments share one look and one feel under the mouse. Changes
// from R2Synth's are marked; the new views are the sequencer's.
//
//   Backdrop       everything that never changes, from FilterDrumLayout.h.
//                  One view, behind the rest, mouse-disabled.
//   Knob           a rotary control with a value arc in its colour, its
//                  reading and its label underneath. (+ reversed, for the
//                  mix: D1 is on the left.)
//   SegmentSwitch  one segment per named position; click one.
//   Scope          a dark screen with a grid and one or two traces.
//                  (+ a dashed companion, + a coloured caption.)
//   Lamp           a dot and a name: SELF-OSC, and the STRIKE lamps.
//                  (+ any colour, + a level, so a strike can fade.)
//   StepPad        NEW: one sequencer step. Click toggles; outlined
//                  while the playhead is on it.
//   ToggleButton   NEW: Run. Click toggles; a lamp, a name and a status
//                  line ("off", "armed", "running").
//
// THE SAME TWO RULES AS R2Synth. No bitmaps - everything is paths and
// text, so resource/ needs no artwork. And no numbers made here: every
// reading comes in through a formatter the editor hands over, and every
// curve through setCurves() from the shared trace functions in
// FilterDrumDsp.h.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumLayout.h"

#include "vstgui/vstgui.h"

#include <functional>
#include <string>
#include <vector>

namespace FilterDrum {
namespace Panel {

//------------------------------------------------------------------------
// The palette - the design's tokens, R2Synth's values
//------------------------------------------------------------------------
namespace Palette {

const VSTGUI::CColor kGround    ( 20,  21,  24, 255);
const VSTGUI::CColor kHeader    ( 26,  28,  32, 255);
const VSTGUI::CColor kBorder    ( 42,  45,  51, 255);
const VSTGUI::CColor kSection   ( 28,  30,  34, 255);
const VSTGUI::CColor kRule      ( 36,  38,  43, 255);
const VSTGUI::CColor kTrack     ( 46,  49,  55, 255);
const VSTGUI::CColor kCap       ( 42,  44,  49, 255);
const VSTGUI::CColor kCapEdge   ( 60,  64,  71, 255);
const VSTGUI::CColor kText      (236, 234, 228, 255);
const VSTGUI::CColor kTitle     (201, 204, 210, 255);
const VSTGUI::CColor kMuted     (156, 161, 170, 255);
const VSTGUI::CColor kSubtle    (126, 131, 140, 255);
const VSTGUI::CColor kDim       ( 74,  78,  86, 255);
const VSTGUI::CColor kScreen    ( 15,  16,  18, 255);
const VSTGUI::CColor kScreenEdge( 38,  41,  46, 255);
const VSTGUI::CColor kGrid      ( 26,  29,  33, 255);
const VSTGUI::CColor kSegBack   ( 18,  19,  22, 255);
const VSTGUI::CColor kPad       ( 36,  37,  44, 255);
const VSTGUI::CColor kPadEdge   ( 48,  50,  58, 255);
const VSTGUI::CColor kLampOn    (255, 138,  61, 255);
const VSTGUI::CColor kLampOff   ( 58,  42,  32, 255);
const VSTGUI::CColor kLampEdge  ( 90,  58,  40, 255);

/** A part's colour: noise amber, filter blue, amp mint, sequencer violet. */
VSTGUI::CColor accent (Layout::Accent a);

/** The same at `alpha` (0..255). */
VSTGUI::CColor accentAlpha (Layout::Accent a, uint8_t alpha);

/** The text drawn ON an accent-coloured fill: a near-black of the same
    hue, so a lit segment or step reads at a glance. */
VSTGUI::CColor accentInk (Layout::Accent a);

} // namespace Palette

//------------------------------------------------------------------------
// The faces. On macOS CoreText falls back to the system face if one is
// missing, so a name here can never leave text undrawn.
//------------------------------------------------------------------------
VSTGUI::CFontRef fontWordmark ();   // Avenir Next Condensed, bold 23
VSTGUI::CFontRef fontTitle ();      // Helvetica Neue, bold 13
VSTGUI::CFontRef fontSubtitle ();   // Helvetica Neue 12
VSTGUI::CFontRef fontLabel ();      // Helvetica Neue, bold 10 - capitals
VSTGUI::CFontRef fontValue ();      // Menlo 10 - every reading
VSTGUI::CFontRef fontNote ();       // Helvetica Neue 11
VSTGUI::CFontRef fontBadge ();      // Menlo, bold 12
VSTGUI::CFontRef fontButton ();     // Helvetica Neue, bold 13

/** A string in capitals. ASCII only is changed; UTF-8 passes through. */
std::string upper (const std::string& s);

//------------------------------------------------------------------------
class Backdrop : public VSTGUI::CView
{
public:
	explicit Backdrop (const VSTGUI::CRect& size);
	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (Backdrop, VSTGUI::CView)
};

//------------------------------------------------------------------------
/** A ROTARY KNOB - R2Synth's. 270 degrees of travel; the value arc in
    its colour from the left end or, BIPOLAR, from the top.

    THE WHOLE RECTANGLE IS THE TARGET, reading and label included.
      drag          vertically; up increases; 200 pixels is the range
      Shift         ten times finer, drag and wheel alike
      double-click  the parameter's default
      wheel         one per cent a click

    REVERSED (new here): the arc and the pointer are drawn for
    1 - value, and up drags the value DOWN - so "up turns it clockwise"
    still holds. For the mix, whose 100 % is drum 1 and whose drum 1 is
    printed on the left. */
class Knob : public VSTGUI::CControl
{
public:
	Knob (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	      Layout::Accent accent, int diameter, bool bipolar, bool reversed = false);

	void setLabel (const std::string& label) { mLabel = upper (label); }
	void setFormatter (std::function<std::string (float)> formatter) { mFormatter = std::move (formatter); }

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
	Layout::Accent mAccent;
	int  mDiameter;
	bool mBipolar;
	bool mReversed;
	std::string mLabel;
	std::function<std::string (float)> mFormatter;

	bool mDragging = false;
	VSTGUI::CCoord mLastY = 0.;
};

//------------------------------------------------------------------------
/** A SEGMENTED SWITCH - R2Synth's. Positions spread evenly over 0..1,
    the way an Enum parameter's normalised values are. */
class SegmentSwitch : public VSTGUI::CControl
{
public:
	SegmentSwitch (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	               Layout::Accent accent);

	void setNames (const std::vector<std::string>& names) { mNames = names; invalid (); }

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SegmentSwitch, VSTGUI::CControl)

private:
	int count () const { return static_cast<int> (mNames.size ()); }
	int currentIndex () const;
	void select (int index);

	Layout::Accent mAccent;
	std::vector<std::string> mNames;
};

//------------------------------------------------------------------------
/** A SCREEN - R2Synth's: a dark plate, a grid, a caption top left, a
    figure top right, up to two traces 0..1 (y up, CLAMPED, not scaled),
    and an optional dot on the main trace.

    New here: the companion can be DASHED (the filter at the strike; the
    filter envelope under the amp's), and the caption can take a colour
    (the filter envelope's depth, in the filter's blue). */
class Scope : public VSTGUI::CView
{
public:
	explicit Scope (const VSTGUI::CRect& size);

	void setColours (const VSTGUI::CColor& main, const VSTGUI::CColor& companion);
	void setFill (bool fill) { mFill = fill; }
	void setCompanionDashed (bool dashed) { mCompanionDashed = dashed; }
	void setCurves (const float* main, const float* companion, int count);
	void setCaption (const std::string& caption) { mCaption = caption; }
	void setCaptionColour (const VSTGUI::CColor& colour) { mCaptionColour = colour; }
	void setAnnotation (const std::string& annotation) { mAnnotation = annotation; }

	/** The dot, at (x, y) 0..1 of the plot; negative x for none. */
	void setMarker (double x, double y) { mMarkerX = x; mMarkerY = y; }

	void draw (VSTGUI::CDrawContext* context) override;

	CLASS_METHODS (Scope, VSTGUI::CView)

private:
	VSTGUI::CRect plotRect () const;
	void drawTrace (VSTGUI::CDrawContext* context, const std::vector<float>& data,
	                const VSTGUI::CColor& colour, VSTGUI::CCoord width, bool fill, bool dashed) const;

	VSTGUI::CColor mMain;
	VSTGUI::CColor mCompanion;
	VSTGUI::CColor mCaptionColour;
	bool mFill = false;
	bool mCompanionDashed = false;
	std::vector<float> mMainData;
	std::vector<float> mCompanionData;
	std::string mCaption;
	std::string mAnnotation;
	double mMarkerX = -1.0;
	double mMarkerY = 0.0;
};

//------------------------------------------------------------------------
/** A LAMP: a dot that glows, and its name to the right, the pair
    right-aligned in the rectangle. Mouse-disabled.

    setOn() is the SELF-OSC lamp's whole interface. The STRIKE lamps use
    setLevel(), 0..1, so a hit can fade rather than blink; any level
    above zero is "on". */
class Lamp : public VSTGUI::CView
{
public:
	Lamp (const VSTGUI::CRect& size, const std::string& name);

	void setColour (const VSTGUI::CColor& colour) { mColour = colour; invalid (); }
	void setOn (bool on) { setLevel (on ? 1.f : 0.f); }
	void setLevel (float level);
	bool isOn () const { return mLevel > 0.f; }
	float level () const { return mLevel; }

	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (Lamp, VSTGUI::CView)

private:
	std::string mName;
	VSTGUI::CColor mColour;
	float mLevel = 0.f;
};

//------------------------------------------------------------------------
/** ONE SEQUENCER STEP. Lit in the sequencer's violet when the step is
    on; a white outline while the playhead is on it - the two are
    separate on purpose, so a step that is off still shows the playhead
    passing. Click toggles. The step's number is drawn at its foot.

    RIGHT-CLICK (or Ctrl-click) DISABLES IT - takes it out of the
    sequencer's cycle, which is how the bar plays 3/4 or 7/8. That is a
    second parameter, not this control's value, so the pad does not set
    it: it calls the handler and the editor makes the edit, then tells
    the pad through setDisabled - the same path a host's automation or a
    loaded project takes. A disabled pad is drawn hollow and dashed,
    with its hit (if it has one) kept as a faint violet ring, so
    enabling it again shows what will come back. */
class StepPad : public VSTGUI::CControl
{
public:
	StepPad (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	         Layout::Accent accent, const std::string& number);

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
	Layout::Accent mAccent;
	std::string mNumber;
	bool mPlayhead = false;
	bool mDisabled = false;
	std::function<void ()> mDisableHandler;
};

//------------------------------------------------------------------------
/** A TOGGLE BUTTON - Run. A lamp, the name, and a status line under it
    from a formatter (the editor's readoutFor: "off" / "armed" /
    "running"). Tinted in its colour while on. Click toggles. */
class ToggleButton : public VSTGUI::CControl
{
public:
	ToggleButton (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	              Layout::Accent accent, const std::string& name);

	void setFormatter (std::function<std::string (float)> formatter) { mFormatter = std::move (formatter); }

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;

	CLASS_METHODS (ToggleButton, VSTGUI::CControl)

private:
	Layout::Accent mAccent;
	std::string mName;
	std::function<std::string (float)> mFormatter;
};

//------------------------------------------------------------------------
} // namespace Panel
} // namespace FilterDrum
