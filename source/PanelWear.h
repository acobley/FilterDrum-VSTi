//------------------------------------------------------------------------
// FilterDrum - the panel's wear: "Studio Worn"
//
// CARRIED OVER FROM R2Synth (source/PanelWear.h, 1.1.0.0), whose shipped
// look this is (R2Synth-VSTi/Designs/R2Synth-Weathered-1-StudioWorn.png),
// for the Mirror Columns panel (branch PolivoksUI, Oct 2026). The same
// generator and the same kinds of mark; the changes are FilterDrum's: its
// own seeds, foot rings only round knobs and the selector (the step keys
// and Run sit in the panel, not on it), and the fingerprints placed for
// its four screens. The black panel after ten years on a desk. Light wear only - nothing may cost a label its
// legibility:
//
//   PANEL    a patchy haze of dust, heavier towards the top and the right
//            (where hands reach least); dust drifted against the inside
//            of each section's box; a ring of dust round the foot of
//            every knob and selector; hairline scratches, some in
//            clusters (something slid across it), some alone; specks and
//            the odd fibre.
//   KNOBS    the cap polished by fingers - a soft sheen at the top left -
//            and fine circular scuffs from turning; dust in the knurling.
//            Both TURN WITH THE KNOB: they are stored relative to the
//            pointer, so a knob at 3 and a knob at 7 show the same marks
//            in different places, as a real one would.
//   SELECTORS  grit round the chicken head's hub, turning with it.
//   SCREENS  dust specks on the glass, the odd fibre, and a fingerprint
//            on drum 1's filter and drum 2's envelope screens.
//
// WHY DATA AND NOT A PICTURE. The panel has no bitmaps (FilterDrumPanel-
// Views.h, "the same two rules"): everything is paths, so it is sharp at
// any scale. The wear keeps that rule. Every mark is GENERATED here, from
// a fixed seed, as plain numbers - circles, polylines, arcs, soft blobs -
// in panel units. The views draw them; tests/LayoutTests.cpp holds them
// to the panel (inside it, off the screens' cut-outs, faint enough);
// tools/dump-layout.cpp prints them so docs/panel.png shows the same
// marks. Same seed, same marks, on every machine: the generator uses its
// own random numbers (SplitMix64) and its own normal distribution, never
// <random>'s, whose distributions differ between standard libraries -
// and takes ONE random number per statement, never two in the arguments
// of one call, whose order of evaluation is up to the compiler (g++ on
// x86 and the Mac's arm64 build disagreed, 22 Oct 2026).
//
// NO VSTGUI here, like FilterDrumLayout.h, so all three can read it.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace FilterDrum {
namespace Wear {

//------------------------------------------------------------------------
// The colours of the dirt, as RGB. Alphas are 0..255 and travel with
// each mark.
//------------------------------------------------------------------------
struct Rgb { uint8_t r, g, b; };

constexpr Rgb kDust      { 150, 146, 138 };   // haze, drifts, foot rings
constexpr Rgb kSpeck     { 175, 170, 160 };   // specks and fibres on the panel
constexpr Rgb kScratch   { 120, 120, 116 };   // hairlines through the black paint
constexpr Rgb kScuff     { 150, 150, 148 };   // turning marks on a knob's cap
constexpr Rgb kKnurlDust { 140, 136, 128 };   // dust in the knurling
constexpr Rgb kGlassDust { 170, 170, 168 };   // specks on a screen's glass
constexpr Rgb kPrint     { 200, 200, 205 };   // a fingerprint's ridges
constexpr Rgb kSheen     { 255, 255, 255 };   // a polished cap

/** The most any one mark may cover, so the wear can never hide a label:
    the tests hold every alpha below these. */
constexpr int kMaxHazeAlpha    = 20;
constexpr int kMaxMarkAlpha    = 130;   // specks, scratches, scuffs - all under 1.4 px across
constexpr int kMaxPrintAlpha   = 18;

//------------------------------------------------------------------------
// The marks
//------------------------------------------------------------------------
struct Pt { double x, y; };

/** A dot of dust: a filled circle. */
struct Speck
{
	double x, y, r;
	uint8_t alpha;
};

/** A soft blob of haze: alpha at the centre, fading to none at r. */
struct Blob
{
	double x, y, r;
	uint8_t alpha;
};

/** Dust drifted against a box edge: a rectangle whose alpha fades from
    the edge side (alpha) to nothing at the other. `fromX`/`fromY` say
    which side is the edge: (-1, 0) the left, (1, 0) the right, (0, -1)
    the top, (0, 1) the bottom. */
struct Drift
{
	Layout::Rect r;
	int fromX, fromY;
	uint8_t alpha;
};

/** Dust round the foot of a knob: a ring, peak alpha at `peak`, fading
    to none at `inner` and `outer`. */
struct FootRing
{
	double cx, cy, inner, peak, outer;
	uint8_t alpha;
};

/** A hairline: a polyline. Drawn at full alpha in the middle third and
    half alpha at either end, so it fades in and out. */
struct Line
{
	std::vector<Pt> pts;
	double width;
	uint8_t alpha;
};

/** A scuff on a knob's cap: an arc about the knob's centre. `radius` is
    a fraction of the CAP's radius; `start` is degrees clockwise FROM THE
    POINTER, so it turns with the knob. */
struct Arc
{
	double radius, start, span, width;
	uint8_t alpha;
};

/** A speck that turns with its knob: polar, radius as a fraction of the
    KNOB BODY's radius, angle in degrees from the pointer. */
struct PolarSpeck
{
	double radius, angle, r;
	uint8_t alpha;
};

/** A fingerprint on glass: concentric elliptical ridges round (x, y),
    the outermost semi-axes rx, ry, tilted `tilt` degrees. Each ridge is
    a closed polyline already wobbled, so the drawing just strokes it;
    ridges fade towards the edge of the print. */
struct Print
{
	double x, y, rx, ry, tilt;
	std::vector<Line> ridges;
};

struct PanelWear
{
	std::vector<Blob> haze;
	std::vector<Drift> drifts;
	std::vector<FootRing> rings;
	std::vector<Line> scratches;
	std::vector<Speck> specks;
	std::vector<Line> fibres;
};

struct KnobWear
{
	std::vector<Arc> scuffs;
	std::vector<PolarSpeck> grit;       // in the knurling and on the cap
	uint8_t sheen = 0;                  // the polished patch, its alpha at the centre
	uint8_t knurlAlpha = 0;             // a faint even dust over the whole knurled ring
};

struct GlassWear
{
	std::vector<Speck> dust;
	std::vector<Line> fibres;
	std::vector<Print> prints;
};

//------------------------------------------------------------------------
// The random numbers - the same on every machine
//------------------------------------------------------------------------
class Random
{
public:
	explicit Random (uint64_t seed) : mState (seed) {}

	uint64_t next ()
	{
		uint64_t z = (mState += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	/** [0, 1) */
	double unit () { return static_cast<double> (next () >> 11) * (1.0 / 9007199254740992.0); }
	double uniform (double lo, double hi) { return lo + (hi - lo) * unit (); }
	int below (int n) { return static_cast<int> (unit () * n); }

	/** Standard normal, Box-Muller, one of the pair. */
	double normal ()
	{
		const double u = std::max (unit (), 1e-12);
		const double v = unit ();
		return std::sqrt (-2.0 * std::log (u)) * std::cos (6.283185307179586 * v);
	}

private:
	uint64_t mState;
};

//------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------
namespace Detail {

constexpr double kPi = 3.14159265358979323846;

inline uint8_t alpha (double a)
{
	return static_cast<uint8_t> (std::clamp (std::lround (a), 0L, 255L));
}

inline bool inRect (const Layout::Rect& r, double x, double y, double pad = 0.0)
{
	return x >= r.left - pad && x <= r.right + pad && y >= r.top - pad && y <= r.bottom + pad;
}

/** A gently curving line: `length` long from (x, y) at `angle` radians,
    bending by `bend` radians a step. */
inline std::vector<Pt> curve (Random& rng, double x, double y, double angle, double length, double bend,
                              double step = 3.0)
{
	std::vector<Pt> pts { { x, y } };
	const int n = std::max (2, static_cast<int> (length / step));
	for (int i = 0; i < n; ++i)
	{
		angle += bend + rng.normal () * 0.0075;
		x += std::cos (angle) * step;
		y += std::sin (angle) * step;
		pts.push_back ({ x, y });
	}
	return pts;
}

/** The knob's centre and body radius, as Panel::Knob draws it. */
inline void knobGeometry (const Layout::Control& c, double& cx, double& cy, double& body)
{
	const Layout::Rect r = Layout::controlRect (c);
	cx = (r.left + r.right) / 2.0;
	cy = r.top + c.diameter / 2.0;
	body = c.diameter / 2.0 - ((c.diameter >= 44) ? 6.0 : 5.0);
}

/** Does a polyline come within `pad` of any screen's cut-out? Scratches
    on the panel stop at the glass - the screens get their own wear. */
inline bool crossesAScreen (const std::vector<Pt>& pts, double pad)
{
	for (const auto& s : Layout::kScreens)
		for (const auto& p : pts)
			if (inRect (Layout::bezelOf (s), p.x, p.y, pad))
				return true;
	return false;
}

inline bool insidePanel (const std::vector<Pt>& pts, double margin)
{
	for (const auto& p : pts)
		if (p.x < margin || p.y < margin || p.x > Layout::kEditorWidth - margin ||
		    p.y > Layout::kEditorHeight - margin)
			return false;
	return true;
}

/** "Reach": how much more dust collects here, 0..1 - the top and the
    right, which hands reach least. */
inline double reach (double x, double y)
{
	return std::clamp (0.5 + 0.3 * x / Layout::kEditorWidth - 0.25 * y / Layout::kEditorHeight, 0.0, 1.0);
}

} // namespace Detail

//------------------------------------------------------------------------
// The panel
//------------------------------------------------------------------------
inline PanelWear panelWear ()
{
	using namespace Layout;
	using namespace Detail;
	Random rng (0x46696C74'4472756Dull);   // "FiltDrum"
	PanelWear w;
	const double W = kEditorWidth, H = kEditorHeight;

	// ---- the haze: soft blobs, more of them up and to the right ----------
	for (int i = 0; i < 90; ++i)
	{
		const double x = rng.uniform (0, W), y = rng.uniform (0, H);
		const double r = rng.uniform (28, 95);
		const double a = rng.uniform (4, 14) * (0.4 + 0.8 * reach (x, y));
		w.haze.push_back ({ x, y, r, alpha (std::min<double> (a, kMaxHazeAlpha)) });
	}

	// ---- dust drifted against the inside of each box: short runs -------
	for (const auto& s : kSections)
	{
		const Rect& b = s.r;
		const int depth = 6;
		auto run = [&] (bool horizontal, double from, double to, int fromX, int fromY, double fixed) {
			double at = from;
			while (at < to)
			{
				const double len = std::min (to - at, rng.uniform (18, 70));
				const uint8_t a = alpha (rng.uniform (3, 13));
				Rect r {};
				if (horizontal)
					r = { static_cast<int> (at), static_cast<int> (fromY < 0 ? fixed : fixed - depth),
					      static_cast<int> (at + len), static_cast<int> (fromY < 0 ? fixed + depth : fixed) };
				else
					r = { static_cast<int> (fromX < 0 ? fixed : fixed - depth), static_cast<int> (at),
					      static_cast<int> (fromX < 0 ? fixed + depth : fixed), static_cast<int> (at + len) };
				if (rng.unit () < 0.8)
					w.drifts.push_back ({ r, fromX, fromY, a });
				at += len;
			}
		};
		run (true, b.left + 2, b.right - 2, 0, -1, b.top + 1.5);       // along the top, inside
		run (true, b.left + 2, b.right - 2, 0, 1, b.bottom - 1.5);     // the bottom
		run (false, b.top + 2, b.bottom - 2, -1, 0, b.left + 1.5);     // the left
		run (false, b.top + 2, b.bottom - 2, 1, 0, b.right - 1.5);     // the right
	}

	// ---- a ring of dust round the foot of every knob and selector --------
	for (const auto& c : kControls)
	{
		if (c.kind == Kind::Knob)
		{
			double cx, cy, body;
			knobGeometry (c, cx, cy, body);
			w.rings.push_back ({ cx, cy, body - 1.0, body + 1.5, body + 5.0, alpha (rng.uniform (20, 30)) });
		}
		else if (c.kind == Kind::Switch)
		{
			const Selector g = selectorOf (c, paramDef (c.tag).stepCount + 1);
			const double hub = g.radius * 0.8;
			w.rings.push_back ({ g.cx, g.cy, hub - 1.0, hub + 1.2, hub + 4.0, alpha (rng.uniform (18, 26)) });
		}
	}

	// ---- hairline scratches: clusters, then strays -----------------------
	auto addScratch = [&] (std::vector<Pt> pts, double width, double a) {
		if (crossesAScreen (pts, 2.0) || !insidePanel (pts, 3.0))
			return;
		w.scratches.push_back ({ std::move (pts), width, alpha (std::min<double> (a, kMaxMarkAlpha)) });
	};
	for (int k = 0; k < 9; ++k)
	{
		const double cx = rng.uniform (0, W), cy = rng.uniform (0, H);
		const double angle = 0.15 + rng.normal () * 0.4;
		const int n = 3 + rng.below (6);
		for (int i = 0; i < n; ++i)
		{
			// One draw per statement: the order a function's arguments
			// are evaluated in is the compiler's choice, and the marks
			// must not depend on it.
			const double x = cx + rng.normal () * 30.0;
			const double y = cy + rng.normal () * 18.0;
			const double a = angle + rng.normal () * 0.08;
			const double length = rng.uniform (20, 110);
			const double bend = rng.normal () * 0.002;
			auto pts = curve (rng, x, y, a, length, bend);
			const double width = rng.uniform (0.25, 0.45);
			const double strength = rng.uniform (0.15, 0.4) * 0.9 * 255;
			addScratch (std::move (pts), width, strength);
		}
	}
	for (int i = 0; i < 40; ++i)
	{
		const double x = rng.uniform (0, W);
		const double y = rng.uniform (0, H);
		const double a = rng.uniform (0, 2 * kPi);
		const double length = rng.uniform (8, 45);
		const double bend = rng.normal () * 0.01;
		auto pts = curve (rng, x, y, a, length, bend);
		const double strength = rng.uniform (0.1, 0.3) * 0.9 * 255;
		addScratch (std::move (pts), 0.3, strength);
	}

	// ---- specks, and the odd fibre -----------------------------------------
	for (int i = 0; i < 1100; ++i)
	{
		const double x = rng.uniform (1, W - 1), y = rng.uniform (1, H - 1);
		bool onGlass = false;
		for (const auto& s : kScreens)
			onGlass = onGlass || inRect (s, x, y, 1.0);
		if (onGlass)
			continue;
		w.specks.push_back ({ x, y, rng.uniform (0.15, 0.6), alpha (rng.uniform (0.15, 0.45) * 255) });
	}
	for (int i = 0; i < 10; ++i)
	{
		const double x = rng.uniform (20, W - 20);
		const double y = rng.uniform (20, H - 20);
		const double a = rng.uniform (0, 2 * kPi);
		const double length = rng.uniform (10, 35);
		const double bend = rng.normal () * 0.12;
		auto pts = curve (rng, x, y, a, length, bend, 1.5);
		const uint8_t strength = alpha (rng.uniform (0.25, 0.5) * 255);
		if (!crossesAScreen (pts, 1.0) && insidePanel (pts, 2.0))
			w.fibres.push_back ({ std::move (pts), 0.35, strength });
	}
	return w;
}

/** A control's place in kControls - the seed of its wear - or -1. */
inline int controlIndex (Steinberg::Vst::ParamID tag)
{
	for (int i = 0; i < Layout::kNumControls; ++i)
		if (Layout::kControls[i].tag == tag)
			return i;
	return -1;
}

//------------------------------------------------------------------------
// A knob - `index` is its place in kControls, so every knob is its own
//------------------------------------------------------------------------
inline KnobWear knobWear (int index)
{
	using namespace Detail;
	Random rng (0x4B6E6F62'46440000ull + static_cast<uint64_t> (index) * 0x9E37ull);   // "Knob"
	KnobWear k;
	const int n = 8 + rng.below (11);
	for (int i = 0; i < n; ++i)
		k.scuffs.push_back ({ rng.uniform (0.25, 0.95), rng.uniform (0, 360), rng.uniform (17, 92),
		                      rng.uniform (0.3, 0.65), alpha (std::min (rng.uniform (0.12, 0.35) * 255.0,
		                                                                static_cast<double> (kMaxMarkAlpha))) });
	// grit in the knurling (between the cap, at 0.78 of the body, and the
	// teeth) and a little on the cap
	for (int i = 0; i < 22; ++i)
		k.grit.push_back ({ rng.uniform (0.8, 0.97), rng.uniform (0, 360), rng.uniform (0.2, 0.55),
		                    alpha (rng.uniform (0.2, 0.42) * 255) });
	for (int i = 0; i < 4; ++i)
		k.grit.push_back ({ rng.uniform (0.1, 0.72), rng.uniform (0, 360), rng.uniform (0.15, 0.4),
		                    alpha (rng.uniform (0.15, 0.35) * 255) });
	k.sheen = alpha (rng.uniform (12, 17));
	k.knurlAlpha = alpha (rng.uniform (0.18, 0.26) * 255 * 0.5);
	return k;
}

/** A selector's grit, round the hub's edge - the same shape as a knob's
    (PolarSpeck radius as a fraction of the HUB). */
inline std::vector<PolarSpeck> selectorGrit (int index)
{
	using namespace Detail;
	Random rng (0x53656C65'46440000ull + static_cast<uint64_t> (index) * 0x9E37ull);   // "Sele"
	std::vector<PolarSpeck> g;
	for (int i = 0; i < 12; ++i)
		g.push_back ({ rng.uniform (0.82, 1.0), rng.uniform (0, 360), rng.uniform (0.2, 0.5),
		               alpha (rng.uniform (0.2, 0.4) * 255) });
	return g;
}

//------------------------------------------------------------------------
// A screen's glass - `index` is its place in kScreens
//------------------------------------------------------------------------
inline GlassWear glassWear (int index)
{
	using namespace Layout;
	using namespace Detail;
	const Rect s = kScreens[index];
	Random rng (0x476C6173'46440000ull + static_cast<uint64_t> (index) * 0x9E37ull);   // "Glas"
	GlassWear g;
	const double area = static_cast<double> (s.right - s.left) * (s.bottom - s.top);
	const int n = static_cast<int> (area / 280.0);   // about 200 over all four
	for (int i = 0; i < n; ++i)
		g.dust.push_back ({ rng.uniform (s.left + 1, s.right - 1), rng.uniform (s.top + 1, s.bottom - 1),
		                    rng.uniform (0.2, 0.65), alpha (rng.uniform (0.15, 0.5) * 255) });
	if (index == 0 || index == 2)
	{
		const double x = rng.uniform (s.left + 20, s.right - 40);
		const double y = rng.uniform (s.top + 15, s.bottom - 15);
		const double a = rng.uniform (0, 2 * kPi);
		const double length = rng.uniform (10, 22);
		const double bend = rng.normal () * 0.15;
		auto pts = curve (rng, x, y, a, length, bend, 1.5);
		const uint8_t strength = alpha (rng.uniform (0.3, 0.5) * 255);
		bool inside = true;
		for (const auto& p : pts)
			inside = inside && inRect (s, p.x, p.y, -1.0);
		if (inside)
			g.fibres.push_back ({ std::move (pts), 0.35, strength });
	}

	// The fingerprints: one on drum 1's filter screen, towards its right,
	// and one on drum 2's envelope screen, over the tail.
	auto print = [&] (double x, double y, double rx, double ry, double tilt) {
		Print p { x, y, rx, ry, tilt, {} };
		const double ct = std::cos (tilt * kPi / 180.0), st = std::sin (tilt * kPi / 180.0);
		const int ridges = 12;
		for (int i = 1; i <= ridges; ++i)
		{
			const double f = static_cast<double> (i) / ridges;   // 0 centre .. 1 edge
			Line ridge { {}, 0.45, alpha (kMaxPrintAlpha * std::exp (-f * f * 2.2)) };
			const double phase = rng.uniform (0, 2 * kPi);
			const int steps = 40;
			for (int k = 0; k <= steps; ++k)
			{
				const double t = 2 * kPi * k / steps;
				const double wobble = 1.0 + 0.04 * std::sin (3 * t + phase) + 0.025 * std::sin (5 * t + 2 * phase);
				const double u = rx * f * wobble * std::cos (t), v = ry * f * wobble * std::sin (t);
				double px = x + u * ct - v * st, py = y + u * st + v * ct;
				px = std::clamp (px, s.left + 0.5, s.right - 0.5);
				py = std::clamp (py, s.top + 0.5, s.bottom - 0.5);
				ridge.pts.push_back ({ px, py });
			}
			p.ridges.push_back (std::move (ridge));
		}
		g.prints.push_back (std::move (p));
	};
	if (index == 0)
		print (s.right - 58.0, s.top + 42.0, 11.0, 15.0, 23.0);
	if (index == 3)
		print (s.left + 118.0, s.top + 44.0, 12.0, 16.0, -17.0);
	return g;
}

//------------------------------------------------------------------------
} // namespace Wear
} // namespace FilterDrum
