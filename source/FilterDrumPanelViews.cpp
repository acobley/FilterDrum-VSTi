//------------------------------------------------------------------------
// FilterDrum - the panel's views (the black panel, "Mirror Columns")
//
// Knob, RotarySwitch, Scope, the lettering and the wear are R2Synth 1.1's
// (source/R2SynthPanelViews.cpp), with the additions marked in the
// header; the Backdrop draws FilterDrum's tables; Lamp, StepPad,
// ToggleButton are FilterDrum's own, redrawn for the black panel.
//------------------------------------------------------------------------

#include "FilterDrumPanelViews.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

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

/** The layout's angles: degrees from twelve o'clock, clockwise. */
CPoint pointFromTop (const CPoint& centre, double radius, double degrees)
{
	return pointAt (centre, radius, degrees - 90.0);
}

CRect circleRect (const CPoint& centre, double radius)
{
	return CRect (centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius);
}

CRect toCRect (const Layout::Rect& r)
{
	return CRect (r.left, r.top, r.right, r.bottom);
}

CLineStyle roundLine ()
{
	return CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound);
}

void strokeLine (CDrawContext* context, const CPoint& from, const CPoint& to, const CColor& colour, CCoord width)
{
	context->setFrameColor (colour);
	context->setLineWidth (width);
	context->setLineStyle (roundLine ());
	context->drawLine (from, to);
}

void fillCircle (CDrawContext* context, const CPoint& centre, double radius, const CColor& fill)
{
	context->setFillColor (fill);
	context->drawEllipse (circleRect (centre, radius), kDrawFilled);
}

void strokeCircle (CDrawContext* context, const CPoint& centre, double radius, const CColor& colour, CCoord width)
{
	context->setFrameColor (colour);
	context->setLineWidth (width);
	context->setLineStyle (kLineSolid);
	context->drawEllipse (circleRect (centre, radius), kDrawStroked);
}

void roundRect (CDrawContext* context, const CRect& r, CCoord radius, const CColor* fill,
                const CColor* stroke = nullptr, CCoord strokeWidth = 1.)
{
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->addRoundRect (r, radius);
	if (fill)
	{
		context->setFillColor (*fill);
		context->drawGraphicsPath (path, CDrawContext::kPathFilled);
	}
	if (stroke)
	{
		context->setFrameColor (*stroke);
		context->setLineWidth (strokeWidth);
		context->setLineStyle (kLineSolid);
		context->drawGraphicsPath (path, CDrawContext::kPathStroked);
	}
}

/** A black knob's cap: lit from the top left, a near-black at the rim. */
void fillCap (CDrawContext* context, CGraphicsPath* path, const CPoint& centre, double radius)
{
	auto gradient = owned (CGradient::create (0.0, 1.0, CColor (58, 58, 58, 255), CColor (5, 5, 5, 255)));
	if (!gradient)
		return;
	gradient->addColorStop (0.6, CColor (22, 22, 22, 255));
	context->fillRadialGradient (path, *gradient, centre, radius * 1.4,
	                             CPoint (-radius * 0.25, -radius * 0.35));
}

//------------------------------------------------------------------------
// The wear (PanelWear.h): the marks as paths
//------------------------------------------------------------------------
CColor wearColour (const Wear::Rgb& c, uint8_t alpha)
{
	return CColor (c.r, c.g, c.b, alpha);
}

/** Does the box (l, t, r, b) touch the area being drawn? */
bool touches (const CRect& clip, double l, double t, double r, double b)
{
	return r >= clip.left && l <= clip.right && b >= clip.top && t <= clip.bottom;
}

bool lineTouches (const CRect& clip, const std::vector<Wear::Pt>& pts, double pad)
{
	if (pts.empty ())
		return false;
	double l = pts[0].x, t = pts[0].y, r = l, b = t;
	for (const auto& p : pts)
	{
		l = std::min (l, p.x); r = std::max (r, p.x);
		t = std::min (t, p.y); b = std::max (b, p.y);
	}
	return touches (clip, l - pad, t - pad, r + pad, b + pad);
}

void polyline (CDrawContext* context, const std::vector<Wear::Pt>& pts, size_t from, size_t to,
               const CColor& colour, CCoord width)
{
	if (to <= from || to >= pts.size () + 1)
		return;
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->beginSubpath (CPoint (pts[from].x, pts[from].y));
	for (size_t i = from + 1; i < to; ++i)
		path->addLine (CPoint (pts[i].x, pts[i].y));
	context->setFrameColor (colour);
	context->setLineWidth (width);
	context->setLineStyle (roundLine ());
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

/** A hairline: half strength in its first and last thirds, so it fades
    in and out. */
void wearLine (CDrawContext* context, const Wear::Line& line, const Wear::Rgb& rgb)
{
	const size_t n = line.pts.size ();
	if (n < 2)
		return;
	const size_t a = std::max<size_t> (1, n / 3), b = std::max (a + 1, n - n / 3);
	const uint8_t half = static_cast<uint8_t> (line.alpha / 2);
	polyline (context, line.pts, 0, a + 1, wearColour (rgb, half), line.width);
	polyline (context, line.pts, a, std::min (b + 1, n), wearColour (rgb, line.alpha), line.width);
	if (b < n - 1)
		polyline (context, line.pts, b, n, wearColour (rgb, half), line.width);
}

/** A soft round patch: `alpha` at the centre, none at the edge. */
void softDisc (CDrawContext* context, const CPoint& centre, double radius, const Wear::Rgb& rgb, uint8_t alpha)
{
	auto path = owned (context->createGraphicsPath ());
	auto gradient = owned (CGradient::create (0.0, 1.0, wearColour (rgb, alpha), wearColour (rgb, 0)));
	if (!path || !gradient)
		return;
	gradient->addColorStop (0.5, wearColour (rgb, static_cast<uint8_t> (alpha * 0.55)));
	path->addEllipse (circleRect (centre, radius));
	context->fillRadialGradient (path, *gradient, centre, radius);
}

/** Dust round a knob's foot: a ring that fades both ways from `peak`. */
void footRing (CDrawContext* context, const Wear::FootRing& f)
{
	auto path = owned (context->createGraphicsPath ());
	const CColor clear = wearColour (Wear::kDust, 0);
	auto gradient = owned (CGradient::create (0.0, 1.0, clear, clear));
	if (!path || !gradient)
		return;
	gradient->addColorStop (f.inner / f.outer, clear);
	gradient->addColorStop (f.peak / f.outer, wearColour (Wear::kDust, f.alpha));
	const CPoint centre (f.cx, f.cy);
	path->addEllipse (circleRect (centre, f.outer));
	context->fillRadialGradient (path, *gradient, centre, f.outer);
}

/** Dust drifted against a box edge: fades from the edge inwards. */
void drift (CDrawContext* context, const Wear::Drift& d)
{
	auto path = owned (context->createGraphicsPath ());
	auto gradient = owned (CGradient::create (0.0, 1.0, wearColour (Wear::kDust, d.alpha), wearColour (Wear::kDust, 0)));
	if (!path || !gradient)
		return;
	const CRect r (d.r.left, d.r.top, d.r.right, d.r.bottom);
	path->addRect (r);
	const CPoint mid ((r.left + r.right) / 2., (r.top + r.bottom) / 2.);
	CPoint from = mid, to = mid;
	if (d.fromX < 0) { from.x = r.left;  to.x = r.right; }
	if (d.fromX > 0) { from.x = r.right; to.x = r.left; }
	if (d.fromY < 0) { from.y = r.top;    to.y = r.bottom; }
	if (d.fromY > 0) { from.y = r.bottom; to.y = r.top; }
	context->fillLinearGradient (path, *gradient, from, to);
}

/** Flip a two-state control, as one edit. */
void toggle (CControl* control)
{
	control->beginEdit ();
	control->setValueNormalized (control->getValueNormalized () >= 0.5f ? 0.f : 1.f);
	control->valueChanged ();
	control->endEdit ();
	control->invalid ();
}

/** The glyphs, parsed once each. */
const std::vector<Lettering::Command>& commandsOf (const Lettering::Glyph& glyph)
{
	static std::map<const char*, std::vector<Lettering::Command>> cache;
	auto it = cache.find (glyph.path);
	if (it == cache.end ())
		it = cache.emplace (glyph.path, Lettering::parse (glyph.path)).first;
	return it->second;
}

} // anonymous namespace

//------------------------------------------------------------------------
// Palette, lettering
//------------------------------------------------------------------------
namespace Palette {

CColor accent (Layout::Accent a)
{
	switch (a)
	{
		case Layout::Accent::Noise:  return CColor (255, 178,  58, 255);
		case Layout::Accent::Filter: return CColor ( 47, 184, 255, 255);
		case Layout::Accent::Amp:    return CColor ( 56, 240, 160, 255);
		case Layout::Accent::Seq:    return CColor (194, 123, 255, 255);
	}
	return kInk;
}

CColor accentAlpha (Layout::Accent a, uint8_t alpha)
{
	CColor c = accent (a);
	c.alpha = alpha;
	return c;
}

} // namespace Palette

std::string upper (const std::string& s)
{
	std::string out (s);
	for (auto& c : out)
		if (static_cast<unsigned char> (c) < 0x80)
			c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
	return out;
}

void drawLettering (CDrawContext* context, const std::string& text, const Lettering::Style& style,
                    const CColor& colour, CCoord x, CCoord centreY, Align align, double strokeUnits)
{
	double endUnits = 0.0;
	const auto placed = Lettering::layout (text, style, &endUnits);
	if (placed.empty ())
		return;

	const double s = style.scale ();
	const double width = Lettering::width (text, style);
	double left = x;
	if (align == Align::Centre)
		left = x - width / 2.0;
	else if (align == Align::Right)
		left = x - width;
	const double top = centreY - style.height () / 2.0;
	const double half = style.weight / 2.0;

	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;

	for (const auto& p : placed)
	{
		const double ox = left + (p.x + half) * s;
		const double oy = top + half * s;
		auto at = [&] (double gx, double gy) { return CPoint (ox + gx * s, oy + gy * s); };

		CPoint current, start;
		for (const auto& c : commandsOf (Lettering::glyphOrDot (p.code)))
		{
			switch (c.op)
			{
				case 'M':
					current = start = at (c.x, c.y);
					path->beginSubpath (current);
					break;
				case 'L':
					current = at (c.x, c.y);
					path->addLine (current);
					break;
				case 'Q':
				{
					// The quadratic as a cubic: c1 = p0 + 2/3 (q - p0),
					// c2 = p2 + 2/3 (q - p2).
					const CPoint q = at (c.x1, c.y1);
					const CPoint end = at (c.x, c.y);
					const CPoint c1 (current.x + 2.0 / 3.0 * (q.x - current.x), current.y + 2.0 / 3.0 * (q.y - current.y));
					const CPoint c2 (end.x + 2.0 / 3.0 * (q.x - end.x), end.y + 2.0 / 3.0 * (q.y - end.y));
					path->addBezierCurve (c1, c2, end);
					current = end;
					break;
				}
				case 'Z':
					path->closeSubpath ();
					current = start;
					break;
				default:
					break;
			}
		}
	}

	context->setFrameColor (colour);
	context->setLineWidth ((strokeUnits > 0.0 ? strokeUnits : style.weight) * s);
	context->setLineStyle (roundLine ());
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

//------------------------------------------------------------------------
// Backdrop
//------------------------------------------------------------------------
Backdrop::Backdrop (const CRect& size)
: CView (size)
, mWear (Wear::panelWear ())
{
	setMouseEnabled (false);
}

void Backdrop::drawWear (CDrawContext* context)
{
	CRect clip;
	context->getClipRect (clip);

	// ---- the haze and the drifts: soft, underneath the sharper marks -------
	for (const auto& b : mWear.haze)
		if (touches (clip, b.x - b.r, b.y - b.r, b.x + b.r, b.y + b.r))
			softDisc (context, CPoint (b.x, b.y), b.r, Wear::kDust, b.alpha);
	for (const auto& d : mWear.drifts)
		if (touches (clip, d.r.left, d.r.top, d.r.right, d.r.bottom))
			drift (context, d);
	for (const auto& f : mWear.rings)
		if (touches (clip, f.cx - f.outer, f.cy - f.outer, f.cx + f.outer, f.cy + f.outer))
			footRing (context, f);

	// ---- hairlines through the paint, and the printing ------------------------
	for (const auto& l : mWear.scratches)
		if (lineTouches (clip, l.pts, l.width))
			wearLine (context, l, Wear::kScratch);

	// ---- specks and fibres ------------------------------------------------------
	for (const auto& sp : mWear.specks)
		if (touches (clip, sp.x - sp.r, sp.y - sp.r, sp.x + sp.r, sp.y + sp.r))
			fillCircle (context, CPoint (sp.x, sp.y), sp.r, wearColour (Wear::kSpeck, sp.alpha));
	for (const auto& l : mWear.fibres)
		if (lineTouches (clip, l.pts, l.width))
			wearLine (context, l, Wear::kSpeck);
}

void Backdrop::draw (CDrawContext* context)
{
	using namespace Layout;
	const CRect all = getViewSize ();

	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	// ---- the ground, the header band and the white rule under it --------
	context->setFillColor (Palette::kGround);
	context->drawRect (all, kDrawFilled);
	context->setFillColor (Palette::kHeader);
	context->drawRect (CRect (all.left, all.top, all.right, all.top + kHeaderHeight), kDrawFilled);
	context->setFillColor (Palette::kHeaderRule);
	context->drawRect (CRect (all.left, kHeaderHeight - 2, all.right, kHeaderHeight), kDrawFilled);

	// ---- the corner screws ------------------------------------------------
	for (const auto& s : kScrews)
	{
		const CPoint c (s.x, s.y);
		fillCircle (context, c, 3.2, Palette::kScrew);
		strokeCircle (context, c, 3.2, CColor (0, 0, 0, 255), 0.6);
		strokeLine (context, CPoint (c.x - 2.2, c.y - 2.2), CPoint (c.x + 2.2, c.y + 2.2), CColor (34, 34, 34, 255), 0.8);
	}

	// ---- the wordmark: FILTER DRUM, heavy, with the inline cut -------------
	{
		const double centreY = kHeaderHeight / 2.0 + 1.0;
		const double second = kWordmark.left + Lettering::width ("FILTER", kTextWordmark) + kWordmarkGap;
		for (int pass = 0; pass < 2; ++pass)
		{
			const CColor ink = pass ? Palette::kHeader : Palette::kInk;
			const double stroke = pass ? 0.32 : -1.0;
			drawLettering (context, "FILTER", kTextWordmark, ink, kWordmark.left, centreY, Align::Left, stroke);
			drawLettering (context, "DRUM", kTextWordmark, ink, second, centreY, Align::Left, stroke);
		}
		context->setFillColor (Palette::kDivider);
		context->drawRect (CRect (kHeaderDivider, 16, kHeaderDivider + 1, 40), kDrawFilled);
	}

	// ---- the sections: boxed in white, a badge, the title in its own box ---
	for (const auto& s : kSections)
	{
		CRect r = toCRect (s.r);
		r.inset (0.5, 0.5);
		roundRect (context, r, kSectionRadius, &Palette::kSection, &Palette::kSectionEdge, 1.2);

		if (s.badge && *s.badge)
		{
			double bx = 0.0, by = 0.0;
			badgeCentre (s, bx, by);
			strokeCircle (context, CPoint (bx, by), kBadgeRadius, Palette::kSectionEdge, 1.3);
			drawLettering (context, s.badge, kTextBadge, Palette::kInk, bx, by, Align::Centre);
		}

		const Box b = titleBox (s);
		roundRect (context, CRect (b.left, b.top, b.right, b.bottom), 2., &Palette::kSection,
		           &Palette::kSectionEdge, 1.2);
		const double centre = (b.top + b.bottom) / 2.0;
		drawLettering (context, s.title, kTextTitle, Palette::kInk, b.left + 8.0, centre);
		if (s.subtitle && *s.subtitle)
			drawLettering (context, s.subtitle, kTextSubtitle, Palette::kInkSoft, subtitleLeft (s), centre);
	}

	// ---- the blocks: a thin box, its caption cut into the top edge --------
	for (const auto& bl : kBlocks)
	{
		CRect r = toCRect (bl.r);
		r.inset (0.5, 0.5);
		roundRect (context, r, 2., nullptr, &Palette::kBlockEdge, 1.);
		const double left = bl.r.left + kBlockCaptionInset;
		const double w = Lettering::width (bl.caption, kTextBlock);
		context->setFillColor (Palette::kSection);
		context->drawRect (CRect (left - kBlockCaptionGap, bl.r.top - 2., left + w + kBlockCaptionGap, bl.r.top + 2.),
		                   kDrawFilled);
		drawLettering (context, bl.caption, kTextBlock, Palette::kInk, left, bl.r.top);
	}

	// ---- the chevrons: the signal goes this way ---------------------------
	for (const auto& c : kChevrons)
	{
		auto path = owned (context->createGraphicsPath ());
		if (path)
		{
			const double d = c.left ? -1.0 : 1.0;
			path->beginSubpath (CPoint (c.x - 5 * d, c.y - 10));
			path->addLine (CPoint (c.x + 4 * d, c.y));
			path->addLine (CPoint (c.x - 5 * d, c.y + 10));
			context->setFrameColor (Palette::kSectionEdge);
			context->setLineWidth (2.2);
			context->setLineStyle (roundLine ());
			context->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
	}

	// ---- the rules ----------------------------------------------------------
	context->setFillColor (Palette::kRule);
	for (const auto& r : kRules)
		context->drawRect (toCRect (r), kDrawFilled);

	// ---- the screens' black cut-outs: the OLEDs sit in them -----------------
	for (const auto& s : kScreens)
	{
		CRect r = toCRect (bezelOf (s));
		r.inset (0.5, 0.5);
		roundRect (context, r, 4., &Palette::kBezel, &Palette::kBezelEdge, 1.);
	}

	// ---- the fixed captions -------------------------------------------------
	for (const auto& c : kCaptions)
	{
		const CRect r = toCRect (c.r);
		const double centre = (r.top + r.bottom) / 2.0;
		const double x = (c.align == Align::Left) ? r.left : (c.align == Align::Right) ? r.right : r.getCenter ().x;
		switch (c.style)
		{
			case CaptionStyle::Block:
				drawLettering (context, c.text, kTextBlock, Palette::kInk, x, centre, c.align);
				break;
			case CaptionStyle::Field:
				drawLettering (context, c.text, kTextLabel, Palette::kInk, x, centre, c.align);
				break;
			case CaptionStyle::Note:
				drawLettering (context, c.text, kTextNote, Palette::kInkSoft, x, centre, c.align);
				break;
		}
	}

	// ---- the wear, over the printing ----------------------------------------
	drawWear (context);

	setDirty (false);
}

//------------------------------------------------------------------------
// Knob
//------------------------------------------------------------------------
namespace {
/** Pixels of vertical drag for the whole range. */
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
	const CPoint centre (r.left + r.getWidth () / 2., r.top + mDiameter / 2.);
	const double outer = mDiameter / 2.0;
	const double body = outer - ((mDiameter >= 44) ? 6.0 : 5.0);
	const double a = angleOf (v);

	// ---- the printed scale: eleven ticks, long at the ends and the middle
	for (int i = 0; i <= 10; ++i)
	{
		const double t = 135.0 + 27.0 * i;
		const bool longTick = (i == 0 || i == 5 || i == 10);
		strokeLine (context, pointAt (centre, body + 1.5, t), pointAt (centre, outer - (longTick ? 0.0 : 1.8), t),
		            Palette::kTick, longTick ? 1.3 : 0.9);
	}
	if (!mLow.empty () || mDiameter >= 52)
	{
		const std::string lo = !mLow.empty () ? mLow : (mBipolar ? "-" : "0");
		const std::string hi = !mHigh.empty () ? mHigh : (mBipolar ? "+" : "10");
		const CPoint l = pointFromTop (centre, outer + 2.0, -138.0);
		const CPoint h = pointFromTop (centre, outer + 2.0, 138.0);
		drawLettering (context, lo, Layout::kTextTick, Palette::kTick, l.x - 2., l.y + 4., Align::Right);
		drawLettering (context, hi, Layout::kTextTick, Palette::kTick, h.x + 2., h.y + 4., Align::Left);
	}

	// ---- the knurled body ---------------------------------------------------
	{
		auto rim = owned (context->createGraphicsPath ());
		if (rim)
		{
			const int teeth = (mDiameter >= 64) ? 36 : 28;
			for (int i = 0; i < teeth * 2; ++i)
			{
				const CPoint p = pointAt (centre, (i % 2 == 0) ? body : body - 1.4, 360.0 * i / (teeth * 2));
				if (i == 0)
					rim->beginSubpath (p);
				else
					rim->addLine (p);
			}
			rim->closeSubpath ();
			context->setFillColor (Palette::kKnobBody);
			context->drawGraphicsPath (rim, CDrawContext::kPathFilled);
			context->setFrameColor (Palette::kKnobRim);
			context->setLineWidth (0.6);
			context->setLineStyle (kLineSolid);
			context->drawGraphicsPath (rim, CDrawContext::kPathStroked);
		}
		auto cap = owned (context->createGraphicsPath ());
		if (cap)
		{
			cap->addEllipse (circleRect (centre, body * 0.78));
			fillCap (context, cap, centre, body * 0.78);
			context->setFrameColor (CColor (0, 0, 0, 255));
			context->setLineWidth (0.8);
			context->setLineStyle (kLineSolid);
			context->drawGraphicsPath (cap, CDrawContext::kPathStroked);
		}
	}

	// ---- the wear: dust in the knurling, the polished cap, turning marks ------
	// Everything but the sheen is placed relative to the pointer (`a`), so
	// it turns with the knob; the sheen is where the light is, so it stays.
	{
		const double capR = body * 0.78;
		if (mWear.knurlAlpha > 0)
			strokeCircle (context, centre, (capR + body) / 2.0, wearColour (Wear::kKnurlDust, mWear.knurlAlpha),
			              body - capR);
		if (mWear.sheen > 0)
			softDisc (context, CPoint (centre.x - capR * 0.25, centre.y - capR * 0.3), capR * 0.55, Wear::kSheen,
			          mWear.sheen);
		for (const auto& s : mWear.scuffs)
		{
			auto arc = owned (context->createGraphicsPath ());
			if (!arc)
				continue;
			const double start = a + s.start;
			arc->addArc (circleRect (centre, capR * s.radius), start, start + s.span, true);
			context->setFrameColor (wearColour (Wear::kScuff, s.alpha));
			context->setLineWidth (s.width);
			context->setLineStyle (roundLine ());
			context->drawGraphicsPath (arc, CDrawContext::kPathStroked);
		}
		for (const auto& g : mWear.grit)
			fillCircle (context, pointAt (centre, body * g.radius, a + g.angle), g.r,
			            wearColour (Wear::kKnurlDust, g.alpha));
	}

	// ---- the white line -------------------------------------------------------
	strokeLine (context, pointAt (centre, body * 0.15, a), pointAt (centre, body - 0.5, a), Palette::kInk,
	            (mDiameter >= 60) ? 2.4 : ((mDiameter >= 44) ? 2.2 : 1.8));

	// ---- the reading and the label --------------------------------------------
	const CCoord textTop = r.top + mDiameter + Layout::kKnobGap;
	const CCoord cx = centre.x;
	if (mFormatter)
		drawLettering (context, mFormatter (getValueNormalized ()), Layout::kTextValue, Palette::kInk, cx,
		               textTop + Layout::kKnobValueH / 2., Align::Centre);
	if (!mLabel.empty ())
		drawLettering (context, mLabel, Layout::kTextLabel, Palette::kInk, cx,
		               textTop + Layout::kKnobValueH + Layout::kKnobLabelH / 2., Align::Centre);

	setDirty (false);
}

void Knob::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;

	// DOUBLE-CLICK: the default. The first click of the pair began and
	// ended an ordinary (empty) drag, so this is one edit of its own.
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
// RotarySwitch
//------------------------------------------------------------------------
RotarySwitch::RotarySwitch (const CRect& size, IControlListener* listener, int32_t tag,
                            const Layout::Control& layout)
: CControl (size, listener, tag)
, mLayout (layout)
, mGeometry (Layout::selectorOf (layout, 2))
{
}

void RotarySwitch::setNames (const std::vector<std::string>& names)
{
	mNames = names;
	mGeometry = Layout::selectorOf (mLayout, count ());
	invalid ();
}

int RotarySwitch::currentIndex () const
{
	if (count () < 2)
		return 0;
	const int i = static_cast<int> (std::floor (getValueNormalized () * (count () - 1) + 0.5f));
	return std::clamp (i, 0, count () - 1);
}

void RotarySwitch::select (int index)
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

void RotarySwitch::draw (CDrawContext* context)
{
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	const auto& g = mGeometry;
	const CPoint centre (g.cx, g.cy);
	const int n = std::min (count (), g.positions);
	const int current = currentIndex ();

	// ---- the printed positions: a mark and a name for each ----------------
	for (int i = 0; i < n; ++i)
	{
		const bool set = (i == current);
		const double t = g.angle[i];
		strokeLine (context, pointFromTop (centre, g.radius + 2.5, t), pointFromTop (centre, g.radius + 8.0, t),
		            set ? Palette::kRed : Palette::kTick, set ? 2.0 : 1.6);
		const std::string name = upper (mNames[static_cast<size_t> (i)]);
		const Layout::Box b = Layout::selectorLabelBox (g, i, name);
		drawLettering (context, name, Layout::kTextLabel, set ? Palette::kInk : Palette::kInkDim, b.left,
		               (b.top + b.bottom) / 2.);
	}

	// ---- the chicken head: a round hub and a beak that points ------------
	const double a = (n > 0) ? g.angle[current] : 0.0;
	fillCircle (context, centre, g.radius * 0.8, CColor (11, 11, 11, 255));
	strokeCircle (context, centre, g.radius * 0.8, CColor (68, 68, 68, 255), 0.8);
	for (const auto& s : mGrit)   // grit round the hub, turning with the beak
		fillCircle (context, pointFromTop (centre, g.radius * 0.8 * s.radius, a + s.angle), s.r,
		            wearColour (Wear::kKnurlDust, s.alpha));

	auto beak = owned (context->createGraphicsPath ());
	if (beak)
	{
		const CPoint tip = pointFromTop (centre, g.radius + 3.0, a);
		const CPoint left = pointFromTop (centre, 8.0, a - 90.0);
		const CPoint right = pointFromTop (centre, 8.0, a + 90.0);
		const CPoint tail = pointFromTop (centre, g.radius, a + 180.0);
		beak->beginSubpath (tip);
		beak->addLine (right);
		// The tail, a quadratic through `tail`, as a cubic.
		const CPoint c1 (right.x + 2.0 / 3.0 * (tail.x - right.x), right.y + 2.0 / 3.0 * (tail.y - right.y));
		const CPoint c2 (left.x + 2.0 / 3.0 * (tail.x - left.x), left.y + 2.0 / 3.0 * (tail.y - left.y));
		beak->addBezierCurve (c1, c2, left);
		beak->closeSubpath ();
		fillCap (context, beak, centre, g.radius);
		context->setFrameColor (CColor (0, 0, 0, 255));
		context->setLineWidth (0.8);
		context->setLineStyle (roundLine ());
		context->drawGraphicsPath (beak, CDrawContext::kPathStroked);
		strokeLine (context, pointFromTop (centre, 2.0, a), tip, Palette::kInk, 1.8);
	}

	// ---- its name, under it ---------------------------------------------------
	if (!mLabel.empty ())
		drawLettering (context, mLabel, Layout::kTextLabel, Palette::kInk, g.cx, g.nameCentreY, Align::Centre);

	setDirty (false);
}

void RotarySwitch::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft () || count () < 2)
		return;
	const int picked = Layout::selectorPositionAt (mGeometry, event.mousePosition.x, event.mousePosition.y);
	select (picked >= 0 ? picked : (currentIndex () + 1) % count ());
	event.consumed = true;
}

void RotarySwitch::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

void RotarySwitch::onMouseWheelEvent (MouseWheelEvent& event)
{
	if (event.deltaY > 0.)
		select (currentIndex () + 1);
	else if (event.deltaY < 0.)
		select (currentIndex () - 1);
	event.consumed = true;
}

//------------------------------------------------------------------------
// Scope - the small colour OLED
//------------------------------------------------------------------------
namespace {
constexpr CCoord kScopeInset   = 6.;
constexpr CCoord kScopeTextH   = 13.;
constexpr CCoord kScopeGrid    = 24.;
constexpr CCoord kScopeDot     = 12.;
constexpr CCoord kScopeRadius  = 2.;
} // anonymous namespace

Scope::Scope (const CRect& size)
: CView (size)
, mMain (Palette::kInk)
, mCompanion (Palette::kScreenText)
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
                       CCoord width, bool fill, bool bloom, bool dashed) const
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

	// THE FILL fades from the line's colour at the top of the plot to
	// black at the floor, as an OLED's lit area does.
	if (fill)
	{
		auto area = owned (context->createGraphicsPath ());
		auto gradient = owned (CGradient::create (0.0, 1.0, CColor (colour.red, colour.green, colour.blue, 97),
		                                          CColor (colour.red, colour.green, colour.blue, 5)));
		if (area && gradient)
		{
			area->beginSubpath (CPoint (p.left, p.bottom));
			for (size_t i = 0; i < data.size (); ++i)
				area->addLine (pointAtIndex (i));
			area->addLine (CPoint (p.right, p.bottom));
			area->closeSubpath ();
			context->fillLinearGradient (area, *gradient, CPoint (p.left, p.top), CPoint (p.left, p.bottom));
		}
	}

	if (dashed)
	{
		const CCoord dashes[] = { 3., 2.5 };
		context->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinRound, 0., 2, dashes));
	}
	else
		context->setLineStyle (roundLine ());
	if (bloom)
	{
		// A slight bloom: the same line, wide and faint, under it.
		CColor glow (colour);
		glow.alpha = 60;
		context->setFrameColor (glow);
		context->setLineWidth (width + 2.5);
		context->drawGraphicsPath (path, CDrawContext::kPathStroked);
	}
	context->setFrameColor (colour);
	context->setLineWidth (width);
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

void Scope::draw (CDrawContext* context)
{
	using namespace Layout;
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	// ---- the glass: true black, and a dot grid ------------------------------
	roundRect (context, r, kScopeRadius, &Palette::kScreen);
	{
		ConcatClip clip (*context, r);
		context->setFillColor (Palette::kGridDot);
		for (CCoord x = r.left + kScopeGrid; x < r.right - 1; x += kScopeGrid)
			for (CCoord y = r.top + kScopeDot; y < r.bottom - 1; y += kScopeDot)
				context->drawEllipse (circleRect (CPoint (x, y), 0.6), kDrawFilled);
		for (CCoord y = r.top + kScopeGrid; y < r.bottom - 1; y += kScopeGrid)
			for (CCoord x = r.left + kScopeDot; x < r.right - 1; x += kScopeDot)
				context->drawEllipse (circleRect (CPoint (x, y), 0.6), kDrawFilled);

		// ---- lettering, before the traces so a curve crosses it ------------
		const double headY = r.top + 10.;
		if (!mCaption.empty ())
			drawLettering (context, mCaption, kTextScreen, Palette::kScreenText, r.left + 8., headY);
		if (!mAnnotation.empty ())
			drawLettering (context, mAnnotation, kTextScreen, mMain, r.right - 8., headY, Align::Right);
		const CRect p = plotRect ();

		// ---- the traces: the companion under, the main one over -------------
		drawTrace (context, mCompanionData, mCompanion, 1.3, false, false, mCompanionDashed);
		drawTrace (context, mMainData, mMain, 1.6, mFill, true, false);

		// ---- the ring ---------------------------------------------------------
		if (mMarkerX >= 0.0 && mMarkerX <= 1.0)
		{
			const CPoint dot (p.left + mMarkerX * p.getWidth (),
			                  p.bottom - std::clamp (mMarkerY, 0.0, 1.0) * p.getHeight ());
			fillCircle (context, dot, 4., Palette::kScreen);
			strokeCircle (context, dot, 4., Palette::kInk, 1.6);
		}

		// ---- the glass: a faint sheen at the top left, a hairline highlight -
		auto sheen = owned (context->createGraphicsPath ());
		auto gradient = owned (CGradient::create (0.0, 0.46, CColor (255, 255, 255, 18), CColor (255, 255, 255, 0)));
		if (sheen && gradient)
		{
			gradient->addColorStop (0.45, CColor (255, 255, 255, 4));
			sheen->addRect (r);
			context->fillLinearGradient (sheen, *gradient, CPoint (r.left, r.top),
			                             CPoint (r.left + 0.35 * r.getWidth (), r.bottom));
		}
		strokeLine (context, CPoint (r.left + 3., r.top + 0.8), CPoint (r.right - 3., r.top + 0.8),
		            CColor (255, 255, 255, 30), 0.6);

		// ---- the wear on the glass: fingerprints, dust, the odd fibre -------
		for (const auto& p : mWear.prints)
			for (const auto& ridge : p.ridges)
				polyline (context, ridge.pts, 0, ridge.pts.size (), wearColour (Wear::kPrint, ridge.alpha),
				          ridge.width);
		for (const auto& d : mWear.dust)
			fillCircle (context, CPoint (d.x, d.y), d.r, wearColour (Wear::kGlassDust, d.alpha));
		for (const auto& l : mWear.fibres)
			wearLine (context, l, Wear::kGlassDust);
	}

	setDirty (false);
}

//------------------------------------------------------------------------
// Lamp
//------------------------------------------------------------------------
namespace {

/** A red lamp in a black bezel, lit to `level` 0..1: dark red when off;
    lit, a glow and the lamp fading from full red to the unlit colour. */
void drawLampAt (CDrawContext* context, const CPoint& dot, float level, double bezel = 5.5, double lamp = 4.)
{
	fillCircle (context, dot, bezel, CColor (0, 0, 0, 255));
	strokeCircle (context, dot, bezel, Palette::kSectionEdge, 1.);
	if (level > 0.f)
	{
		CColor glow (Palette::kLampOn);
		glow.alpha = static_cast<uint8_t> (70.f * level);
		fillCircle (context, dot, bezel + 2.5, glow);
		const CColor& on = Palette::kLampOn;
		const CColor& off = Palette::kLampOff;
		const CColor lit (static_cast<uint8_t> (off.red + (on.red - off.red) * level),
		                  static_cast<uint8_t> (off.green + (on.green - off.green) * level),
		                  static_cast<uint8_t> (off.blue + (on.blue - off.blue) * level), 255);
		fillCircle (context, dot, lamp, lit);
	}
	else
		fillCircle (context, dot, lamp, Palette::kLampOff);
	fillCircle (context, CPoint (dot.x - 1.2, dot.y - 1.2), 1.3, CColor (255, 255, 255, level > 0.f ? 140 : 64));
}

} // anonymous namespace

Lamp::Lamp (const CRect& size, const std::string& name)
: CView (size)
, mName (upper (name))
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

	const double centreY = (r.top + r.bottom) / 2.;
	const double textLeft = r.right - Lettering::width (mName, Layout::kTextLabel);
	drawLampAt (context, CPoint (textLeft - 12., centreY), mLevel);
	drawLettering (context, mName, Layout::kTextLabel, Palette::kInk, r.right, centreY, Align::Right);

	setDirty (false);
}

//------------------------------------------------------------------------
// StepPad
//------------------------------------------------------------------------
namespace {

/** A black push-button: a black shadow, a face lit from above, a rim. */
void drawKey (CDrawContext* context, const CRect& key, CCoord radius)
{
	roundRect (context, key, radius + 0.5, &Palette::kBezel);
	CRect face (key.left + 1.5, key.top + 1., key.right - 1.5, key.bottom - 2.5);
	auto path = owned (context->createGraphicsPath ());
	auto gradient = owned (CGradient::create (0.0, 1.0, CColor (44, 44, 44, 255), CColor (18, 18, 18, 255)));
	if (path && gradient)
	{
		path->addRoundRect (face, radius);
		context->fillLinearGradient (path, *gradient, CPoint (face.left, face.top), CPoint (face.left, face.bottom));
		context->setFrameColor (Palette::kKeyEdge);
		context->setLineWidth (0.7);
		context->setLineStyle (kLineSolid);
		context->drawGraphicsPath (path, CDrawContext::kPathStroked);
	}
}

} // anonymous namespace

StepPad::StepPad (const CRect& size, IControlListener* listener, int32_t tag, const std::string& number)
: CControl (size, listener, tag)
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
	using namespace Layout;
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	const bool on = getValueNormalized () >= 0.5f;
	const CPoint led (r.getCenter ().x, r.top + kStepLedY);
	const CRect key (r.left, r.top + kStepKeyTop, r.right, r.top + kStepKeyBottom);
	const CRect lens (key.left + key.getWidth () * 0.2, key.top + 6., key.right - key.getWidth () * 0.2, key.top + 11.);
	const double numberY = r.top + kStepNumberY;

	if (mDisabled)
	{
		// OUT OF THE CYCLE: no key, a dashed hollow; a dash for its LED -
		// the playhead never lands here; its hit kept as a lens outline.
		auto path = owned (context->createGraphicsPath ());
		if (path)
		{
			CRect hollow (key);
			hollow.inset (0.5, 0.5);
			path->addRoundRect (hollow, 3.);
			const CCoord dashes[] = { 3., 3. };
			context->setFrameColor (CColor (106, 104, 98, 255));
			context->setLineWidth (1.1);
			context->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0., 2, dashes));
			context->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
		if (on)
			roundRect (context, lens, 1.5, nullptr, &Palette::kRed, 1.);
		strokeLine (context, CPoint (led.x - 3., led.y), CPoint (led.x + 3., led.y), Palette::kInkFaint, 1.2);
		drawLettering (context, mNumber, kTextStep, Palette::kInkFaint, led.x, numberY, Align::Centre);
		setDirty (false);
		return;
	}

	// ---- the chaser LED: white while the playhead is here ------------------
	fillCircle (context, led, 2.8, CColor (0, 0, 0, 255));
	strokeCircle (context, led, 2.8, CColor (58, 58, 58, 255), 0.8);
	if (mPlayhead)
	{
		fillCircle (context, led, 6., CColor (255, 255, 255, 60));
		fillCircle (context, led, 2.2, Palette::kChaser);
	}
	else
		fillCircle (context, led, 2., CColor (34, 32, 28, 255));

	// ---- the key, and its red lens: lit for a hit -------------------------
	drawKey (context, key, 3.);
	if (on)
	{
		CRect glow (lens);
		glow.inset (-3., -3.);
		const CColor halo (255, 59, 47, 80);
		roundRect (context, glow, 3., &halo);
		roundRect (context, lens, 1.5, &Palette::kLampOn);
		const CRect shine (lens.left + 1., lens.top + 0.8, lens.right - 1., lens.top + 2.);
		const CColor white (255, 255, 255, 90);
		roundRect (context, shine, 1., &white);
	}
	else
	{
		const CColor edge (0, 0, 0, 255);
		roundRect (context, lens, 1.5, &Palette::kLensOff, &edge, 0.5);
	}

	drawLettering (context, mNumber, kTextStep, (on || mPlayhead) ? Palette::kInk : Palette::kInkDim, led.x,
	               numberY, Align::Centre);

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
ToggleButton::ToggleButton (const CRect& size, IControlListener* listener, int32_t tag, const std::string& name)
: CControl (size, listener, tag)
, mName (upper (name))
{
}

void ToggleButton::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	const bool on = getValueNormalized () >= 0.5f;
	const CRect key (r.left, r.top, r.right, r.top + kButtonHeight);
	drawKey (context, key, 3.5);

	const double centreY = key.top + key.getHeight () / 2. - 1.;
	drawLampAt (context, CPoint (key.left + 16., centreY), on ? 1.f : 0.f, 4.8, 3.6);
	drawLettering (context, mName, Layout::kTextButton, Palette::kInk, key.left + 30., centreY);

	if (mFormatter)
		drawLettering (context, upper (mFormatter (getValueNormalized ())), Layout::kTextInfo,
		               on ? Palette::kInk : Palette::kInkDim, r.getCenter ().x, key.bottom + 10., Align::Centre);

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
// TextLine
//------------------------------------------------------------------------
TextLine::TextLine (const CRect& size, const Lettering::Style& style, const CColor& colour, Align align)
: CView (size)
, mStyle (style)
, mColour (colour)
, mAlign (align)
{
	setMouseEnabled (false);
}

void TextLine::setText (const std::string& text)
{
	if (text != mText)
	{
		mText = text;
		invalid ();
	}
}

void TextLine::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	const CCoord x = (mAlign == Align::Left) ? r.left : (mAlign == Align::Right) ? r.right : r.getCenter ().x;
	drawLettering (context, mText, mStyle, mColour, x, (r.top + r.bottom) / 2., mAlign);
	setDirty (false);
}

//------------------------------------------------------------------------
} // namespace Panel
} // namespace FilterDrum
