//------------------------------------------------------------------------
// FilterDrum - the Design 2.0 panel's views
//
// Knob, SegmentSwitch, Scope and Lamp are R2Synth's, with the additions
// the header lists. StepPad and ToggleButton are new.
//------------------------------------------------------------------------

#include "FilterDrumPanelViews.h"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace VSTGUI;

namespace FilterDrum {
namespace Panel {

//------------------------------------------------------------------------
namespace {

constexpr double kPi = 3.14159265358979323846;

/** VSTGUI's angles: degrees, 0 at three o'clock, clockwise (y is down). */
CPoint pointAt (const CPoint& centre, double radius, double degrees)
{
	const double t = degrees * kPi / 180.0;
	return CPoint (centre.x + radius * std::cos (t), centre.y + radius * std::sin (t));
}

CRect circleRect (const CPoint& centre, double radius)
{
	return CRect (centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius);
}

CRect toCRect (const Layout::Rect& r)
{
	return CRect (r.left, r.top, r.right, r.bottom);
}

void strokeArc (CDrawContext* context, const CPoint& centre, double radius, double from, double to,
                const CColor& colour, CCoord width)
{
	if (to - from < 0.5)
		return;
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->addArc (circleRect (centre, radius), from, to, true);
	context->setFrameColor (colour);
	context->setLineWidth (width);
	context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

void fillRoundRect (CDrawContext* context, const CRect& r, CCoord radius, const CColor& fill,
                    const CColor* stroke = nullptr, CCoord strokeWidth = 1.)
{
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->addRoundRect (r, radius);
	context->setFillColor (fill);
	context->drawGraphicsPath (path, CDrawContext::kPathFilled);
	if (stroke)
	{
		context->setFrameColor (*stroke);
		context->setLineWidth (strokeWidth);
		context->setLineStyle (kLineSolid);
		context->drawGraphicsPath (path, CDrawContext::kPathStroked);
	}
}

CFontRef fontSegment ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Helvetica Neue", 12, kBoldFace);
	return font;
}

/** Toggle a two-state control as one edit. */
void toggle (CControl* control)
{
	control->beginEdit ();
	control->setValueNormalized (control->getValueNormalized () >= 0.5f ? 0.f : 1.f);
	control->valueChanged ();
	control->endEdit ();
	control->invalid ();
}

} // anonymous namespace

//------------------------------------------------------------------------
// Palette and faces
//------------------------------------------------------------------------
namespace Palette {

CColor accent (Layout::Accent a)
{
	switch (a)
	{
		case Layout::Accent::Noise:  return CColor (242, 165,  65, 255);
		case Layout::Accent::Filter: return CColor ( 86, 168, 245, 255);
		case Layout::Accent::Amp:    return CColor ( 95, 208, 160, 255);
		case Layout::Accent::Seq:    return CColor (176, 140, 255, 255);
	}
	return kText;
}

CColor accentAlpha (Layout::Accent a, uint8_t alpha)
{
	CColor c = accent (a);
	c.alpha = alpha;
	return c;
}

CColor accentInk (Layout::Accent a)
{
	switch (a)
	{
		case Layout::Accent::Noise:  return CColor ( 26,  18,   5, 255);
		case Layout::Accent::Filter: return CColor (  6,  20,  36, 255);
		case Layout::Accent::Amp:    return CColor (  6,  30,  20, 255);
		case Layout::Accent::Seq:    return CColor ( 22,  15,  38, 255);
	}
	return kGround;
}

} // namespace Palette

CFontRef fontWordmark ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Avenir Next Condensed", 23, kBoldFace);
	return font;
}
CFontRef fontTitle ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Helvetica Neue", 13, kBoldFace);
	return font;
}
CFontRef fontSubtitle ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Helvetica Neue", 12);
	return font;
}
CFontRef fontLabel ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Helvetica Neue", 10, kBoldFace);
	return font;
}
CFontRef fontValue ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Menlo", 10);
	return font;
}
CFontRef fontNote ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Helvetica Neue", 11);
	return font;
}
CFontRef fontBadge ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Menlo", 12, kBoldFace);
	return font;
}
CFontRef fontButton ()
{
	static SharedPointer<CFontDesc> font = makeOwned<CFontDesc> ("Helvetica Neue", 13, kBoldFace);
	return font;
}

std::string upper (const std::string& s)
{
	std::string out (s);
	for (auto& c : out)
	{
		const auto u = static_cast<unsigned char> (c);
		if (u < 0x80)
			c = static_cast<char> (std::toupper (u));
	}
	return out;
}

//------------------------------------------------------------------------
// Backdrop
//------------------------------------------------------------------------
Backdrop::Backdrop (const CRect& size)
: CView (size)
{
	setMouseEnabled (false);
}

void Backdrop::draw (CDrawContext* context)
{
	using namespace Layout;
	const CRect all = getViewSize ();

	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	// ---- the ground and the header band --------------------------------
	context->setFillColor (Palette::kGround);
	context->drawRect (all, kDrawFilled);
	context->setFillColor (Palette::kHeader);
	context->drawRect (CRect (all.left, all.top, all.right, all.top + kHeaderHeight), kDrawFilled);
	context->setFillColor (Palette::kBorder);
	context->drawRect (CRect (all.left, kHeaderHeight - 1, all.right, kHeaderHeight), kDrawFilled);

	// ---- the wordmark: FILTER in the text colour, DRUM in amber --------
	{
		context->setFont (fontWordmark ());
		const CCoord prefixWidth = context->getStringWidth ("FILTER");
		CRect r = toCRect (kWordmark);
		context->setFontColor (Palette::kText);
		context->drawString ("FILTER", r, kLeftText, true);
		r.left += prefixWidth + 1;
		context->setFontColor (Palette::accent (Accent::Noise));
		context->drawString ("DRUM", r, kLeftText, true);

		context->setFillColor (Palette::kBorder);
		context->drawRect (CRect (kWordmark.right + 4, 16, kWordmark.right + 5, 40), kDrawFilled);
	}

	// ---- the sections ---------------------------------------------------
	for (const auto& s : kSections)
	{
		CRect r = toCRect (s.r);
		r.inset (0.5, 0.5);
		fillRoundRect (context, r, kSectionRadius, Palette::kSection, &Palette::kBorder);

		const CCoord rowTop = s.r.top + kSectionPadTop;
		const CCoord rowMid = rowTop + kSectionTitleH / 2.;

		context->setFont (fontTitle ());
		const CCoord titleWidth = context->getStringWidth (s.title);

		// The mark before the title: a numbered disc for a drum, the
		// section's dot otherwise.
		const bool badge = s.badge && *s.badge;
		const CCoord markWidth = badge ? 30. : 16.;
		CCoord left = s.r.left + kSectionPadX;
		if (s.centred)
			left = (s.r.left + s.r.right) / 2. - (markWidth + titleWidth) / 2.;

		if (badge)
		{
			const CPoint centre (left + 11, rowMid);
			context->setFillColor (Palette::kCap);
			context->drawEllipse (circleRect (centre, 11), kDrawFilled);
			context->setFont (fontBadge ());
			context->setFontColor (Palette::accent (s.accent));
			context->drawString (s.badge, circleRect (centre, 11), kCenterText, true);
		}
		else
		{
			context->setFillColor (Palette::accent (s.accent));
			context->drawEllipse (circleRect (CPoint (left + 4, rowMid), 4), kDrawFilled);
		}

		context->setFont (fontTitle ());
		context->setFontColor (Palette::kTitle);
		const CRect titleRect (left + markWidth, rowTop, s.r.right - kSectionPadX, rowTop + kSectionTitleH);
		context->drawString (s.title, titleRect, kLeftText, true);

		if (s.subtitle && *s.subtitle)
		{
			context->setFont (fontSubtitle ());
			context->setFontColor (Palette::kSubtle);
			CRect sub (titleRect);
			sub.left += titleWidth + 14;
			context->drawString (s.subtitle, sub, kLeftText, true);
		}
	}

	// ---- the chevrons: the signal goes this way -------------------------
	for (const auto& c : kChevrons)
	{
		auto path = owned (context->createGraphicsPath ());
		if (path)
		{
			path->beginSubpath (CPoint (c.x - 3, c.y - 8));
			path->addLine (CPoint (c.x + 3, c.y));
			path->addLine (CPoint (c.x - 3, c.y + 8));
			context->setFrameColor (Palette::kDim);
			context->setLineWidth (2.);
			context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
			context->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
	}

	// ---- the rules -------------------------------------------------------
	context->setFillColor (Palette::kRule);
	for (const auto& r : kRules)
		context->drawRect (toCRect (r), kDrawFilled);

	// ---- the fixed captions ---------------------------------------------
	for (const auto& c : kCaptions)
	{
		const CRect r = toCRect (c.r);
		const CHoriTxtAlign align = c.centred ? kCenterText : kLeftText;
		switch (c.style)
		{
			case CaptionStyle::Block:
				context->setFont (fontLabel ());
				context->setFontColor (Palette::accent (c.accent));
				context->drawString (upper (c.text).c_str (), r, align, true);
				break;
			case CaptionStyle::Field:
				context->setFont (fontLabel ());
				context->setFontColor (Palette::kMuted);
				context->drawString (upper (c.text).c_str (), r, align, true);
				break;
			case CaptionStyle::Note:
				context->setFont (fontNote ());
				context->setFontColor (Palette::kSubtle);
				context->drawString (c.text, r, align, true);
				break;
		}
	}

	setDirty (false);
}

//------------------------------------------------------------------------
// Knob
//------------------------------------------------------------------------
namespace {
constexpr float kDragPixels = 200.f;
} // anonymous namespace

Knob::Knob (const CRect& size, IControlListener* listener, int32_t tag,
            Layout::Accent accent, int diameter, bool bipolar, bool reversed)
: CControl (size, listener, tag)
, mAccent (accent)
, mDiameter (diameter)
, mBipolar (bipolar)
, mReversed (reversed)
{
}

void Knob::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	double v = std::clamp (static_cast<double> (getValueNormalized ()), 0.0, 1.0);
	if (mReversed)
		v = 1.0 - v;

	const CCoord stroke = (mDiameter >= 60) ? 5. : 4.;
	const CPoint centre (r.left + r.getWidth () / 2., r.top + mDiameter / 2.);
	const double radius = mDiameter / 2. - stroke / 2. - 1.;

	strokeArc (context, centre, radius, 135.0, 405.0, Palette::kTrack, stroke);
	const double a = angleOf (v);
	if (mBipolar)
		strokeArc (context, centre, radius, std::min (a, 270.0), std::max (a, 270.0),
		           Palette::accent (mAccent), stroke);
	else
		strokeArc (context, centre, radius, 135.0, a, Palette::accent (mAccent), stroke);

	// A bipolar knob AT its centre has no arc at all; a dot at the top
	// says where the centre is, so the knob does not read as dead.
	if (mBipolar && std::fabs (a - 270.0) < 0.5)
	{
		context->setFillColor (Palette::accent (mAccent));
		context->drawEllipse (circleRect (pointAt (centre, radius, 270.0), stroke / 2.), kDrawFilled);
	}

	const double capRadius = radius - stroke / 2. - 4.;
	context->setFillColor (Palette::kCap);
	context->setFrameColor (Palette::kCapEdge);
	context->setLineWidth (1.);
	context->setLineStyle (kLineSolid);
	context->drawEllipse (circleRect (centre, capRadius), kDrawFilledAndStroked);

	context->setFrameColor (Palette::kText);
	context->setLineWidth (2.);
	context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	context->drawLine (pointAt (centre, capRadius * 0.2, a), pointAt (centre, capRadius - 2., a));

	const CCoord textTop = r.top + mDiameter + Layout::kKnobGap;
	if (mFormatter)
	{
		context->setFont (fontValue ());
		context->setFontColor (mDragging ? Palette::accent (mAccent) : Palette::kText);
		context->drawString (mFormatter (getValueNormalized ()).c_str (),
		                     CRect (r.left - 4, textTop, r.right + 4, textTop + Layout::kKnobValueH),
		                     kCenterText, true);
	}
	if (!mLabel.empty ())
	{
		const CCoord labelTop = textTop + Layout::kKnobValueH;
		context->setFont (fontLabel ());
		context->setFontColor (Palette::kMuted);
		context->drawString (mLabel.c_str (),
		                     CRect (r.left - 4, labelTop, r.right + 4, labelTop + Layout::kKnobLabelH),
		                     kCenterText, true);
	}

	setDirty (false);
}

void Knob::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;

	if (event.clickCount >= 2)
	{
		beginEdit ();
		setValue (getDefaultValue ());
		valueChanged ();
		endEdit ();
		invalid ();
		event.consumed = true;
		return;
	}

	mDragging = true;
	mLastY = event.mousePosition.y;
	beginEdit ();
	invalid ();
	event.consumed = true;
}

void Knob::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (!mDragging)
		return;

	const CCoord dy = mLastY - event.mousePosition.y;   // up is clockwise
	if (std::fabs (dy) < 0.5)
		return;
	mLastY = event.mousePosition.y;

	const float scale = (event.modifiers.has (ModifierKey::Shift) ? 0.1f : 1.f) * (mReversed ? -1.f : 1.f);
	setValueNormalized (std::clamp (
		getValueNormalized () + static_cast<float> (dy) / kDragPixels * scale, 0.f, 1.f));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

void Knob::onMouseUpEvent (MouseUpEvent& event)
{
	if (!mDragging)
		return;
	mDragging = false;
	endEdit ();
	invalid ();
	event.consumed = true;
}

void Knob::onMouseCancelEvent (MouseCancelEvent& event)
{
	if (mDragging)
	{
		mDragging = false;
		endEdit ();
		invalid ();
	}
	event.consumed = true;
}

void Knob::onMouseWheelEvent (MouseWheelEvent& event)
{
	const float step = (event.modifiers.has (ModifierKey::Shift) ? 0.001f : 0.01f) * (mReversed ? -1.f : 1.f);
	beginEdit ();
	setValueNormalized (std::clamp (
		getValueNormalized () + static_cast<float> (event.deltaY) * step, 0.f, 1.f));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
// SegmentSwitch
//------------------------------------------------------------------------
SegmentSwitch::SegmentSwitch (const CRect& size, IControlListener* listener, int32_t tag,
                              Layout::Accent accent)
: CControl (size, listener, tag)
, mAccent (accent)
{
}

int SegmentSwitch::currentIndex () const
{
	if (count () < 2)
		return 0;
	const int i = static_cast<int> (std::floor (getValueNormalized () * (count () - 1) + 0.5f));
	return std::clamp (i, 0, count () - 1);
}

void SegmentSwitch::select (int index)
{
	if (count () < 2)
		return;
	index = std::clamp (index, 0, count () - 1);
	beginEdit ();
	setValueNormalized (static_cast<float> (index) / static_cast<float> (count () - 1));
	valueChanged ();
	endEdit ();
	invalid ();
}

void SegmentSwitch::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	CRect outer (r);
	outer.inset (0.5, 0.5);
	fillRoundRect (context, outer, 8., Palette::kSegBack, &Palette::kBorder);

	const int n = count ();
	if (n == 0)
	{
		setDirty (false);
		return;
	}

	const CCoord inner = 3.;
	const CCoord segWidth = (r.getWidth () - 2. * inner) / n;
	const int current = currentIndex ();

	context->setFont (fontSegment ());
	for (int i = 0; i < n; ++i)
	{
		const CRect seg (r.left + inner + i * segWidth, r.top + inner,
		                 r.left + inner + (i + 1) * segWidth, r.bottom - inner);
		if (i == current)
		{
			fillRoundRect (context, seg, 6., Palette::accent (mAccent));
			context->setFontColor (Palette::accentInk (mAccent));
		}
		else
			context->setFontColor (Palette::kMuted);
		context->drawString (mNames[static_cast<size_t> (i)].c_str (), seg, kCenterText, true);
	}

	setDirty (false);
}

void SegmentSwitch::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft () || count () == 0)
		return;
	const CRect r = getViewSize ();
	const double fraction = (event.mousePosition.x - r.left) / r.getWidth ();
	select (static_cast<int> (fraction * count ()));
	event.consumed = true;
}

void SegmentSwitch::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

void SegmentSwitch::onMouseWheelEvent (MouseWheelEvent& event)
{
	if (event.deltaY > 0.)
		select (currentIndex () + 1);
	else if (event.deltaY < 0.)
		select (currentIndex () - 1);
	event.consumed = true;
}

//------------------------------------------------------------------------
// Scope
//------------------------------------------------------------------------
namespace {
constexpr CCoord kScopeInset   = 6.;
constexpr CCoord kScopeTextH   = 13.;
constexpr CCoord kScopeGrid    = 24.;
constexpr CCoord kScopeRadius  = 8.;
} // anonymous namespace

Scope::Scope (const CRect& size)
: CView (size)
, mMain (Palette::kText)
, mCompanion (Palette::kMuted)
, mCaptionColour (Palette::kSubtle)
{
	setMouseEnabled (false);
}

void Scope::setColours (const CColor& main, const CColor& companion)
{
	mMain = main;
	mCompanion = companion;
}

void Scope::setCurves (const float* main, const float* companion, int count)
{
	mMainData.assign (main, main + ((main && count > 0) ? count : 0));
	if (companion && count > 0)
		mCompanionData.assign (companion, companion + count);
	else
		mCompanionData.clear ();
	invalid ();
}

CRect Scope::plotRect () const
{
	CRect p = getViewSize ();
	p.inset (kScopeInset, kScopeInset);
	if (!mCaption.empty () || !mAnnotation.empty ())
		p.top += kScopeTextH;
	return p;
}

void Scope::drawTrace (CDrawContext* context, const std::vector<float>& data, const CColor& colour,
                       CCoord width, bool fill, bool dashed) const
{
	if (data.size () < 2)
		return;

	const CRect p = plotRect ();
	auto pointAtIndex = [&] (size_t i) {
		double v = std::isfinite (data[i]) ? static_cast<double> (data[i]) : 0.0;
		v = std::clamp (v, 0.0, 1.0);
		const double x = p.left + p.getWidth () * static_cast<double> (i) / static_cast<double> (data.size () - 1);
		return CPoint (x, p.bottom - v * p.getHeight ());
	};

	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->beginSubpath (pointAtIndex (0));
	for (size_t i = 1; i < data.size (); ++i)
		path->addLine (pointAtIndex (i));

	if (fill)
	{
		auto area = owned (context->createGraphicsPath ());
		if (area)
		{
			area->beginSubpath (CPoint (p.left, p.bottom));
			for (size_t i = 0; i < data.size (); ++i)
				area->addLine (pointAtIndex (i));
			area->addLine (CPoint (p.right, p.bottom));
			area->closeSubpath ();
			CColor tint (colour);
			tint.alpha = 40;
			context->setFillColor (tint);
			context->drawGraphicsPath (area, CDrawContext::kPathFilled);
		}
	}

	context->setFrameColor (colour);
	context->setLineWidth (width);
	if (dashed)
	{
		const CCoord dashes[] = { 3., 2.5 };
		context->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinRound, 0., 2, dashes));
	}
	else
		context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

void Scope::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	CRect plate (r);
	plate.inset (0.5, 0.5);
	fillRoundRect (context, plate, kScopeRadius, Palette::kScreen);
	{
		ConcatClip clip (*context, plate);
		context->setFillColor (Palette::kGrid);
		for (CCoord x = r.left + kScopeGrid; x < r.right - 1; x += kScopeGrid)
			context->drawRect (CRect (x, r.top + 1, x + 1, r.bottom - 1), kDrawFilled);
		for (CCoord y = r.top + kScopeGrid; y < r.bottom - 1; y += kScopeGrid)
			context->drawRect (CRect (r.left + 1, y, r.right - 1, y + 1), kDrawFilled);

		context->setFont (fontValue ());
		CRect text (r);
		text.inset (kScopeInset + 2, 3.);
		text.bottom = text.top + kScopeTextH;
		if (!mCaption.empty ())
		{
			context->setFontColor (mCaptionColour);
			context->drawString (mCaption.c_str (), text, kLeftText, true);
		}
		if (!mAnnotation.empty ())
		{
			context->setFontColor (Palette::kSubtle);
			context->drawString (mAnnotation.c_str (), text, kRightText, true);
		}

		drawTrace (context, mCompanionData, mCompanion, 1.5, false, mCompanionDashed);
		drawTrace (context, mMainData, mMain, 2., mFill, false);

		const CRect p = plotRect ();
		if (mMarkerX >= 0.0 && mMarkerX <= 1.0)
		{
			const CPoint dot (p.left + mMarkerX * p.getWidth (),
			                  p.bottom - std::clamp (mMarkerY, 0.0, 1.0) * p.getHeight ());
			context->setFillColor (Palette::kScreen);
			context->setFrameColor (mMain);
			context->setLineWidth (2.);
			context->setLineStyle (kLineSolid);
			context->drawEllipse (circleRect (dot, 4.), kDrawFilledAndStroked);
		}
	}

	context->setLineStyle (kLineSolid);
	fillRoundRect (context, plate, kScopeRadius, CColor (0, 0, 0, 0), &Palette::kScreenEdge);

	setDirty (false);
}

//------------------------------------------------------------------------
// Lamp
//------------------------------------------------------------------------
Lamp::Lamp (const CRect& size, const std::string& name)
: CView (size)
, mName (upper (name))
, mColour (Palette::kLampOn)
{
	setMouseEnabled (false);
}

void Lamp::setLevel (float level)
{
	level = std::clamp (level, 0.f, 1.f);
	if (level != mLevel)
	{
		mLevel = level;
		invalid ();
	}
}

void Lamp::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	context->setFont (fontLabel ());
	const CCoord textWidth = context->getStringWidth (mName.c_str ());
	const CCoord textLeft = r.right - textWidth;
	const CPoint dot (textLeft - 10., (r.top + r.bottom) / 2.);

	// The unlit lamp: a dark dot of the lamp's own hue.
	CColor off (mColour);
	off.red   = static_cast<uint8_t> (mColour.red   / 4);
	off.green = static_cast<uint8_t> (mColour.green / 4);
	off.blue  = static_cast<uint8_t> (mColour.blue  / 4);
	CColor edge (mColour);
	edge.alpha = 90;

	if (mLevel > 0.f)
	{
		CColor glow (mColour);
		glow.alpha = static_cast<uint8_t> (70.f * mLevel);
		context->setFillColor (glow);
		context->drawEllipse (circleRect (dot, 7.), kDrawFilled);

		// The dot itself fades from full colour to the unlit colour.
		CColor lit;
		lit.red   = static_cast<uint8_t> (off.red   + (mColour.red   - off.red)   * mLevel);
		lit.green = static_cast<uint8_t> (off.green + (mColour.green - off.green) * mLevel);
		lit.blue  = static_cast<uint8_t> (off.blue  + (mColour.blue  - off.blue)  * mLevel);
		lit.alpha = 255;
		context->setFillColor (lit);
		context->drawEllipse (circleRect (dot, 4.), kDrawFilled);
	}
	else
	{
		context->setFillColor (off);
		context->setFrameColor (edge);
		context->setLineWidth (1.);
		context->setLineStyle (kLineSolid);
		context->drawEllipse (circleRect (dot, 4.), kDrawFilledAndStroked);
	}

	context->setFontColor (mLevel > 0.f ? mColour : Palette::kSubtle);
	context->drawString (mName.c_str (), CRect (textLeft, r.top, r.right, r.bottom), kLeftText, true);

	setDirty (false);
}

//------------------------------------------------------------------------
// StepPad
//------------------------------------------------------------------------
StepPad::StepPad (const CRect& size, IControlListener* listener, int32_t tag,
                  Layout::Accent accent, const std::string& number)
: CControl (size, listener, tag)
, mAccent (accent)
, mNumber (number)
{
}

void StepPad::setPlayhead (bool here)
{
	if (here != mPlayhead)
	{
		mPlayhead = here;
		invalid ();
	}
}

void StepPad::setDisabled (bool disabled)
{
	if (disabled != mDisabled)
	{
		mDisabled = disabled;
		invalid ();
	}
}

void StepPad::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	const bool on = getValueNormalized () >= 0.5f;
	CRect pad (r);
	pad.inset (1., 1.);

	if (mDisabled)
	{
		// OUT OF THE CYCLE: hollow, dashed, number dimmed. Its hit is kept
		// as a faint ring so enabling it again holds no surprise. No
		// playhead outline - the playhead never lands here.
		fillRoundRect (context, pad, 7., Palette::kSegBack);
		if (auto path = owned (context->createGraphicsPath ()))
		{
			path->addRoundRect (pad, 7.);
			const CCoord dashes[] = { 3., 3. };
			context->setFrameColor (on ? Palette::accentAlpha (mAccent, 120) : Palette::kDim);
			context->setLineWidth (1.);
			context->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter,
			                                   0., 2, dashes));
			context->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
		context->setFont (fontValue ());
		context->setFontColor (Palette::kDim);
		context->drawString (mNumber.c_str (), CRect (r.left, r.bottom - 18, r.right, r.bottom - 4),
		                     kCenterText, true);
		setDirty (false);
		return;
	}

	if (on)
	{
		fillRoundRect (context, pad, 7., Palette::accent (mAccent));
		// A sheen on the top third, so a lit step reads as a key.
		CRect sheen (pad);
		sheen.inset (3., 3.);
		sheen.bottom = sheen.top + pad.getHeight () * 0.36;
		fillRoundRect (context, sheen, 5., CColor (255, 255, 255, 34));
	}
	else
		fillRoundRect (context, pad, 7., Palette::kPad, &Palette::kPadEdge);

	if (mPlayhead)
	{
		CColor white (255, 255, 255, 255);
		fillRoundRect (context, pad, 7., CColor (0, 0, 0, 0), &white, 2.);
	}

	context->setFont (on ? fontBadge () : fontValue ());
	context->setFontColor (on ? Palette::accentInk (mAccent) : Palette::kMuted);
	context->drawString (mNumber.c_str (), CRect (r.left, r.bottom - 18, r.right, r.bottom - 4),
	                     kCenterText, true);

	setDirty (false);
}

void StepPad::onMouseDownEvent (MouseDownEvent& event)
{
	// Right button, or Ctrl - the macOS convention, and the fallback for
	// a host that keeps the right button for its own menu. VSTGUI calls
	// the Mac's Command key Control and its Control key Super, so both
	// are accepted: SpySelector's idiom plus the real Ctrl key.
	const bool secondary = event.buttonState.isRight ()
	                    || event.modifiers.has (ModifierKey::Control)
	                    || event.modifiers.has (ModifierKey::Super);
	if (secondary)
	{
		if (mDisableHandler)
			mDisableHandler ();
		event.consumed = true;
		return;
	}

	if (!event.buttonState.isLeft ())
		return;
	toggle (this);
	event.consumed = true;
}

void StepPad::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

//------------------------------------------------------------------------
// ToggleButton
//------------------------------------------------------------------------
ToggleButton::ToggleButton (const CRect& size, IControlListener* listener, int32_t tag,
                            Layout::Accent accent, const std::string& name)
: CControl (size, listener, tag)
, mAccent (accent)
, mName (upper (name))
{
}

void ToggleButton::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	const bool on = getValueNormalized () >= 0.5f;
	const CColor colour = Palette::accent (mAccent);

	CRect plate (r);
	plate.inset (1., 1.);
	if (on)
		fillRoundRect (context, plate, 8., Palette::accentAlpha (mAccent, 46), &colour, 1.5);
	else
		fillRoundRect (context, plate, 8., Palette::kPad, &Palette::kPadEdge);

	// The lamp, then the name; the status line under the name.
	const CPoint dot (r.left + 20., r.top + r.getHeight () / 2.);
	if (on)
	{
		context->setFillColor (Palette::accentAlpha (mAccent, 70));
		context->drawEllipse (circleRect (dot, 8.), kDrawFilled);
		context->setFillColor (colour);
		context->drawEllipse (circleRect (dot, 5.), kDrawFilled);
	}
	else
	{
		context->setFillColor (Palette::kSegBack);
		context->setFrameColor (Palette::kPadEdge);
		context->setLineWidth (1.);
		context->setLineStyle (kLineSolid);
		context->drawEllipse (circleRect (dot, 5.), kDrawFilledAndStroked);
	}

	const CCoord textLeft = r.left + 36.;
	context->setFont (fontButton ());
	context->setFontColor (on ? colour : Palette::kMuted);
	context->drawString (mName.c_str (), CRect (textLeft, r.top + 7, r.right - 6, r.top + 25), kLeftText, true);

	if (mFormatter)
	{
		context->setFont (fontValue ());
		context->setFontColor (on ? Palette::kText : Palette::kSubtle);
		context->drawString (mFormatter (getValueNormalized ()).c_str (),
		                     CRect (textLeft, r.top + 25, r.right - 6, r.top + 39), kLeftText, true);
	}

	setDirty (false);
}

void ToggleButton::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;
	toggle (this);
	event.consumed = true;
}

void ToggleButton::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

//------------------------------------------------------------------------
} // namespace Panel
} // namespace FilterDrum
