//------------------------------------------------------------------------
// FilterDrum - 70s sci-fi lettering
//
// CARRIED OVER FROM R2Synth (source/SciFiLettering.h, 1.1.0.0) unchanged
// but for the namespace, so the two instruments share one face.
//
// Andy's own hand-drawn stroke face: heavy, wide, rounded-square
// capitals and figures. Every glyph is a path on a 4 x 6 grid (y down),
// STROKED with round caps and joins - there is no font file and no font
// licence. In anything public call it "70s sci-fi lettering" and nothing
// else. (The same table drives the HTML mock-ups, Designs/polivoks-src/rbfont.js.)
//
// THIS HEADER INCLUDES NO VSTGUI, so three things read the same numbers:
//
//   * FilterDrumPanelViews.cpp, which strokes the glyphs with a
//     CGraphicsPath (Panel::drawLettering);
//   * tests/LayoutTests.cpp, which asks width() whether every reading,
//     label and caption fits its place;
//   * tools/dump-layout.cpp, which prints the parsed glyphs for
//     tools/render-panel.py, so the picture draws the same strokes.
//
// THE METRICS, in grid units, scaled by cap / 6:
//
//   advance   each glyph's own width (the table), + the stroke weight
//             + the tracking (1.05) between letters
//   space     2.6
//   weight    1.25 bold, 0.95 regular, 1.7 for the wordmark - the
//             stroke's width, so the ink box is cap + weight tall
//
// Lowercase is drawn in capitals. A character the face has no glyph for
// is drawn as a centred dot; Latin-1 accented letters are drawn as their
// plain capitals (a licensee's name can have an accent in it).
//------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace FilterDrum {
namespace Lettering {

//------------------------------------------------------------------------
/** A text style: the cap height in pixels, the stroke weight and the
    tracking in grid units. */
struct Style
{
	double cap;
	double weight;
	double track = 1.05;

	/** Pixels per grid unit. */
	constexpr double scale () const { return cap / 6.0; }

	/** The stroke's width in pixels. */
	constexpr double strokePx () const { return weight * scale (); }

	/** The height of the ink, strokes included. */
	constexpr double height () const { return cap + strokePx (); }
};

constexpr double kBold    = 1.25;
constexpr double kRegular = 0.95;
constexpr double kSpace   = 2.6;

//------------------------------------------------------------------------
/** One drawing command, in grid units. Q has a control point (x1, y1)
    and an end (x, y); M and L only an end; Z closes the subpath. */
struct Command
{
	char op;
	double x1, y1, x, y;
};

struct Glyph
{
	double advance;
	const char* path;
};

//------------------------------------------------------------------------
/** THE TABLE. Z closes to the start of the CURRENT subpath, not the
    glyph's - a glyph with several M commands closes with an explicit L
    (this is how B first came out as a 3). */
inline const Glyph* glyphFor (uint32_t c)
{
	struct Entry { uint32_t code; Glyph glyph; };
	static const Entry kTable[] = {
		{ 'A', { 4, "M0,6 L0,2 Q0,0 2,0 Q4,0 4,2 L4,6 M0,3.6 L4,3.6" } },
		{ 'B', { 4, "M0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3 M2.8,3 Q4,3 4,4.5 Q4,6 2.8,6 L0,6 L0,0" } },
		{ 'C', { 4, "M4,0 L1.6,0 Q0,0 0,1.6 L0,4.4 Q0,6 1.6,6 L4,6" } },
		{ 'D', { 4, "M0,0 L2,0 Q4,0 4,2 L4,4 Q4,6 2,6 L0,6 Z" } },
		{ 'E', { 4, "M4,0 L0,0 L0,6 L4,6 M0,3 L3.2,3" } },
		{ 'F', { 4, "M4,0 L0,0 L0,6 M0,3 L3.2,3" } },
		{ 'G', { 4, "M4,0 L1.6,0 Q0,0 0,1.6 L0,4.4 Q0,6 1.6,6 L4,6 L4,3.2 L2.2,3.2" } },
		{ 'H', { 4, "M0,0 L0,6 M4,0 L4,6 M0,3 L4,3" } },
		{ 'I', { 0, "M0,0 L0,6" } },
		{ 'J', { 4, "M4,0 L4,4.4 Q4,6 2.4,6 L0,6" } },
		{ 'K', { 4, "M0,0 L0,6 M0,3 L1.8,3 L4,0 M1.8,3 L4,6" } },
		{ 'L', { 4, "M0,0 L0,6 L4,6" } },
		{ 'M', { 5, "M0,6 L0,1.4 Q0,0 1.25,0 Q2.5,0 2.5,1.4 L2.5,4.2 M2.5,1.4 Q2.5,0 3.75,0 Q5,0 5,1.4 L5,6" } },
		{ 'N', { 4, "M0,6 L0,0 L4,6 L4,0" } },
		{ 'O', { 4, "M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z" } },
		{ 'P', { 4, "M0,6 L0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3" } },
		{ 'Q', { 4, "M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z M2.6,4.4 L4.3,6.2" } },
		{ 'R', { 4, "M0,6 L0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3 M2.4,3 L4,6" } },
		{ 'S', { 4, "M4,0 L1.4,0 Q0,0 0,1.5 Q0,3 1.5,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.6,6 L0,6" } },
		{ 'T', { 4, "M0,0 L4,0 M2,0 L2,6" } },
		{ 'U', { 4, "M0,0 L0,4 Q0,6 2,6 Q4,6 4,4 L4,0" } },
		{ 'V', { 4, "M0,0 L0,3.4 L2,6 L4,3.4 L4,0" } },
		{ 'W', { 5, "M0,0 L0,4.6 Q0,6 1.25,6 Q2.5,6 2.5,4.6 L2.5,1.8 M2.5,4.6 Q2.5,6 3.75,6 Q5,6 5,4.6 L5,0" } },
		{ 'X', { 4, "M0,0 L4,6 M4,0 L0,6" } },
		{ 'Y', { 4, "M0,0 L0,1.4 Q0,3 2,3 Q4,3 4,1.4 L4,0 M2,3 L2,6" } },
		{ 'Z', { 4, "M0,0 L4,0 L0,6 L4,6" } },
		{ '0', { 4, "M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z" } },
		{ '1', { 1.2, "M0,1 L1.2,0 L1.2,6" } },
		{ '2', { 4, "M0,0 L2.5,0 Q4,0 4,1.5 Q4,3 2.5,3 L1.5,3 Q0,3 0,4.5 L0,6 L4,6" } },
		{ '3', { 4, "M0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L1.2,3 M2.8,3 Q4,3 4,4.5 Q4,6 2.8,6 L0,6" } },
		{ '4', { 4, "M3,6 L3,0 L0,4 L4,4" } },
		{ '5', { 4, "M4,0 L0,0 L0,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.5,6 L0,6" } },
		{ '6', { 4, "M3.6,0 L1.6,0 Q0,0 0,1.6 L0,4.5 Q0,6 1.5,6 L2.5,6 Q4,6 4,4.5 Q4,3 2.5,3 L0,3" } },
		{ '7', { 4, "M0,0 L4,0 L1.6,6" } },
		{ '8', { 4, "M1.5,3 Q0,3 0,1.5 Q0,0 1.5,0 L2.5,0 Q4,0 4,1.5 Q4,3 2.5,3 Z M1.5,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.5,6 L1.5,6 Q0,6 0,4.5 Q0,3 1.5,3" } },
		{ '9', { 4, "M0.4,6 L2.4,6 Q4,6 4,4.4 L4,1.5 Q4,0 2.5,0 L1.5,0 Q0,0 0,1.5 Q0,3 1.5,3 L4,3" } },
		{ '.', { 0, "M0,6 L0,6.01" } },
		{ ',', { 0, "M0,6 L-0.4,7" } },
		{ ':', { 0, "M0,2 L0,2.01 M0,5.5 L0,5.51" } },
		{ 0x00B7, { 0, "M0,3 L0,3.01" } },        // middle dot
		{ 0x2022, { 0, "M0,3 L0,3.01" } },        // bullet
		{ 0x25CF, { 1.6, "M0.8,3 L0.8,3.01" } },  // black circle
		{ '%', { 4, "M0,0 L1.1,0 L1.1,1.3 L0,1.3 Z M4,0 L0,6 M2.9,4.7 L4,4.7 L4,6 L2.9,6 Z" } },
		{ '-', { 3, "M0,3 L3,3" } },
		{ 0x2212, { 3, "M0,3 L3,3" } },           // minus
		{ 0x2013, { 3, "M0,3 L3,3" } },           // en dash
		{ 0x2014, { 4, "M0,3 L4,3" } },           // em dash
		{ '+', { 3, "M0,3 L3,3 M1.5,1.5 L1.5,4.5" } },
		{ 0x00D7, { 3, "M0,1.6 L3,4.6 M3,1.6 L0,4.6" } },   // times
		{ '/', { 3, "M0,6 L3,0" } },
		{ '=', { 3, "M0,2 L3,2 M0,4 L3,4" } },
		{ 0x2192, { 4.4, "M0,3 L4.4,3 M2.8,1.4 L4.4,3 L2.8,4.6" } },   // right arrow
		{ 0x2193, { 3, "M1.5,0 L1.5,6 M0,4.5 L1.5,6 L3,4.5" } },      // down arrow
		{ 0x2039, { 2.2, "M2.2,1 L0,3 L2.2,5" } },   // single left angle quote
		{ 0x203A, { 2.2, "M0,1 L2.2,3 L0,5" } },     // single right angle quote
		{ '\'', { 0, "M0,0 L0,1.6" } },
		{ '(', { 1.2, "M1.2,0 Q0,1 0,3 Q0,5 1.2,6" } },
		{ ')', { 1.2, "M0,0 Q1.2,1 1.2,3 Q1.2,5 0,6" } },
		// ---- added for R2Synth: characters its readouts and the licence
		// lines can contain, in the same hand (FilterDrum keeps them) ----------------------------
		{ '>', { 2.2, "M0,1 L2.2,3 L0,5" } },
		{ '<', { 2.2, "M2.2,1 L0,3 L2.2,5" } },
		{ '!', { 0, "M0,0 L0,4 M0,6 L0,6.01" } },
		{ '?', { 3.4, "M0,1.2 Q0,0 1.5,0 L1.9,0 Q3.4,0 3.4,1.5 Q3.4,2.8 1.7,3.4 L1.7,4.2 M1.7,6 L1.7,6.01" } },
		{ '"', { 1.2, "M0,0 L0,1.6 M1.2,0 L1.2,1.6" } },
		{ '|', { 0, "M0,-0.4 L0,6.4" } },
		{ '[', { 1.2, "M1.2,0 L0,0 L0,6 L1.2,6" } },
		{ ']', { 1.2, "M0,0 L1.2,0 L1.2,6 L0,6" } },
		{ '_', { 4, "M0,6.6 L4,6.6" } },
		{ '&', { 4, "M4,6 L1.2,2.6 Q0.4,1.7 0.8,0.8 Q1.2,0 2,0 Q2.8,0 3.1,0.8 Q3.4,1.8 1.6,3 Q0,4 0,4.8 Q0,6 1.6,6 Q3,6 4,3.6" } },
		{ '#', { 4, "M1.2,0.6 L0.8,5.4 M3.2,0.6 L2.8,5.4 M0,2 L4,2 M0,4 L4,4" } },
		{ '@', { 4.6, "M3.2,4 L3.2,2 M3.2,3 Q3.2,4 4,4 Q4.6,4 4.6,3 Q4.6,0 2.3,0 Q0,0 0,3 Q0,6 2.3,6 L3.6,6 M3.2,3 Q3.2,2 2.3,2 Q1.4,2 1.4,3 Q1.4,4 2.3,4 Q3.2,4 3.2,3" } },
		{ '*', { 3, "M1.5,1 L1.5,4 M0.2,1.8 L2.8,3.2 M2.8,1.8 L0.2,3.2" } },
		{ 0x00B0, { 1.6, "M0.8,0 Q1.6,0 1.6,0.8 Q1.6,1.6 0.8,1.6 Q0,1.6 0,0.8 Q0,0 0.8,0 Z" } },   // degree
	};
	for (const auto& e : kTable)
		if (e.code == c)
			return &e.glyph;
	return nullptr;
}

/** The glyph drawn for a character the table lacks: a centred dot. */
inline const Glyph& missingGlyph ()
{
	static const Glyph g { 0, "M0,3 L0,3.01" };
	return g;
}

//------------------------------------------------------------------------
/** UTF-8 to code points, capitals, with Latin-1 accented letters folded
    to their plain capitals. Malformed bytes become U+FFFD (drawn as the
    dot). */
inline std::vector<uint32_t> codePoints (const std::string& s)
{
	std::vector<uint32_t> out;
	out.reserve (s.size ());
	for (size_t i = 0; i < s.size ();)
	{
		const auto b = static_cast<unsigned char> (s[i]);
		uint32_t c = 0xFFFD;
		size_t n = 1;
		if (b < 0x80)
			c = b;
		else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; n = 2; }
		else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; n = 3; }
		else if ((b & 0xF8) == 0xF0) { c = b & 0x07; n = 4; }
		if (n > 1)
		{
			if (i + n > s.size ())
			{
				c = 0xFFFD;
				n = 1;
			}
			else
				for (size_t k = 1; k < n; ++k)
				{
					const auto cb = static_cast<unsigned char> (s[i + k]);
					if ((cb & 0xC0) != 0x80)
					{
						c = 0xFFFD;
						n = k;
						break;
					}
					c = (c << 6) | (cb & 0x3F);
				}
		}
		i += n;

		if (c >= 'a' && c <= 'z')
			c -= 32;
		else if (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7)
		{
			// Latin-1 letters, folded: index by (c & ~0x20) - 0xC0.
			static const char kFold[] = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTS";
			const char f = kFold[(c & ~0x20u) - 0xC0];
			c = (c == 0xFF) ? 'Y' : static_cast<uint32_t> (f);
		}
		out.push_back (c);
	}
	return out;
}

//------------------------------------------------------------------------
/** A glyph's path, parsed. Numbers may be negative and fractional. An
    unparsable path gives an empty list (the tests hold every glyph in
    the table to parsing). */
inline std::vector<Command> parse (const char* path)
{
	std::vector<Command> out;
	const char* p = path;
	auto skip = [&] { while (*p == ' ' || *p == ',') ++p; };
	auto number = [&] (double& v) {
		skip ();
		char* end = nullptr;
		v = std::strtod (p, &end);
		if (end == p)
			return false;
		p = end;
		return true;
	};
	while (true)
	{
		skip ();
		if (!*p)
			break;
		const char op = *p++;
		Command c { op, 0, 0, 0, 0 };
		bool ok = true;
		switch (op)
		{
			case 'M':
			case 'L': ok = number (c.x) && number (c.y); break;
			case 'Q': ok = number (c.x1) && number (c.y1) && number (c.x) && number (c.y); break;
			case 'Z': break;
			default: ok = false;
		}
		if (!ok || (out.empty () && op != 'M'))
			return {};
		out.push_back (c);
	}
	return out;
}

/** The glyph for a code point - the dot when there is none. */
inline const Glyph& glyphOrDot (uint32_t c)
{
	const Glyph* g = glyphFor (c);
	return g ? *g : missingGlyph ();
}

//------------------------------------------------------------------------
/** The pen positions of a string, in grid units: where each glyph's own
    x = 0 sits, before the half-stroke inset. Spaces get no entry. */
struct Placed
{
	uint32_t code;
	double x;
};

inline std::vector<Placed> layout (const std::string& text, const Style& style, double* endUnits = nullptr)
{
	std::vector<Placed> out;
	double x = 0.0;
	for (uint32_t c : codePoints (text))
	{
		if (c == ' ')
		{
			x += kSpace;
			continue;
		}
		out.push_back ({ c, x });
		x += glyphOrDot (c).advance + style.weight + style.track;
	}
	if (endUnits)
		*endUnits = x;
	return out;
}

/** THE WIDTH IN PIXELS of `text` drawn in `style`, from the left edge of
    the first stroke to the right edge of the last - what a fit test
    needs. */
inline double width (const std::string& text, const Style& style)
{
	double end = 0.0;
	layout (text, style, &end);
	return (end > 0.0) ? (end - style.track) * style.scale () : 0.0;
}

//------------------------------------------------------------------------
} // namespace Lettering
} // namespace FilterDrum
