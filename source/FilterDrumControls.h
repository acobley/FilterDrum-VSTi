//------------------------------------------------------------------------
// FilterDrum - custom VSTGUI controls
//
// LIFTED from ~/DXi-DEv/Project6-VSTi/source/Project6Controls.*, which
// came from VocalFilter-VSTi, which came from
// SpyBand-VSTi/source/SpyBandControls.*. The only change on this hop is
// the NAMESPACE; not one line of drawing or mouse handling differs from
// Project6's copy, which is what makes a diff against any of the four
// show only what genuinely differs.
//
// THE CLASS NAMES ARE DELIBERATELY UNCHANGED. SpySlider is still
// SpySlider, so `diff` against either of the other two copies shows only
// what genuinely differs, and a fix made in any of them can be carried to
// the others by hand. Renaming them would buy tidiness and cost that.
//
// Two things to know before editing this file:
//
//   * kValue IS NOT THE DXi's RED. The original is (192, 50, 50) and
//     SpyBand still uses it; VocalFilter changed the readouts to
//     near-white so they would not compete with a display's saturated
//     traces, and that choice is carried here. IT IS THE ONE COLOUR THAT
//     DIFFERS between the SpyBand copy and this one - keep it in mind
//     when diffing them.
//
//   * NONE OF THESE CONTROLS USES A BITMAP. The DXi property page they
//     descend from drew everything with GDI rectangles and text over the
//     panel, which is the whole reason they port at all, and why this
//     file needs no artwork in resource/. Keep any new control the same
//     way and the panel stays resolution-independent for free.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/vstgui.h"

#include <functional>
#include <string>
#include <vector>

namespace FilterDrum {

//------------------------------------------------------------------------
// The original's palette
//------------------------------------------------------------------------
namespace Colours {

const VSTGUI::CColor kBarLight   (200, 200, 200, 255);  // Draw3dRect top-left
const VSTGUI::CColor kBarHigh    (255, 255, 255, 255);  // Draw3dRect bottom-right
const VSTGUI::CColor kBarFill    (100, 100, 100, 255);
const VSTGUI::CColor kLabel      ( 50, 255,  50, 255);  // SetTextColor, green
const VSTGUI::CColor kValue      (232, 232, 232, 255);  // DrawTheText - see the banner
const VSTGUI::CColor kLampOn     (255,   0,   0, 255);
const VSTGUI::CColor kLampOff    (  0,   0,   0, 255);
const VSTGUI::CColor kLampFrame  (100, 100, 100, 255);
const VSTGUI::CColor kGrid       (200, 200, 200, 255);
const VSTGUI::CColor kGridBorder (100, 255, 100, 255);
const VSTGUI::CColor kOuterBorder(100, 100, 100, 255);
const VSTGUI::CColor kPin        (255,   0,   0, 255);
const VSTGUI::CColor kTrace      (127, 200, 255, 255);  // DrawArea's polyline
const VSTGUI::CColor kTraceVcf   ( 60, 255,  90, 255);  // the filter envelope
const VSTGUI::CColor kTraceVca   (255,  70,  70, 255);  // the amp envelope
const VSTGUI::CColor kPlate      (  0,   0,   0, 190);  // the envelope display ground
const VSTGUI::CColor kGroupFrame (120, 128, 120, 255);  // a control group's border
const VSTGUI::CColor kDrumFrame  ( 90, 150,  90, 255);  // the outer box round a drum

} // namespace Colours

/** MFC's CreatePointFont(80) - Arial at 8 points. */
VSTGUI::CFontRef panelFont ();

/** The same face one and two sizes down, for a label too long for its
    control. Six of the DXi's labels are wider than the control they name -
    "Unvoiced Noise Level" wants 95 pixels and has 82 - and DT_WORDBREAK
    wrapped them into an 11-pixel band, which clipped the second line.
    Dropping a size instead is what these are for. */
VSTGUI::CFontRef panelFontSmall ();
VSTGUI::CFontRef panelFontTiny ();

//------------------------------------------------------------------------
/** A SlideSpin in its ordinary mode: drag left and right.

    The DXi moved the value by ONE unit of a 0..100 range per pixel of
    horizontal movement, in either direction, with no absolute
    positioning - clicking did not jump the value to the pointer. That is
    preserved, because on a control 69 pixels wide an absolute drag would
    make every setting a coarse one. */
class SpySlider : public VSTGUI::CControl
{
public:
	SpySlider (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);

	/** The green text under the bar - the DXi's SetLabel. */
	void setLabel (const std::string& label);

	/** The red text across the middle - the DXi's SetValue, which the
	    property page filled in on a timer with a frequency reading. Empty
	    means the numeric value is shown instead, which is what the DXi
	    did when m_Value was empty. */
	void setValueText (const std::string& text);

	/** The 10 x 10 lamp in the top-left corner - SetUseIndicator. */
	void setUseIndicator (bool use);
	void setIndicator (bool on);
	bool indicator () const { return mIndicator; }

	/** How the numeric value reads when there is no value text. Given the
	    NORMALISED value; the editor hands it the parameter's own
	    formatting so the panel and the host cannot disagree. */
	void setFormatter (std::function<std::string (float)> formatter);

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySlider, VSTGUI::CControl)

protected:
	void drawLamp (VSTGUI::CDrawContext* context);
	void drawBar (VSTGUI::CDrawContext* context, double fraction, bool fill);
	void drawLabel (VSTGUI::CDrawContext* context, const std::string& text,
	                const VSTGUI::CColor& colour);
	/** Draw `text` centred at the TOP of `band`, dropping a font size
	    rather than letting it run past the edges. */
	void drawFitted (VSTGUI::CDrawContext* context, const std::string& text,
	                 const VSTGUI::CRect& band, const VSTGUI::CColor& colour);

	std::string mLabel;
	std::string mValueText;
	std::function<std::string (float)> mFormatter;
	bool mUseIndicator = false;
	bool mIndicator = false;

	bool mDragging = false;
	VSTGUI::CPoint mLastPoint;
};

//------------------------------------------------------------------------
/** A SlideSpin in two-state mode: a click toggles it.

    The bar fills the whole width when on and disappears when off, and the
    text under it is the name of the state rather than a label - "Use
    Sample" against "Interlace". */
class SpyToggle : public SpySlider
{
public:
	SpyToggle (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);

	void setStateNames (const std::string& off, const std::string& on);

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpyToggle, SpySlider)

private:
	std::string mNames[2];
};

//------------------------------------------------------------------------
/** A SlideSpin in multi-state mode: an outlined box with the name of the
    current value across it.

    LEFT CLICK STEPS DOWN, RIGHT CLICK STEPS UP, and both wrap. Ctrl-click
    counts as a right click, which is the macOS convention and a fallback
    for hosts that keep the right button to themselves.

    NOT the DXi, which had no click behaviour at all: its vertical mode
    needed the pointer to move 25 pixels before it did anything, on a
    control 18 pixels tall, so the control read as dead until you happened
    to drag it. See ENGINEERING-NOTES section 3.

    The drag is still there and still works the original's way round -
    DOWN ADVANCES, because the DXi decremented a counter it then reported
    as `max - count`, which is the opposite of the usual convention and is
    preserved. The wheel advances upwards, one position per click. */
class SpySelector : public SpySlider
{
public:
	SpySelector (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);

	void setNames (const std::vector<std::string>& names);

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpySelector, SpySlider)

private:
	int currentIndex () const;

	std::vector<std::string> mNames;
	VSTGUI::CCoord mAnchorY = 0.;
	bool mMoved = false;
	bool mStepUp = false;
};

//------------------------------------------------------------------------
/** A momentary push button, for recalling a preset.

    NOT in SpyBand - the nearest thing there was SpyFileButton, which is a
    SlideSpin with its indicator turned on and a click handler on the part
    that is not the lamp. This is that idea with the lamp taken off and
    the parameter taken away.

    It is a CControl only to inherit SpySlider's text fitting; it carries
    NO TAG and never calls valueChanged, beginEdit or endEdit, so a host
    sees nothing when it is clicked except the parameters the handler then
    writes. Every mouse handler is overridden for that reason - SpySlider's
    would drag a value that is not there.

    The click fires on mouse UP, and only if the pointer is still inside:
    pressing a vowel and sliding off it is how you change your mind. */
class SpyPresetButton : public SpySlider
{
public:
	SpyPresetButton (const VSTGUI::CRect& size, const std::string& name);

	void setHandler (std::function<void ()> handler);

	/** Lit, because the host - or this panel - has this vowel selected.
	    A push button that is also a state indicator: the Vowel parameter
	    can be moved by automation with nobody touching the panel, and a
	    row of buttons that did not show which one was live would be
	    lying. */
	void setSelected (bool selected);
	bool selected () const { return mSelected; }

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpyPresetButton, SpySlider)

private:
	std::string mName;
	std::function<void ()> mHandler;
	bool mPressed = false;
	bool mInside = false;
	bool mSelected = false;
};

//------------------------------------------------------------------------
/** A VERTICAL fader, for the crossfader between the two drums.

    THE ONLY CONTROL IN THIS FILE THAT IS NOT LIFTED. The DXi property
    page these descend from had a vertical mode on its SlideSpin, but it
    needed 25 pixels of travel before it moved at all - see the note on
    SpySelector - so there was nothing worth carrying across. This is
    written to match their drawing conventions rather than ported from
    them: no bitmap, a 3d-rect groove, text fitted with the inherited
    drawFitted().

    UP INCREASES, which is the only sane convention for a fader and the
    opposite of what the DXi's vertical SlideSpin did. The drag is
    RELATIVE, like SpySlider's - clicking does not jump the value to the
    pointer - so a nudge is possible on a control this narrow.

    It carries a name at each END rather than one label underneath,
    because a crossfader's two extremes are the information: the top name
    is what you get at 1.0 and the bottom name what you get at 0.0. */
class SpyFader : public SpySlider
{
public:
	SpyFader (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag);

	/** The names at the two ends. `top` is shown at value 1.0. */
	void setEndNames (const std::string& top, const std::string& bottom);

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpyFader, SpySlider)

private:
	std::string mTop;
	std::string mBottom;
};

//------------------------------------------------------------------------
/** One sequencer step: a boxed cell with its lamp centred above the
    number, and a bar across the bottom when the step is on.

    NOT LIFTED - the second control in this file that is not, after
    SpyFader, and for a related reason: the DXi's SlideSpin put its lamp
    hard in the control's TOP-LEFT CORNER. On a 69-pixel property-page
    control that reads as a corner indicator; on a 30-pixel step switch
    it sits ten pixels left of the centred number, so a row of sixteen
    reads as a column of lamps that does not line up with the column of
    switches. That is exactly what it looked like.

    So this centres the lamp over its own switch and draws a BOX round
    the pair, which is what makes each step read as one cell rather than
    as a lamp and a switch that happen to be near each other.

    A CLICK TOGGLES, like SpyToggle - there is no drag. A step is on or
    off and nothing in between, and a relative drag on a control this
    narrow would be a way to change the wrong one. */
class SpyStepSwitch : public SpySlider
{
public:
	SpyStepSwitch (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	               int32_t tag);

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SpyStepSwitch, SpySlider)
};

//------------------------------------------------------------------------
/** THE ENVELOPE DISPLAY - two AR curves on one time axis.

    Taken from ForTran's FtCurveView (dark plate, caption top left,
    figure top right, fixed full scale) with one change that is the
    whole reason it exists: it holds TWO series, not one, and draws them
    against a SHARED horizontal axis.

    THE SHARED AXIS IS THE POINT. Per-curve scaling - which is what you
    get by putting two FtCurveViews side by side - would draw a 45 ms
    amp envelope and a 4 s filter envelope as the same picture, and the
    single most useful thing this display can tell you is which of the
    two outlasts the other. On a shared axis a filter release that runs
    past the amp's is a green line still descending after the red one
    has reached the floor, which is a fault you can see from across the
    room.

    BOTH CURVES ARE DRAWN FULL HEIGHT, because the curve is the SHAPE
    and the shapes are what want comparing. They were scaled by their
    Amount controls once, and it was the wrong call: the VCF Amount is
    kept low in normal use - a large one is a siren sweep, not a drum -
    so the filter envelope got drawn as a flat smear along the bottom
    edge exactly when it most needed looking at.

    THE AMOUNTS ARE TWO MARKER LINES instead, short horizontals a
    quarter of the plot wide at the height each Amount corresponds to,
    in the same colours as the curves. That keeps the amounts on the
    display without letting either of them set the scale.

    MOUSE-DISABLED. It is a readout, not a control; a click here should
    fall through to the frame rather than do something. */
class SpyEnvelopeView : public VSTGUI::CView
{
public:
	explicit SpyEnvelopeView (const VSTGUI::CRect& size);

	/** The two series, in order: the VCF curve then the VCA curve. Both
	    are 0..1 and both are sampled over the SAME span of time, which
	    the caller establishes - see traceDrumEnvelopes(). Passing two
	    different spans would silently produce a lie. */
	void setCurves (const float* vcf, const float* vca, int count);

	/** Top left: what this display is. Top right: how long its axis is.
	    Either may be empty. */
	void setCaption (const std::string& caption);
	void setAnnotation (const std::string& annotation);

	/** The two Amount marker lines, 0..1 of full height.

	    The caller normalises: a linear gain for the VCA, and
	    |octaves| / kMaxEnvOctaves for the VCF. A negative VCF Amount
	    marks at the same height as its positive twin - the depth is the
	    same, it is the direction that differs - and the SIGN IS IN THE
	    LEGEND, where there is room to print it. */
	void setAmounts (double vcf, double vca);

	/** The legend along the bottom, each word in its own trace colour.

	    IT CARRIES THE FIGURES, which is why it is a string and not two
	    fixed words: "VCF -3.6oct" says both how deep the sweep is and
	    that it runs downwards, and a marker line cannot say the second
	    of those. An inverted envelope is the SAME SHAPE as an upright
	    one, so neither the curve nor the marker can show the difference
	    and the text has to. */
	void setLegend (const std::string& vcf, const std::string& vca);

	void draw (VSTGUI::CDrawContext* context) override;

	CLASS_METHODS (SpyEnvelopeView, VSTGUI::CView)

private:
	void drawTrace (VSTGUI::CDrawContext* context, const std::vector<float>& data,
	                const VSTGUI::CColor& colour, VSTGUI::CCoord left,
	                VSTGUI::CCoord width, VSTGUI::CCoord top,
	                VSTGUI::CCoord height) const;

	void drawAmountMarker (VSTGUI::CDrawContext* context, double amount,
	                       const VSTGUI::CColor& colour, VSTGUI::CCoord left,
	                       VSTGUI::CCoord width, VSTGUI::CCoord top,
	                       VSTGUI::CCoord height) const;

	std::vector<float> mVcf;
	std::vector<float> mVca;
	double mVcfAmount = 0.0;
	double mVcaAmount = 0.0;
	std::string mCaption;
	std::string mAnnotation;
	std::string mVcfLegend;
	std::string mVcaLegend;
};

//------------------------------------------------------------------------
/** A GROUP BOX: a rectangle with its name sitting on the top edge.

    The panel had no grouping at all beyond position. Seven controls in
    a row and then four in a row underneath is a layout you can read
    once you know what it is, but it gives a newcomer nothing to
    navigate by - and with three rows per drum and two drums, position
    alone stopped being enough.

    THE TITLE BREAKS THE BORDER rather than sitting above it or inside
    it. Above costs a whole line of panel per group, six of them here;
    inside eats the space the controls need. Breaking the line costs
    nothing and is what a group box has looked like since Windows 3.

    DRAWN BEHIND EVERYTHING. It is added to the frame before the
    controls it encloses, is mouse-disabled, and never fills - so it is
    a line on the background and a click anywhere inside it reaches the
    control that is really there. A filled box would have to be
    transparent-aware and would still swallow the panel's own ground. */
class SpyGroupBox : public VSTGUI::CView
{
public:
	/** `title` may be empty, and then the border is unbroken - which is
	    what the outer per-drum boxes want, because the drum's own
	    heading is already inside them. */
	SpyGroupBox (const VSTGUI::CRect& size, const std::string& title,
	             const VSTGUI::CColor& frame);

	void draw (VSTGUI::CDrawContext* context) override;

	CLASS_METHODS (SpyGroupBox, VSTGUI::CView)

private:
	std::string    mTitle;
	VSTGUI::CColor mFrame;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
