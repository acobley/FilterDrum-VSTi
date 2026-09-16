//------------------------------------------------------------------------
// FilterDrum - custom VSTGUI controls
//
// LIFTED from ~/DXi-DEv/VocalFilter-VSTi/source/VocalFilterControls.*,
// which was itself lifted from SpyBand-VSTi/source/SpyBandControls.*. The
// only change on this hop is the NAMESPACE; not one line of drawing or
// mouse handling differs from VocalFilter's copy.
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

#include "FilterDrumControls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace FilterDrum {

namespace {

/** The DXi's bar geometry, in pixels from the control's own edges
    (SlideSpin::PaintBk). */
constexpr CCoord kBarBottomInset = 3.;
constexpr CCoord kBarHeight      = 12.;   // rect.bottom-15 .. rect.bottom-3
constexpr CCoord kLabelTop       = 17.;   // rect.bottom-17
constexpr CCoord kLabelBottom    = 6.;    // rect.bottom-6
constexpr CCoord kLampSize       = 10.;

/** The DXi moved one unit of a 0..100 range per pixel. */
constexpr float kUnitsPerPixel = 1.f / 100.f;

/** The vertical selector needed 25 pixels of movement per step. */
constexpr CCoord kSelectorStep = 25.;

/** MFC's Draw3dRect: a light line down the top and left, a highlight up
    the bottom and right. */
void draw3dRect (CDrawContext* context, const CRect& r,
                 const CColor& topLeft, const CColor& bottomRight)
{
	if (r.getWidth () <= 0. || r.getHeight () <= 0.)
		return;

	context->setLineWidth (1.);
	context->setFrameColor (topLeft);
	context->drawLine (CPoint (r.left, r.top), CPoint (r.right - 1., r.top));
	context->drawLine (CPoint (r.left, r.top), CPoint (r.left, r.bottom - 1.));

	context->setFrameColor (bottomRight);
	context->drawLine (CPoint (r.left, r.bottom - 1.), CPoint (r.right - 1., r.bottom - 1.));
	context->drawLine (CPoint (r.right - 1., r.top), CPoint (r.right - 1., r.bottom - 1.));
}

} // namespace

//------------------------------------------------------------------------
CFontRef panelFont ()
{
	// MFC's CreatePointFont(80) is Arial at 8 points, which at the 96 dpi
	// the dialog was designed for is 11 pixels.
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Arial", 11);
	return font;
}

CFontRef panelFontSmall ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Arial", 10);
	return font;
}

CFontRef panelFontTiny ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Arial", 9);
	return font;
}

//------------------------------------------------------------------------
// SpySlider
//------------------------------------------------------------------------
SpySlider::SpySlider (const CRect& size, IControlListener* listener, int32_t tag)
: CControl (size, listener, tag, nullptr)
{
	setWantsFocus (true);
}

void SpySlider::setLabel (const std::string& label)
{
	if (mLabel == label)
		return;
	mLabel = label;
	invalid ();
}

void SpySlider::setValueText (const std::string& text)
{
	if (mValueText == text)
		return;
	mValueText = text;
	invalid ();
}

void SpySlider::setUseIndicator (bool use)
{
	mUseIndicator = use;
	invalid ();
}

void SpySlider::setIndicator (bool on)
{
	if (mIndicator == on)
		return;
	mIndicator = on;
	invalid ();
}

void SpySlider::setFormatter (std::function<std::string (float)> formatter)
{
	mFormatter = std::move (formatter);
}

//------------------------------------------------------------------------
void SpySlider::drawLamp (CDrawContext* context)
{
	if (! mUseIndicator)
		return;

	const CRect r = getViewSize ();
	CRect lamp (r.left, r.top, r.left + kLampSize, r.top + kLampSize);
	draw3dRect (context, lamp, Colours::kLampFrame, Colours::kLampFrame);

	lamp.inset (2., 2.);
	context->setFillColor (mIndicator ? Colours::kLampOn : Colours::kLampOff);
	context->drawRect (lamp, kDrawFilled);
}

void SpySlider::drawBar (CDrawContext* context, double fraction, bool fill)
{
	const CRect r = getViewSize ();

	CRect bar (r.left,
	           r.bottom - kBarBottomInset - kBarHeight,
	           r.left + r.getWidth () * std::clamp (fraction, 0.0, 1.0),
	           r.bottom - kBarBottomInset);

	draw3dRect (context, bar, Colours::kBarLight, Colours::kBarHigh);

	if (! fill)
		return;

	bar.inset (1., 1.);
	if (bar.getWidth () <= 0. || bar.getHeight () <= 0.)
		return;
	context->setFillColor (Colours::kBarFill);
	context->drawRect (bar, kDrawFilled);
}

void SpySlider::drawFitted (CDrawContext* context, const std::string& text,
                            const CRect& band, const CColor& colour)
{
	if (text.empty ())
		return;

	// Windows drew both of these with DT_CENTER and no DT_VCENTER, so they
	// sat at the TOP of the band they were given, not in the middle of it.
	// Centring them vertically instead puts the red value straight through
	// the green label - which is what the first render of this panel showed.
	CFontRef font = panelFont ();
	context->setFont (font);
	if (context->getStringWidth (text.c_str ()) > band.getWidth ())
	{
		font = panelFontSmall ();
		context->setFont (font);
		if (context->getStringWidth (text.c_str ()) > band.getWidth ())
		{
			font = panelFontTiny ();
			context->setFont (font);
		}
	}

	const CCoord height = font->getSize () + 2.;
	const CRect line (band.left, band.top, band.right,
	                  std::min (band.top + height, band.bottom));

	context->setFontColor (colour);
	context->drawString (text.c_str (), line, kCenterText, true);
}

void SpySlider::drawLabel (CDrawContext* context, const std::string& text,
                           const CColor& colour)
{
	const CRect r = getViewSize ();
	drawFitted (context, text,
	            CRect (r.left, r.bottom - kLabelTop, r.right, r.bottom), colour);
}

//------------------------------------------------------------------------
void SpySlider::draw (CDrawContext* context)
{
	// The panel bitmap behind the control shows through: the DXi blitted
	// its parent's pixels and drew on top, and here the frame's background
	// has already been drawn under us. Nothing is painted over it but the
	// bar, the text and the lamp.
	drawBar (context, getValueNormalized (), true);
	drawLabel (context, mLabel, Colours::kLabel);
	drawLamp (context);

	std::string value = mValueText;
	if (value.empty () && mFormatter)
		value = mFormatter (getValueNormalized ());

	// The value goes at the TOP of the control and the label at the
	// bottom, which is where DT_CENTER without DT_VCENTER put them.
	drawFitted (context, value, getViewSize (), Colours::kValue);

	setDirty (false);
}

//------------------------------------------------------------------------
void SpySlider::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	// No absolute positioning: the DXi's slider moved by increments from
	// wherever it was, and on a control 69 pixels wide jumping to the
	// pointer would make every setting a coarse one.
	mDragging = true;
	mLastPoint = event.mousePosition;
	beginEdit ();
	event.consumed = true;
}

void SpySlider::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mDragging)
		return;

	const CCoord dx = event.mousePosition.x - mLastPoint.x;
	if (std::fabs (dx) < 1.)
		return;

	mLastPoint = event.mousePosition;

	const float scale = event.modifiers.has (ModifierKey::Shift) ? 0.1f : 1.f;
	setValueNormalized (std::clamp (
		getValueNormalized () + static_cast<float> (dx) * kUnitsPerPixel * scale,
		0.f, 1.f));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

void SpySlider::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mDragging)
		return;
	mDragging = false;
	endEdit ();
	event.consumed = true;
}

void SpySlider::onMouseCancelEvent (MouseCancelEvent& event)
{
	if (mDragging)
	{
		mDragging = false;
		endEdit ();
	}
	event.consumed = true;
}

void SpySlider::onMouseWheelEvent (MouseWheelEvent& event)
{
	const float step = event.modifiers.has (ModifierKey::Shift) ? 0.002f : 0.01f;
	beginEdit ();
	setValueNormalized (std::clamp (
		getValueNormalized () + static_cast<float> (event.deltaY) * step, 0.f, 1.f));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
// SpyToggle
//------------------------------------------------------------------------
SpyToggle::SpyToggle (const CRect& size, IControlListener* listener, int32_t tag)
: SpySlider (size, listener, tag)
{
}

void SpyToggle::setStateNames (const std::string& off, const std::string& on)
{
	mNames[0] = off;
	mNames[1] = on;
	invalid ();
}

void SpyToggle::draw (CDrawContext* context)
{
	const bool on = getValueNormalized () >= 0.5f;

	// All or nothing: the DXi's two-state scale was 1.0 or 0.0, never
	// anything between.
	drawBar (context, on ? 1.0 : 0.0, true);

	// A two-state control shows the NAME OF ITS STATE where a slider shows
	// its label, and shows no red value text at all.
	drawLabel (context, mNames[on ? 1 : 0], Colours::kLabel);
	drawLamp (context);

	setDirty (false);
}

void SpyToggle::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	beginEdit ();
	setValueNormalized (getValueNormalized () >= 0.5f ? 0.f : 1.f);
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

void SpyToggle::onMouseMoveEvent (MouseMoveEvent& event)
{
	// The DXi returned early from OnMouseMove for a two-state control, so
	// a drag across one does nothing.
	event.consumed = false;
}

void SpyToggle::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

void SpyToggle::onMouseWheelEvent (MouseWheelEvent& event)
{
	// SlideSpin::OnMouseWheel returns immediately for a two-state control,
	// and so does this. WITHOUT the override the inherited slider wheel
	// nudges a switch by a hundredth of its travel per click, so it takes
	// fifty clicks to flip and lands the parameter on values a two-state
	// control has no business holding.
	event.consumed = false;
}

//------------------------------------------------------------------------
// SpySelector
//------------------------------------------------------------------------
SpySelector::SpySelector (const CRect& size, IControlListener* listener, int32_t tag)
: SpySlider (size, listener, tag)
{
}

void SpySelector::setNames (const std::vector<std::string>& names)
{
	mNames = names;
	invalid ();
}

int SpySelector::currentIndex () const
{
	if (mNames.empty ())
		return 0;
	const int last = static_cast<int> (mNames.size ()) - 1;
	if (last <= 0)
		return 0;
	return std::clamp (static_cast<int> (getValueNormalized () * last + 0.5f), 0, last);
}

void SpySelector::draw (CDrawContext* context)
{
	// A multi-state control is a box the full height of the view with the
	// name of the current value across it: PaintBk took Bar.top from
	// rect.top and skipped the fill.
	const CRect r = getViewSize ();
	draw3dRect (context, CRect (r.left, r.top, r.right, r.bottom - kBarBottomInset),
	            Colours::kBarLight, Colours::kBarHigh);

	if (! mNames.empty ())
		drawFitted (context, mNames[static_cast<std::size_t> (currentIndex ())],
		            r, Colours::kValue);

	setDirty (false);
}

void SpySelector::onMouseDownEvent (MouseDownEvent& event)
{
	const bool right = event.buttonState.isRight ()
	                || event.modifiers.has (ModifierKey::Control);

	if (! event.buttonState.isLeft () && ! right)
		return;

	// Which way a click without a drag will step. Ctrl counts as a right
	// click: it is the macOS convention, and it is the fallback for a host
	// that keeps the right button for its own menu.
	mStepUp = right;
	mDragging = true;
	mMoved = false;
	mAnchorY = event.mousePosition.y;
	beginEdit ();
	event.consumed = true;
}

void SpySelector::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mDragging || mNames.size () < 2)
		return;

	const int last = static_cast<int> (mNames.size ()) - 1;
	int index = currentIndex ();

	// DOWN ADVANCES. The DXi decremented its counter when the pointer went
	// down, and reported max - counter, so down raised the value. Kept.
	if (event.mousePosition.y > mAnchorY + kSelectorStep)
	{
		mAnchorY = event.mousePosition.y;
		mMoved = true;
		index = std::min (index + 1, last);
	}
	else if (event.mousePosition.y < mAnchorY - kSelectorStep)
	{
		mAnchorY = event.mousePosition.y;
		mMoved = true;
		index = std::max (index - 1, 0);
	}
	else
	{
		return;
	}

	setValueNormalized (static_cast<float> (index) / static_cast<float> (last));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

void SpySelector::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mDragging)
		return;

	// A click that did not drag steps one position: LEFT DOWN, RIGHT UP,
	// both wrapping, so either button alone can reach every value. The DXi
	// had nothing here, which is why the control read as dead - its only
	// way in was a 25-pixel drag on an 18-pixel control. The drag is
	// untouched; this is purely additional. The same click-versus-drag
	// test the patch board uses - did the value actually move? - rather
	// than a timer.
	if (! mMoved && mNames.size () > 1)
	{
		const int positions = static_cast<int> (mNames.size ());
		const int index = (currentIndex () + (mStepUp ? 1 : positions - 1)) % positions;
		setValueNormalized (static_cast<float> (index)
		                    / static_cast<float> (positions - 1));
		valueChanged ();
		invalid ();
	}

	mDragging = false;
	mMoved = false;
	endEdit ();
	event.consumed = true;
}

void SpySelector::onMouseWheelEvent (MouseWheelEvent& event)
{
	// ONE STEP PER CLICK, which is what SlideSpin::OnMouseWheel did:
	// `count--` or `count++`, a whole position at a time.
	//
	// Without this the inherited slider wheel moved a hundredth of the
	// control's travel per click - and a four-position selector's step is
	// a THIRD of its travel, so it took seventeen clicks to change from
	// 9 Bands to 12 and left the parameter on values between the steps.
	// Wheel up raises the value, as it did.
	if (mNames.size () < 2)
		return;

	const int last = static_cast<int> (mNames.size ()) - 1;
	int index = currentIndex ();

	if (event.deltaY > 0.)
		index = std::min (index + 1, last);
	else if (event.deltaY < 0.)
		index = std::max (index - 1, 0);
	else
		return;

	beginEdit ();
	setValueNormalized (static_cast<float> (index) / static_cast<float> (last));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
// SpyPresetButton
//------------------------------------------------------------------------
SpyPresetButton::SpyPresetButton (const CRect& size, const std::string& name)
: SpySlider (size, nullptr, -1)
, mName (name)
{
}

void SpyPresetButton::setHandler (std::function<void ()> handler)
{
	mHandler = std::move (handler);
}

void SpyPresetButton::setSelected (bool selected)
{
	if (selected == mSelected)
		return;
	mSelected = selected;
	invalid ();
}

//------------------------------------------------------------------------
void SpyPresetButton::draw (CDrawContext* context)
{
	// The same outlined box a multi-state SlideSpin draws, so a row of
	// these sits on the panel as though the DXi had always had them.
	const CRect r = getViewSize ();
	const CRect box (r.left, r.top, r.right, r.bottom - kBarBottomInset);

	// Pressed swaps the 3d rect's two edges, which is what Windows did to
	// show a button down, and fills it so the state is obvious on a dark
	// panel where a one-pixel edge is not.
	const bool down = (mPressed && mInside);

	if (down || mSelected)
	{
		draw3dRect (context, box, Colours::kBarHigh, Colours::kBarLight);
		CRect fill (box);
		fill.inset (1., 1.);
		if (fill.getWidth () > 0. && fill.getHeight () > 0.)
		{
			context->setFillColor (Colours::kBarFill);
			context->drawRect (fill, kDrawFilled);
		}
	}
	else
	{
		draw3dRect (context, box, Colours::kBarLight, Colours::kBarHigh);
	}

	// Green for the live vowel, the panel's colour for a label that is
	// stating a fact; red otherwise, the colour it uses for a value you
	// can change.
	drawFitted (context, mName, r,
	            (down || mSelected) ? Colours::kLabel : Colours::kValue);

	setDirty (false);
}

//------------------------------------------------------------------------
void SpyPresetButton::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	mPressed = true;
	mInside = true;
	invalid ();
	event.consumed = true;
}

void SpyPresetButton::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mPressed)
		return;

	const bool inside = getViewSize ().pointInside (event.mousePosition);
	if (inside != mInside)
	{
		mInside = inside;
		invalid ();
	}
	event.consumed = true;
}

void SpyPresetButton::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mPressed)
		return;

	const bool fire = mInside && getViewSize ().pointInside (event.mousePosition);
	mPressed = false;
	mInside = false;
	invalid ();
	event.consumed = true;

	// LAST, because the handler rewrites nine parameters and the frame may
	// well be redrawn out from under this call.
	if (fire && mHandler)
		mHandler ();
}

void SpyPresetButton::onMouseCancelEvent (MouseCancelEvent& event)
{
	mPressed = false;
	mInside = false;
	invalid ();
	event.consumed = true;
}

void SpyPresetButton::onMouseWheelEvent (MouseWheelEvent&)
{
	// Nothing. A wheel over a push button should not do anything, and
	// SpySlider's would move a value that is not there.
}

//------------------------------------------------------------------------
// SpyFader - the vertical crossfader
//------------------------------------------------------------------------
namespace {

/** THE GROOVE IS A FIXED WIDTH, CENTRED - not an inset from the sides.

    An inset makes the groove as wide as whatever box the control is
    given, so a fader dropped into a slider-sized column comes out
    slider-shaped. A fader's groove is a groove whatever the control is
    wide, so it is measured from the centre and the control can be as
    narrow as its labels need. */
constexpr CCoord kFaderGrooveHalf = 8.;
constexpr CCoord kFaderKnobOver   = 7.;   // how far the knob overhangs it
constexpr CCoord kFaderEndBand    = 15.;
constexpr CCoord kFaderKnobHalf   = 4.;

/** Vertical travel per pixel. Coarser than SpySlider's 1/100 because a
    fader is tall: 1/160 gives a 160-pixel control one unit per pixel
    over its whole range, so the drag distance matches the groove. */
constexpr float kFaderUnitsPerPixel = 1.f / 160.f;

} // anonymous namespace

//------------------------------------------------------------------------
SpyFader::SpyFader (const CRect& size, IControlListener* listener, int32_t tag)
: SpySlider (size, listener, tag)
{
}

//------------------------------------------------------------------------
void SpyFader::setEndNames (const std::string& top, const std::string& bottom)
{
	mTop = top;
	mBottom = bottom;
	invalid ();
}

//------------------------------------------------------------------------
void SpyFader::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	// The two end names, at the top and bottom of the control. They are
	// what a crossfader's extremes mean, so they are drawn first and the
	// groove is fitted between them.
	drawFitted (context, mTop,
	            CRect (r.left, r.top, r.right, r.top + kFaderEndBand),
	            Colours::kLabel);
	drawFitted (context, mBottom,
	            CRect (r.left, r.bottom - kFaderEndBand, r.right, r.bottom),
	            Colours::kLabel);

	// The groove: a 3d rect the full height of the travel, like the
	// horizontal bar's, turned on its side. Centred and a fixed width -
	// see kFaderGrooveHalf.
	const CCoord mid = (r.left + r.right) * 0.5;
	const CRect groove (mid - kFaderGrooveHalf,
	                    r.top + kFaderEndBand + 2.,
	                    mid + kFaderGrooveHalf,
	                    r.bottom - kFaderEndBand - 2. - kFaderEndBand);

	draw3dRect (context, groove, Colours::kBarLight, Colours::kBarHigh);

	CRect inner = groove;
	inner.inset (1., 1.);
	if (inner.getWidth () > 0. && inner.getHeight () > 0.)
	{
		context->setFillColor (Colours::kBarFill);
		context->drawRect (inner, kDrawFilled);
	}

	// The knob. VALUE 1 IS AT THE TOP - up increases, which is the only
	// sane convention for a fader, so the travel is measured downwards
	// from the groove's top.
	const double value = std::clamp (static_cast<double> (getValueNormalized ()), 0.0, 1.0);
	const CCoord travel = groove.getHeight () - 2. * kFaderKnobHalf;
	const CCoord centre = groove.top + kFaderKnobHalf + (1.0 - value) * travel;

	CRect knob (groove.left - kFaderKnobOver, centre - kFaderKnobHalf,
	            groove.right + kFaderKnobOver, centre + kFaderKnobHalf);
	draw3dRect (context, knob, Colours::kBarHigh, Colours::kBarLight);
	knob.inset (1., 1.);
	if (knob.getWidth () > 0. && knob.getHeight () > 0.)
	{
		context->setFillColor (Colours::kGrid);
		context->drawRect (knob, kDrawFilled);
	}

	// The reading, in the band just above the bottom name.
	std::string value_text = mValueText;
	if (value_text.empty () && mFormatter)
		value_text = mFormatter (getValueNormalized ());

	drawFitted (context, value_text,
	            CRect (r.left, r.bottom - 2. * kFaderEndBand, r.right, r.bottom - kFaderEndBand),
	            Colours::kValue);

	setDirty (false);
}

//------------------------------------------------------------------------
void SpyFader::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	// Relative, like SpySlider: clicking does not jump the value to the
	// pointer, so a nudge is possible on a control this narrow.
	mDragging = true;
	mLastPoint = event.mousePosition;
	beginEdit ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpyFader::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (! mDragging)
		return;

	const CCoord dy = event.mousePosition.y - mLastPoint.y;
	if (std::fabs (dy) < 1.)
		return;

	mLastPoint = event.mousePosition;

	// NEGATED: screen y grows downwards and the fader's value grows
	// upwards. Getting this backwards is the classic vertical-slider bug
	// and it is why PORT-CHECKLIST.md phase 5 has a line about it.
	const float scale = event.modifiers.has (ModifierKey::Shift) ? 0.1f : 1.f;
	setValueNormalized (std::clamp (
		getValueNormalized () - static_cast<float> (dy) * kFaderUnitsPerPixel * scale,
		0.f, 1.f));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpyFader::onMouseUpEvent (MouseUpEvent& event)
{
	if (! mDragging)
		return;
	mDragging = false;
	endEdit ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpyFader::onMouseWheelEvent (MouseWheelEvent& event)
{
	const float scale = event.modifiers.has (ModifierKey::Shift) ? 0.1f : 1.f;
	const float step = static_cast<float> (event.deltaY) * 0.02f * scale;
	if (step == 0.f)
		return;

	beginEdit ();
	setValueNormalized (std::clamp (getValueNormalized () + step, 0.f, 1.f));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
// SpyStepSwitch - one sequencer step
//------------------------------------------------------------------------
namespace {

/** The cell's furniture. The lamp is smaller than SpySlider's corner one
    (10) because it is centred rather than tucked into a corner, and a
    centred lamp reads larger than it is. */
constexpr CCoord kStepLampSize   = 8.;
constexpr CCoord kStepLampTop    = 4.;
constexpr CCoord kStepLabelTop   = 15.;
constexpr CCoord kStepBarInset   = 4.;
constexpr CCoord kStepBarBottom  = 4.;
constexpr CCoord kStepBarHeight  = 9.;
constexpr CCoord kStepLabelBottom = 13.;

} // anonymous namespace

//------------------------------------------------------------------------
SpyStepSwitch::SpyStepSwitch (const CRect& size, IControlListener* listener, int32_t tag)
: SpySlider (size, listener, tag)
{
	// The lamp is the playhead, and it is always wanted on a step - the
	// caller does not have to remember to turn it on.
	setUseIndicator (true);
}

//------------------------------------------------------------------------
void SpyStepSwitch::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	const CCoord mid = (r.left + r.right) * 0.5;
	const bool on = getValueNormalized () >= 0.5f;

	// THE BOX, round the switch and its lamp together. Drawn first so
	// everything else sits inside it.
	context->setFrameColor (Colours::kOuterBorder);
	context->setLineWidth (1.);
	context->drawRect (CRect (r.left, r.top, r.right - 1., r.bottom - 1.));

	// THE LAMP, CENTRED - the whole point of this class. SpySlider's
	// drawLamp puts it at r.left, which on a control this narrow leaves
	// it sitting ten pixels left of its own number.
	{
		CRect lamp (mid - kStepLampSize * 0.5, r.top + kStepLampTop,
		            mid + kStepLampSize * 0.5, r.top + kStepLampTop + kStepLampSize);
		draw3dRect (context, lamp, Colours::kLampFrame, Colours::kLampFrame);
		lamp.inset (1., 1.);
		context->setFillColor (indicator () ? Colours::kLampOn : Colours::kLampOff);
		context->drawRect (lamp, kDrawFilled);
	}

	// A CELL WITH A READING SHOWS THE READING; A CELL WITHOUT ONE SHOWS
	// ITS BAR.
	//
	// One rule, and it is what lets the same class be a 30-pixel step
	// and the 100-pixel Run switch. A step has no reading - its number
	// and its bar are the whole of what it says - while Run has three
	// states worth naming, "off", "armed" and "running", and a bar under
	// them would be repeating the middle one badly.
	std::string reading = mValueText;
	if (reading.empty () && mFormatter)
		reading = mFormatter (getValueNormalized ());

	if (!reading.empty ())
	{
		drawFitted (context, reading,
		            CRect (r.left + 1., r.top + kStepLabelTop, r.right - 1., r.bottom),
		            on ? Colours::kValue : Colours::kLabel);

		drawFitted (context, mLabel,
		            CRect (r.left + 1., r.bottom - kStepLabelBottom, r.right - 1., r.bottom),
		            Colours::kLabel);

		setDirty (false);
		return;
	}

	// The step number, under the lamp.
	drawFitted (context, mLabel,
	            CRect (r.left + 1., r.top + kStepLabelTop, r.right - 1., r.bottom),
	            on ? Colours::kValue : Colours::kLabel);

	// THE BAR is the step's own state: filled when the step will strike,
	// an empty groove when it will not. Distinct from the lamp above it,
	// which is where the sequencer has got to.
	CRect bar (r.left + kStepBarInset,
	           r.bottom - kStepBarBottom - kStepBarHeight,
	           r.right - kStepBarInset,
	           r.bottom - kStepBarBottom);

	draw3dRect (context, bar, Colours::kBarLight, Colours::kBarHigh);

	if (on)
	{
		bar.inset (1., 1.);
		if (bar.getWidth () > 0. && bar.getHeight () > 0.)
		{
			context->setFillColor (Colours::kBarFill);
			context->drawRect (bar, kDrawFilled);
		}
	}

	setDirty (false);
}

//------------------------------------------------------------------------
void SpyStepSwitch::onMouseDownEvent (MouseDownEvent& event)
{
	if (! event.buttonState.isLeft ())
		return;

	// A CLICK TOGGLES, and that is all. No drag: a step is on or off, and
	// a relative drag across a row of thirty-pixel cells would be a way
	// to change the wrong one on the way past.
	beginEdit ();
	setValueNormalized (getValueNormalized () >= 0.5f ? 0.f : 1.f);
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpyStepSwitch::onMouseMoveEvent (MouseMoveEvent& event)
{
	// Deliberately nothing - see onMouseDownEvent. SpySlider's would drag
	// the value.
	(void)event;
}

//------------------------------------------------------------------------
void SpyStepSwitch::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

//------------------------------------------------------------------------
void SpyStepSwitch::onMouseWheelEvent (MouseWheelEvent& event)
{
	// Nothing. A wheel over a row of sixteen switches is somebody
	// scrolling the host's window, not setting a pattern.
	(void)event;
}

//------------------------------------------------------------------------
// SpyEnvelopeView
//------------------------------------------------------------------------
namespace {

/** The band at the top the caption and figure get to themselves. Without
    it a full-height curve - which is what Amount 100% is - runs straight
    through the lettering. ForTran's FtCurveView reserves the same band
    for the same reason. */
constexpr CCoord kLabelBand = 12.;

/** The inset from the plate to the plotting area, on every side. */
constexpr CCoord kPlotInset = 2.;

} // anonymous namespace

//------------------------------------------------------------------------
SpyEnvelopeView::SpyEnvelopeView (const CRect& size)
: CView (size)
{
	setMouseEnabled (false);
}

//------------------------------------------------------------------------
void SpyEnvelopeView::setCurves (const float* vcf, const float* vca, int count)
{
	if (count < 0)
		count = 0;

	// EITHER MAY BE NULL and the other still draw. A display with one
	// curve missing is a display with one curve missing; refusing to
	// draw at all would hide the one that is there.
	mVcf.assign (vcf ? vcf : nullptr, vcf ? vcf + count : nullptr);
	mVca.assign (vca ? vca : nullptr, vca ? vca + count : nullptr);
	invalid ();
}

//------------------------------------------------------------------------
void SpyEnvelopeView::setCaption (const std::string& caption)
{
	if (mCaption == caption)
		return;
	mCaption = caption;
	invalid ();
}

//------------------------------------------------------------------------
void SpyEnvelopeView::setAnnotation (const std::string& annotation)
{
	if (mAnnotation == annotation)
		return;
	mAnnotation = annotation;
	invalid ();
}

//------------------------------------------------------------------------
void SpyEnvelopeView::setLegend (const std::string& vcf, const std::string& vca)
{
	if (mVcfLegend == vcf && mVcaLegend == vca)
		return;
	mVcfLegend = vcf;
	mVcaLegend = vca;
	invalid ();
}

//------------------------------------------------------------------------
void SpyEnvelopeView::drawTrace (CDrawContext* context, const std::vector<float>& data,
                                 const CColor& colour, CCoord left, CCoord width,
                                 CCoord top, CCoord height) const
{
	if (data.size () < 2)
		return;

	auto pointAt = [&] (size_t i)
	{
		const double x = left + width * (double) i / (double) (data.size () - 1);

		// CLAMPED, not scaled to fit. The scale is fixed at 0..1 on
		// purpose - see the banner - so a value outside it is drawn at
		// the edge rather than being allowed to rescale the picture and
		// flatten the other curve along with it.
		double v = std::isfinite (data[i]) ? (double) data[i] : 0.0;
		if (v > 1.0) v = 1.0;
		if (v < 0.0) v = 0.0;

		return CPoint (x, top + height - v * height);
	};

	context->setFrameColor (colour);
	context->setLineWidth (1.);
	CPoint prev = pointAt (0);
	for (size_t i = 1; i < data.size (); ++i)
	{
		const CPoint p = pointAt (i);
		context->drawLine (prev, p);
		prev = p;
	}
}

//------------------------------------------------------------------------
void SpyEnvelopeView::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	context->setDrawMode (kAntiAliasing);

	// The plate, and a border in the panel's own frame colour so this
	// sits in the same visual family as the boxed step switches rather
	// than looking like a window cut into the panel.
	context->setFillColor (Colours::kPlate);
	context->setFrameColor (Colours::kOuterBorder);
	context->setLineWidth (1.);
	CRect plate (r);
	plate.inset (0.5, 0.5);
	context->drawRect (plate, kDrawFilledAndStroked);

	// Lettering BEFORE the traces, so a curve that reaches the top of
	// its band crosses over the text rather than being hidden by it.
	if (!mCaption.empty () || !mAnnotation.empty ())
	{
		context->setFont (panelFontTiny ());
		CRect text (r);
		text.inset (4., 3.);
		text.bottom = text.top + 11.;
		if (!mCaption.empty ())
		{
			context->setFontColor (Colours::kValue);
			context->drawString (mCaption.c_str (), text, kLeftText, true);
		}
		if (!mAnnotation.empty ())
		{
			context->setFontColor (Colours::kBarLight);
			context->drawString (mAnnotation.c_str (), text, kRightText, true);
		}
	}

	// The legend gets a band at the BOTTOM on the same terms, so a
	// curve sitting on the floor does not run through the lettering.
	const bool hasLegend = !mVcfLegend.empty () || !mVcaLegend.empty ();
	if (hasLegend)
	{
		context->setFont (panelFontTiny ());
		CRect text (r);
		text.inset (4., 3.);
		text.top = text.bottom - 11.;

		// Each word in its OWN trace colour, which is what makes this a
		// legend rather than a caption: the colour is the identifying
		// part and the word only says which is which.
		context->setFontColor (Colours::kTraceVcf);
		context->drawString (mVcfLegend.c_str (), text, kLeftText, true);
		context->setFontColor (Colours::kTraceVca);
		context->drawString (mVcaLegend.c_str (), text, kRightText, true);
	}

	const CCoord band   = (mCaption.empty () && mAnnotation.empty ()) ? 0. : kLabelBand;
	const CCoord foot   = hasLegend ? kLabelBand : 0.;
	const CCoord left   = r.left + kPlotInset;
	const CCoord width  = r.getWidth () - kPlotInset * 2.;
	const CCoord top    = r.top + kPlotInset + band;
	const CCoord height = r.getHeight () - kPlotInset * 2. - band - foot;

	if (width <= 0. || height <= 0.)
	{
		setDirty (false);
		return;
	}

	// The floor. An envelope that has decayed to nothing sits ON this
	// line, and without it a short envelope on a long axis is a curve
	// that vanishes into an empty box.
	context->setFrameColor (Colours::kOuterBorder);
	context->drawLine (CPoint (left, top + height), CPoint (left + width, top + height));

	// VCA LAST, so where the two run together - which is the common case
	// at the default patch - the red is the one you see. The amp
	// envelope is the one that decides whether you hear anything at all.
	drawTrace (context, mVcf, Colours::kTraceVcf, left, width, top, height);
	drawTrace (context, mVca, Colours::kTraceVca, left, width, top, height);

	setDirty (false);
}

//------------------------------------------------------------------------
} // namespace FilterDrum
