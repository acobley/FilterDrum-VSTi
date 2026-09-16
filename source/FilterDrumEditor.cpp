//------------------------------------------------------------------------
// FilterDrum - editor implementation
//------------------------------------------------------------------------

#include "FilterDrumEditor.h"
#include "FilterDrumController.h"
#include "FilterDrumDsp.h"
#include "FilterDrumTransport.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace VSTGUI;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace FilterDrum {

//------------------------------------------------------------------------
namespace {

/** THE LAYOUT, AS DATA.
 
    Positions live here and are read by open(), rather than being typed
    into the middle of it. On a port they come out of the DXi's dialog
    resource, solved once and verified against a second control (see
    PORTING-GUIDE.md section 6); there is no dialog behind this one, so
    they are chosen - but chosen in ONE PLACE, so the panel can be
    re-proportioned without reading the construction code.
 
    SEVEN columns, because the VCF row is the widest and Noise Level
    joined it. The SlideSpin these controls descend from was 69 x 44;
    94 is that widened until a four-character reading and a
    twelve-character label both fit without dropping a font size.

    kEditorWidth is the seven columns, the envelope strip and the
    crossfader; the static_assert under kMixX has the arithmetic. None
    of the three right-hand pieces is a column - see kMixWidth - so
    adding or removing a column moves the panel's width by kColumnPitch
    and carries all three with it. */
constexpr int kMargin       = 16;
constexpr int kColumnWidth  = 94;
constexpr int kColumnGap    = 8;
constexpr int kColumnPitch  = kColumnWidth + kColumnGap;
constexpr int kSliderHeight = 44;

constexpr int kTitleY       = 10;
constexpr int kLabelHeight  = 16;

/** THE BOXES, and the padding that makes room for them.

    Every group of controls sits in a SpyGroupBox with its name on the
    top edge, and each drum's three groups sit inside an outer box of
    their own. Position alone had been doing all the grouping, which was
    readable once you knew the layout and gave a newcomer nothing.

    So the columns move inboard twice - once for the drum box, once for
    the group box - and kContentX, not kMargin, is where column 0 starts.
    Everything horizontal is derived from it, so changing a padding moves
    the panel's width rather than making two boxes overlap. */
constexpr int kGroupPadX      = 5;    // group box -> the sliders in it
constexpr int kGroupPadTop    = 9;    // enough for the title on the edge
constexpr int kGroupPadBottom = 5;
constexpr int kGroupGap       = 6;    // between two group boxes
constexpr int kDrumPad        = 6;    // drum box -> the group boxes in it

constexpr int kGroupX       = kMargin + kDrumPad;
constexpr int kContentX     = kGroupX + kGroupPadX;   // column 0
constexpr int kGroupHeight  = kSliderHeight + kGroupPadTop + kGroupPadBottom;
constexpr int kRowPitch     = kGroupHeight + kGroupGap;

/** The two drum blocks. Each is a section heading, a seven-column VCF
    row and a four-column VCA row; drum 2's is the same shape 132 pixels
    further down. */
/** The two drum boxes. Everything inside one is measured from its top,
    and drum 2's box is drum 1's box one kBlockPitch lower - so a fourth
    row added to a drum moves drum 2, the readout lines and the
    sequencer rather than overlapping any of them. */
constexpr int kDrum1BoxY    = 32;
constexpr int kDrum1LabelY  = kDrum1BoxY + 6;
constexpr int kDrum1VcfY    = kDrum1LabelY + kLabelHeight + kGroupPadTop + 2;
constexpr int kDrum1VcaY    = kDrum1VcfY + kRowPitch;
constexpr int kDrum1ShapeY  = kDrum1VcaY + kRowPitch;
constexpr int kDrumBoxH     = (kDrum1ShapeY + kSliderHeight + kGroupPadBottom
                               + kDrumPad) - kDrum1BoxY;

constexpr int kBlockPitch   = kDrumBoxH + 12;
constexpr int kDrum2BoxY    = kDrum1BoxY   + kBlockPitch;
constexpr int kDrum2LabelY  = kDrum1LabelY + kBlockPitch;
constexpr int kDrum2VcfY    = kDrum1VcfY   + kBlockPitch;
constexpr int kDrum2VcaY    = kDrum1VcaY   + kBlockPitch;
constexpr int kDrum2ShapeY  = kDrum1ShapeY + kBlockPitch;

/** THE OUTPUT GROUP, and why the trim had to leave the drum.

    The output trim used to sit in drum 2's bottom row, two columns
    clear of the controls, with that gap doing the work of saying it was
    not one of them. Boxing the rows took that argument away: inside the
    box the gap says nothing, and the trim was left looking like a
    fourth envelope shape.

    So it gets a box of its own, below both drums and belonging to
    neither - which is what it always was. The two readout lines sit
    beside it rather than under it, because the box is one column wide
    and the lines are the width of the panel. */
constexpr int kOutputBoxY   = kDrum2BoxY + kDrumBoxH + 12;
constexpr int kOutputBoxW   = kColumnWidth + 2 * kGroupPadX;
constexpr int kTrimRowY     = kOutputBoxY + kGroupPadTop;
constexpr int kTrimColumn   = 0;

constexpr int kReadoutX     = kMargin + kOutputBoxW + 14;
constexpr int kVelocityY    = kTrimRowY + 2;
constexpr int kRateY        = kVelocityY + 22;

/** THE SEQUENCER ROW, along the bottom.

    Sixteen switches across the panel's full width, with Run and the
    launch division to their right - the two controls that decide what
    the row does, next to the row they act on.

    The switches are SMALL: the row has to hold sixteen where the drum
    rows hold seven, so a step is a third the width of a slider. That is
    enough for a two-character label and the lamp, which is all a step
    needs to say. */
/** The sequencer's own label sits ABOVE its box, so it needs clearance
    from the box's title, which sits ON the top edge. Eighteen was the
    gap before there was a box; it put the two lines through each
    other. */
constexpr int kSeqLabelY    = kOutputBoxY + kGroupHeight + 12;
constexpr int kSeqRowY      = kSeqLabelY + kLabelHeight + kGroupPadTop + 5;
constexpr int kStepGap      = 4;
constexpr int kStepHeight   = 40;

/** Run and Launch On, RIGHT-ANCHORED, with the sixteen steps filling
    everything left of them.

    THE STEP WIDTH IS DERIVED, not typed. It was 30, which filled a
    798-pixel panel; the envelope strip made the panel 986 and a typed
    30 would have left a two-hundred-pixel hole in the middle of the
    row - the kind of gap that reads as a missing control. Deriving it
    means the row fills whatever width the panel ends up with, and the
    static_assert below says the arithmetic came out whole. */
constexpr int kSeqCtrlW     = 100;
constexpr int kSeqCtrlGap   = 6;
constexpr int kSeqCtrlSpan  = 2 * kSeqCtrlW + kSeqCtrlGap;
constexpr int kSeqCtrlClear = 16;   // the gap between the steps and them
constexpr int kSeqBoxRight  = FilterDrumEditor::kEditorWidth - kMargin;
constexpr int kSeqCtrlX     = kSeqBoxRight - kGroupPadX - kSeqCtrlSpan;

constexpr int kStepPitch    = (kSeqCtrlX - kSeqCtrlClear - kContentX) / 16;
constexpr int kStepWidth    = kStepPitch - kStepGap;

/** THE PANEL'S HEIGHT, asserted on the same terms as its width. The
    sequencer is the last thing down the panel, and every Y above it is
    derived, so this is where a drum block growing a row shows up. */
static_assert (kSeqRowY + kStepHeight + kGroupPadBottom + kMargin
                 <= FilterDrumEditor::kEditorHeight,
               "the sequencer box must fit inside the panel");
static_assert (kSeqLabelY + kLabelHeight < kSeqRowY - kGroupPadTop,
               "the sequencer label must clear its box's title");

/** The blocks must not overlap either - a block pitch smaller than the
    rows it contains would draw drum 2's heading through drum 1's shape
    row rather than failing anywhere visible. */
static_assert (kDrum1ShapeY + kSliderHeight < kDrum2LabelY,
               "drum 1's last row must end above drum 2's heading");

static_assert (kStepWidth >= 30,
               "a step switch narrower than 30 cannot hold its lamp and its number");
static_assert (kContentX + 16 * kStepPitch <= kSeqCtrlX - kSeqCtrlClear,
               "the sixteen steps must not run into Run");

/** THE ENVELOPE DISPLAYS, one per drum, in a strip of their own.

    WHY A STRIP AND NOT THE DEAD SPACE. The VCA rows only use four of
    the seven columns, so there were three columns going spare in each -
    and at 44 pixels tall, minus a caption band and a legend band, that
    leaves sixteen pixels of actual curve. A display that small is a
    decoration. Each of these is as tall as the whole drum block it
    belongs to, which is what makes a release you can compare by eye.

    ALIGNED WITH THE DRUM BLOCK, top and bottom: display 1 spans drum
    1's VCF row down to the foot of its VCA row, so it sits against the
    controls that determine it and the pairing needs no label to
    explain. The strip as a whole therefore spans exactly what the
    crossfader spans. */
constexpr int kEnvX         = kContentX + 7 * kColumnPitch;
constexpr int kEnvWidth     = 180;
constexpr int kEnvGap       = 8;
/** The displays line up with the GROUP BOXES either side of them, not
    with the sliders, so the strip and the three boxes share a top and a
    bottom edge and the drum reads as one rectangle of content. */
constexpr int kEnv1Top      = kDrum1VcfY - kGroupPadTop;
constexpr int kEnv1Bottom   = kDrum1ShapeY + kSliderHeight + kGroupPadBottom;
constexpr int kEnv2Top      = kDrum2VcfY - kGroupPadTop;
constexpr int kEnv2Bottom   = kDrum2ShapeY + kSliderHeight + kGroupPadBottom;

/** The group boxes are seven columns wide, all three of them, even
    though only the VCF row fills that. Boxes of three different widths
    down one drum would draw the eye to the ragged right edge rather
    than to the grouping they exist to show. */
constexpr int kGroupW       = 7 * kColumnPitch - kColumnGap + 2 * kGroupPadX;

/** The outer box: from the panel margin to just past the strip. */
constexpr int kDrumBoxX     = kMargin;
constexpr int kDrumBoxW     = (kEnvX + kEnvWidth + kDrumPad) - kDrumBoxX;

/** How many points each curve is drawn from. One per pixel of plot
    width is the most that can show; a few more costs nothing and keeps
    the trace smooth if the strip is ever widened. */
constexpr int kEnvPoints    = 200;

/** The crossfader, to the right of both drum blocks.

    IT IS NOT ON THE COLUMN GRID. It was, at a full 94-pixel column, and
    it looked wrong - a fader given a slider's width reads as a slider
    that grew rather than as a different kind of control. A fader is
    narrow; only its HEIGHT is meant to be large, and that height is what
    makes it read as the thing the two drums meet in.

    So it gets its own width, and the panel is only as wide as the seven
    columns plus this. */
constexpr int kMixWidth     = 52;
constexpr int kMixBoxX      = kDrumBoxX + kDrumBoxW + kEnvGap;
constexpr int kMixX         = kMixBoxX + kGroupPadX;
constexpr int kMixTop       = kEnv1Top;
constexpr int kMixBottom    = kEnv2Bottom;

/** The fader's own box, spanning both drums because that is what the
    fader spans. It is one control in a box, which is unusual - but it
    would otherwise be the only thing on the panel not in one, and the
    box is what says the fader belongs to both drums rather than to
    drum 2, which is the one it sits nearest. */
constexpr int kMixBoxW      = kMixWidth + 2 * kGroupPadX;
constexpr int kMixBoxTop    = kDrum1BoxY;
constexpr int kMixBoxBottom = kDrum2BoxY + kDrumBoxH;

/** THE PANEL'S WIDTH, asserted rather than trusted. Every piece of the
    right-hand end is positioned off the one before it, so this is the
    one place the chain has to come out where kEditorWidth says it does.
    Change a width and the build says so, rather than the panel quietly
    growing a margin or losing a fader off the edge. */
static_assert (kMixBoxX + kMixBoxW + kMargin == FilterDrumEditor::kEditorWidth,
               "the drum box and the fader box must fill the panel");
static_assert (kGroupX + kGroupW + kDrumPad <= kDrumBoxX + kDrumBoxW,
               "a group box must fit inside its drum box");
static_assert (kEnvX + kEnvWidth + kDrumPad <= kDrumBoxX + kDrumBoxW,
               "and so must the envelope display");

/** The panel's background. Darker than the controls' bar fill so the
    bars read as raised, which is what the DXi's Draw3dRect did. */
const CColor kPanelBack (44, 48, 44, 255);

} // anonymous namespace

//------------------------------------------------------------------------
FilterDrumEditor::FilterDrumEditor (FilterDrumController* controller)
: VSTGUIEditor (controller)
, mController (controller)
{
	ViewRect rect (0, 0, kEditorWidth, kEditorHeight);
	setRect (rect);
}

//------------------------------------------------------------------------
void FilterDrumEditor::addGroupBox (int x, int y, int w, int h,
                                    const char* title, bool drumBox)
{
	// ADDED BEFORE THE CONTROLS IT ENCLOSES, so it is behind them. It is
	// mouse-disabled and never fills, so it is a line on the background
	// and a click inside it reaches whatever is really there.
	frame->addView (new SpyGroupBox (
	    CRect (x, y, x + w, y + h),
	    title ? std::string (title) : std::string (),
	    drumBox ? Colours::kDrumFrame : Colours::kGroupFrame));
}

//------------------------------------------------------------------------
void FilterDrumEditor::addSectionLabel (const char* text, int y)
{
	CRect r (kContentX, y, kEditorWidth - kMargin, y + kLabelHeight);
	auto* label = new CTextLabel (r, text);
	label->setFont (panelFont ());
	label->setFontColor (Colours::kLabel);
	label->setBackColor (kPanelBack);
	label->setFrameColor (kPanelBack);
	label->setHoriAlign (kLeftText);
	label->setMouseEnabled (false);
	frame->addView (label);
}

//------------------------------------------------------------------------
bool PLUGIN_API FilterDrumEditor::open (void* parent, const PlatformType& platformType)
{
	if (frame)
		return false;

	CRect frameSize (0, 0, kEditorWidth, kEditorHeight);
	frame = new CFrame (frameSize, this);
	frame->setBackgroundColor (kPanelBack);
	frame->open (parent, platformType);

	// ---- title ---------------------------------------------------------
	{
		CRect r (kContentX, kTitleY, kEditorWidth - kMargin, kTitleY + kLabelHeight + 2);
		auto* title = new CTextLabel (r, "FilterDrum   -   two monophonic MS-20 drum voices, struck together");
		title->setFont (panelFont ());
		title->setFontColor (Colours::kValue);
		title->setBackColor (kPanelBack);
		title->setFrameColor (kPanelBack);
		title->setHoriAlign (kLeftText);
		title->setMouseEnabled (false);
		frame->addView (title);
	}

	// ---- the two drums -------------------------------------------------
	//
	// ONE FUNCTION, CALLED TWICE. The blocks are identical in shape
	// because the voices are identical in design - if they ever stop
	// matching on the panel, it is because addDrumBlock was special-cased
	// and that is worth having to do deliberately.
	addDrumBlock (1, kDrum1LabelY, kDrum1VcfY, kDrum1VcaY, kDrum1ShapeY);
	addDrumBlock (2, kDrum2LabelY, kDrum2VcfY, kDrum2VcaY, kDrum2ShapeY);

	// ---- the two envelope displays -------------------------------------
	//
	// One per drum, each level with the block it belongs to, each
	// carrying that drum's VCF curve in green and its VCA curve in red
	// on a SHARED time axis - see traceDrumEnvelopes(). They are
	// readouts: mouse-disabled, not in mControls, and refreshed from
	// parameters rather than driving any.
	{
		const int tops[2]    = { kEnv1Top,    kEnv2Top    };
		const int bottoms[2] = { kEnv1Bottom, kEnv2Bottom };

		for (int i = 0; i < 2; ++i)
		{
			CRect r (kEnvX, tops[i], kEnvX + kEnvWidth, bottoms[i]);
			mEnvViews[i] = new SpyEnvelopeView (r);
			frame->addView (mEnvViews[i]);
			refreshEnvelopeDisplay (i + 1);
		}
	}

	// ---- the crossfader ------------------------------------------------
	{
		addGroupBox (kMixBoxX, kMixBoxTop, kMixBoxW,
		             kMixBoxBottom - kMixBoxTop, "MIX");

		CRect r (kMixX, kMixTop, kMixX + kMixWidth, kMixBottom);

		auto* fader = new SpyFader (r, this, static_cast<int32_t> (kMix));

		// D1 / D2, not DRUM 1 / DRUM 2 - the section headings a few
		// pixels to the left already say which is which, and the full
		// words would set the fader's width rather than the other way
		// round.
		fader->setEndNames ("D1", "D2");
		fader->setFormatter ([this] (float) { return readoutFor (kMix); });
		registerControl (kMix, fader);
	}

	// ---- output --------------------------------------------------------
	addGroupBox (kMargin, kOutputBoxY, kOutputBoxW, kGroupHeight, "OUTPUT");
	addSlider (kOutputTrim, kTrimColumn, kTrimRowY);

	// ---- the sequencer, along the bottom -------------------------------
	addStepRow ();

	// ---- what velocity actually does -----------------------------------
	//
	// THE ONE THING A USER CANNOT GUESS, so it is printed rather than
	// left to a tooltip that macOS may never show. Computed through
	// velocityScaled(), the same function each voice latches its amounts
	// with, so the line cannot describe a law the audio does not follow.
	{
		CRect r (kReadoutX, kVelocityY, kEditorWidth - kMargin, kVelocityY + kLabelHeight);
		mVelocityLabel = new CTextLabel (r, "");
		mVelocityLabel->setFont (panelFontSmall ());
		mVelocityLabel->setFontColor (Colours::kTrace);
		mVelocityLabel->setBackColor (kPanelBack);
		mVelocityLabel->setFrameColor (kPanelBack);
		mVelocityLabel->setHoriAlign (kLeftText);
		mVelocityLabel->setMouseEnabled (false);
		frame->addView (mVelocityLabel);
	}

	// ---- the rate the DSP is really running at -------------------------
	{
		CRect r (kReadoutX, kRateY, kEditorWidth - kMargin, kRateY + kLabelHeight);
		mRateLabel = new CTextLabel (r, "");
		mRateLabel->setFont (panelFontSmall ());
		mRateLabel->setFontColor (Colours::kValue);
		mRateLabel->setBackColor (kPanelBack);
		mRateLabel->setFrameColor (kPanelBack);
		mRateLabel->setHoriAlign (kLeftText);
		mRateLabel->setMouseEnabled (false);
		frame->addView (mRateLabel);
	}

	refreshAllReadouts ();

	return true;
}

//------------------------------------------------------------------------
void FilterDrumEditor::addDrumBlock (int drum, int labelY, int vcfRowY,
                                     int vcaRowY, int shapeRowY)
{
	// THE ID FOR THIS DRUM, whichever block the parameter lives in.
	// There are two offsets now - eleven for the original block and four
	// for the shapes - and drumParam() is the only thing that knows
	// which is which. Adding an offset by hand here would compile and
	// would wire drum 2's shapes to four step switches.
	auto p = [drum] (ParamID base) { return drumParam (base, drum); };

	// THE OUTER BOX FIRST, then the three group boxes, then the
	// controls - back to front, because a SpyGroupBox is a line and
	// whatever is added later draws over it.
	const int boxY = (drum == 1) ? kDrum1BoxY : kDrum2BoxY;
	addGroupBox (kDrumBoxX, boxY, kDrumBoxW, kDrumBoxH,
	             (drum == 1) ? "DRUM 1" : "DRUM 2", true);

	addGroupBox (kGroupX, vcfRowY   - kGroupPadTop, kGroupW, kGroupHeight, "VCF");
	addGroupBox (kGroupX, vcaRowY   - kGroupPadTop, kGroupW, kGroupHeight, "VCA");
	addGroupBox (kGroupX, shapeRowY - kGroupPadTop, kGroupW, kGroupHeight, "ENVELOPE SHAPE");

	// THE BOX SAYS WHICH DRUM, so the heading inside it does not have to
	// and can spend its width on what the drum actually is.
	addSectionLabel ((drum == 1)
	                   ? "noise -> MS-20 lowpass -> VCA      (lamp = self-oscillating)"
	                   : "the same voice again, with its own settings",
	                 labelY);

	// NOISE LEVEL FIRST, because it is what feeds the filter and the row
	// then reads left to right in signal order. Its parameter id is the
	// last of the drum's eleven - it was appended - so this is where
	// panel order and id order deliberately disagree.
	addSlider (p (kNoiseLevel),  0, vcfRowY);
	addSlider (p (kCutoff),      1, vcfRowY);
	addSlider (p (kResonance),   2, vcfRowY);
	addSlider (p (kVcfAttack),   3, vcfRowY);
	addSlider (p (kVcfRelease),  4, vcfRowY);
	addSlider (p (kVcfAmount),   5, vcfRowY);
	addSlider (p (kVcfVelocity), 6, vcfRowY);

	addSlider (p (kVcaAttack),   0, vcaRowY);
	addSlider (p (kVcaRelease),  1, vcaRowY);
	addSlider (p (kVcaAmount),   2, vcaRowY);
	addSlider (p (kVcaVelocity), 3, vcaRowY);

	// THE SHAPE ROW, in the same column order as the times they bend:
	// VCF Attack and VCF Release are columns 3 and 4 of the row above,
	// and their shapes are columns 0 and 1 of this one. Not aligned
	// underneath - that would have put the four shapes in columns 3, 4,
	// 0 and 1 and made the row read as two pairs with a hole - but in
	// the same ORDER, so "the second shape is the VCF release's" holds
	// without reading the labels.
	addSlider (p (kVcfAttackShape),  0, shapeRowY);
	addSlider (p (kVcfReleaseShape), 1, shapeRowY);
	addSlider (p (kVcaAttackShape),  2, shapeRowY);
	addSlider (p (kVcaReleaseShape), 3, shapeRowY);

	// THE LEGEND, in the columns this row does not use.
	//
	// The box on this row is now titled ENVELOPE SHAPE, so the legend no
	// longer has to say what the row is - only what the travel is, which
	// is the part a title cannot carry.
	{
		const int x = kContentX + 4 * kColumnPitch;
		const int y = shapeRowY + kSliderHeight - kLabelHeight - 1;
		auto* legend = new CTextLabel (
		    CRect (x, y, kEditorWidth - kMargin, y + kLabelHeight),
		    "Exp  ->  Lin  ->  Log");
		legend->setFont (panelFontSmall ());
		legend->setFontColor (Colours::kLabel);
		legend->setBackColor (kPanelBack);
		legend->setFrameColor (kPanelBack);
		legend->setHoriAlign (kLeftText);
		legend->setMouseEnabled (false);
		frame->addView (legend);
	}

	// THE LAMP, on this drum's resonance knob. setUseIndicator is the
	// DXi SlideSpin's own 10 x 10 corner lamp and this is exactly what it
	// was for. It lights from selfOscillating() - the same predicate the
	// filter's threshold is written in - so it cannot claim the tone is
	// on when it is not.
	if (auto* res = mControls[p (kResonance)])
		if (auto* slider = dynamic_cast<SpySlider*> (res))
			slider->setUseIndicator (true);
}

//------------------------------------------------------------------------
void PLUGIN_API FilterDrumEditor::close ()
{
	// The map holds RAW pointers to views the FRAME owns. Clearing it
	// before forgetting the frame, and never dereferencing it after, is
	// what keeps that safe - see the note in
	// FilterDrumController::editorDestroyed for the other half.
	mControls.clear ();
	mEnvViews[0] = nullptr;
	mEnvViews[1] = nullptr;
	mVelocityLabel = nullptr;
	mMixLabel = nullptr;
	mRateLabel = nullptr;

	if (frame)
	{
		frame->forget ();
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
std::string FilterDrumEditor::shortLabelFor (ParamID tag)
{
	// BOTH DRUMS' LABELS COME FROM DRUM 1'S TITLE, so "Release" under
	// drum 2 is guaranteed to be the same word as the one under drum 1.
	// The section heading above each row is what says which drum it is,
	// and repeating "2" on eleven controls would cost the characters
	// that make "Velocity" legible on a 94-pixel slider.
	int drum = 0;
	ParamID base = tag;
	splitDrumParam (tag, drum, base);

	std::string label = paramDef (base).title;

	// Strip the section prefix: the row's own heading already says VCF
	// or VCA, and the panel puts the two rows one above the other.
	if (label.rfind ("VCF ", 0) == 0 || label.rfind ("VCA ", 0) == 0)
		label.erase (0, 4);

	// THE SHAPE LABELS KEEP THEIR STAGE. "VCF Atk Shape" reduces to
	// "Atk Shape" by the rule above, and the four would then read
	// Atk / Rel / Atk / Rel - ambiguous between the two envelopes, which
	// the row heading cannot disambiguate because it says SHAPE. So the
	// shapes are the one place the prefix goes back on, short enough for
	// a 94-pixel slider: "VCF Atk", "VCA Rel".
	if (isShapeParam (base))
	{
		const bool vcf = (base == kVcfAttackShape || base == kVcfReleaseShape);
		const bool atk = (base == kVcfAttackShape || base == kVcaAttackShape);
		label = std::string (vcf ? "VCF " : "VCA ") + (atk ? "Atk" : "Rel");
	}

	return label;
}

//------------------------------------------------------------------------
void FilterDrumEditor::addSlider (ParamID tag, int column, int y)
{
	const int x = kContentX + column * kColumnPitch;
	CRect r (x, y, x + kColumnWidth, y + kSliderHeight);

	auto* slider = new SpySlider (r, this, static_cast<int32_t> (tag));
	slider->setLabel (shortLabelFor (tag));

	// THE PANEL BORROWS THE PARAMETER'S OWN FORMATTING rather than
	// writing its own, so the panel and the host's generic editor cannot
	// disagree about what a position means - which they will, the first
	// time a range changes and only one of the two copies is updated.
	slider->setFormatter ([this, tag] (float /*normalized*/) {
		return readoutFor (tag);
	});

	registerControl (tag, slider);
}

//------------------------------------------------------------------------
void FilterDrumEditor::addStepRow ()
{
	// The box carries the name; the line above it carries what the row
	// does, which is the part a four-word title cannot.
	addSectionLabel ("16 steps = one bar of 1/16ths   "
	                 "(lamp = playhead; MIDI still triggers)", kSeqLabelY);

	addGroupBox (kMargin, kSeqRowY - kGroupPadTop,
	             kSeqBoxRight - kMargin,
	             kStepHeight + kGroupPadTop + kGroupPadBottom,
	             "SEQUENCER");

	for (int i = 0; i < 16; ++i)
	{
		const int x = kContentX + i * kStepPitch;
		CRect r (x, kSeqRowY, x + kStepWidth, kSeqRowY + kStepHeight);

		auto* sw = new SpyStepSwitch (r, this, static_cast<int32_t> (kStep1 + i));

		// The label is the step NUMBER: on a 30-pixel control the number
		// is what tells you which step you are looking at, and the bar
		// already shows the state.
		char name[8] = {};
		std::snprintf (name, sizeof (name), "%d", i + 1);
		sw->setLabel (name);

		// THE LAMP IS THE PLAYHEAD, not the switch's own state - that is
		// the answer to "a led to indicate it's on". The bar shows
		// whether the step is enabled; the lamp shows the sequencer
		// arriving at it, so the row reads as a running sequencer rather
		// than sixteen static settings. SpyStepSwitch centres it over
		// the switch and boxes the pair; SpySlider's corner lamp sat ten
		// pixels left of its own number and the row looked misaligned.
		sw->setIndicator (false);

		registerControl (kStep1 + i, sw);
	}

	// ---- Run -----------------------------------------------------------
	{
		CRect r (kSeqCtrlX, kSeqRowY, kSeqCtrlX + kSeqCtrlW, kSeqRowY + kStepHeight);
		// THE SAME CELL AS A STEP, so the sequencer row reads as one row
		// of boxed switches rather than sixteen of one kind and one of
		// another. It has a reading where a step has a bar - see the
		// rule in SpyStepSwitch::draw.
		auto* run = new SpyStepSwitch (r, this, static_cast<int32_t> (kSeqRun));
		run->setLabel ("Run");
		run->setFormatter ([this] (float) { return readoutFor (kSeqRun); });

		// Its lamp is the ARM light: Run goes on instantly but the
		// pattern waits for the launch line, and a switch that lights
		// while nothing happens for most of a bar looks broken. Lit
		// means armed and waiting; the step lamps moving mean playing.
		registerControl (kSeqRun, run);
	}

	// ---- Launch On -----------------------------------------------------
	{
		const int x = kSeqCtrlX + kSeqCtrlW + kSeqCtrlGap;
		CRect r (x, kSeqRowY, x + kSeqCtrlW, kSeqRowY + kStepHeight);

		auto* sel = new SpySelector (r, this, static_cast<int32_t> (kSeqDivision));

		std::vector<std::string> names;
		for (int i = 0; i < kLaunchDivisionCount; ++i)
			names.push_back (divisionShortName (divisionFromIndex (i)));
		sel->setNames (names);
		sel->setLabel ("Launch On");

		registerControl (kSeqDivision, sel);
	}
}

//------------------------------------------------------------------------
void FilterDrumEditor::setPlayhead (int step)
{
	if (step == mPlayhead)
		return;

	// ONLY THE TWO THAT CHANGE are repainted. Invalidating all sixteen on
	// every step would be sixteen redraws a sixteenth note, which at 120
	// bpm is a hundred and twenty-eight a second for two lamps.
	auto light = [this] (int index, bool on) {
		if (index < 0 || index >= 16)
			return;
		auto it = mControls.find (kStep1 + index);
		if (it == mControls.end () || it->second == nullptr)
			return;
		// SpySlider, not SpyStepSwitch: setIndicator is the base's, and
		// casting to the base keeps this working if a step ever uses a
		// different control from the family.
		if (auto* sw = dynamic_cast<SpySlider*> (it->second))
		{
			sw->setIndicator (on);
			sw->invalid ();
		}
	};

	light (mPlayhead, false);
	light (step, true);

	mPlayhead = step;
}

//------------------------------------------------------------------------
void FilterDrumEditor::registerControl (ParamID tag, CControl* control)
{
	mControls[tag] = control;
	if (mController)
		control->setValueNormalized (static_cast<float> (mController->getParamNormalized (tag)));
	frame->addView (control);
}

//------------------------------------------------------------------------
double FilterDrumEditor::normalizedOf (ParamID tag) const
{
	auto it = mControls.find (tag);
	if (it != mControls.end () && it->second != nullptr)
		return it->second->getValueNormalized ();

	if (mController)
		return mController->getParamNormalized (tag);

	return paramDef (tag).defaultNormalized ();
}

//------------------------------------------------------------------------
void FilterDrumEditor::valueChanged (CControl* control)
{
	if (!control || !mController || mUpdating)
		return;

	const ParamID    tag   = static_cast<ParamID> (control->getTag ());
	const ParamValue value = control->getValueNormalized ();

	// BOTH CALLS, in this order. setParamNormalized updates the
	// controller's own copy so the panel and the host agree immediately;
	// performEdit is what tells the HOST, and is what makes the move
	// automatable and undoable. One without the other is a control that
	// works until you try to record it.
	mController->setParamNormalized (tag, value);
	mController->performEdit (tag, value);

	refreshReadout (tag);

	// The four knobs the bottom line is computed from, and the lamp's
	// own knob. Cheaper to name them than to redraw the whole panel on
	// every mouse move.
	// The eight knobs the bottom line is computed from - four per drum.
	int d = 0; ParamID b = tag;
	splitDrumParam (tag, d, b);
	if (b == kVcfAmount || b == kVcfVelocity || b == kVcaAmount || b == kVcaVelocity)
		refreshAllReadouts ();

	// THE TEN THAT SHAPE A CURVE. Named rather than redrawing both
	// displays on every parameter, because this runs on every pixel of
	// every drag and a trace is two envelopes run end to end.
	if (b == kVcfAttack || b == kVcfRelease || b == kVcfAmount ||
	    b == kVcaAttack || b == kVcaRelease || b == kVcaAmount ||
	    b == kVcfAttackShape || b == kVcfReleaseShape ||
	    b == kVcaAttackShape || b == kVcaReleaseShape)
		refreshEnvelopeDisplay (d);

	if (tag == kResonance || tag == kResonance2)
		refreshResonanceLamp (tag);
}

//------------------------------------------------------------------------
void FilterDrumEditor::updateControl (ParamID tag, ParamValue normalized)
{
	auto it = mControls.find (tag);
	if (it != mControls.end () && it->second != nullptr)
	{
		mUpdating = true;
		it->second->setValueNormalized (static_cast<float> (normalized));
		it->second->invalid ();
		mUpdating = false;
	}

	refreshReadout (tag);

	// The eight knobs the bottom line is computed from - four per drum.
	int d = 0; ParamID b = tag;
	splitDrumParam (tag, d, b);
	if (b == kVcfAmount || b == kVcfVelocity || b == kVcaAmount || b == kVcaVelocity)
		refreshAllReadouts ();

	// THE TEN THAT SHAPE A CURVE. Named rather than redrawing both
	// displays on every parameter, because this runs on every pixel of
	// every drag and a trace is two envelopes run end to end.
	if (b == kVcfAttack || b == kVcfRelease || b == kVcfAmount ||
	    b == kVcaAttack || b == kVcaRelease || b == kVcaAmount ||
	    b == kVcfAttackShape || b == kVcfReleaseShape ||
	    b == kVcaAttackShape || b == kVcaReleaseShape)
		refreshEnvelopeDisplay (d);

	if (tag == kResonance || tag == kResonance2)
		refreshResonanceLamp (tag);
}

//------------------------------------------------------------------------
std::string FilterDrumEditor::readoutFor (ParamID tag) const
{
	if (!isTableParam (tag))
		return std::string ();

	const ParamDef& def = paramDef (tag);
	const double normalized = normalizedOf (tag);
	const double plain = def.toPlain (normalized);

	char text[64] = {};

	// SWITCHED ON THE BASE ID, so drum 2's readouts are drum 1's
	// formatting rather than a second copy of it.
	int drum = 0;
	ParamID base = tag;
	splitDrumParam (tag, drum, base);

	switch (base)
	{
		case kCutoff:
			// Hz below 1 k, kHz above - the same rule the host's own
			// generic editor uses, because it is the same code path
			// in FilterDrumController.cpp's toString. If these two
			// ever disagree, one of them was edited alone.
			if (plain >= 1000.0)
				std::snprintf (text, sizeof (text), "%.2fk", plain / 1000.0);
			else
				std::snprintf (text, sizeof (text), "%.0f Hz", plain);
			break;

		case kResonance:
			// The RESONANCE KNOB READS IN PER CENT AND IN K, because
			// the per cent is meaningless and K is the number the
			// MS-20 literature quotes - self-oscillation at 2.
			std::snprintf (text, sizeof (text), "%.0f%%  K%.2f",
			               plain, def.toInternal (normalized));
			break;

		case kVcfAmount:
			// Per cent on the knob, OCTAVES underneath, because octaves
			// is what it does. Signed: the negative half sweeps the
			// other way.
			std::snprintf (text, sizeof (text), "%+.0f%% %+.1foct",
			               plain, def.toInternal (normalized));
			break;

		case kOutputTrim:
			std::snprintf (text, sizeof (text), "%+.1f dB", plain);
			break;

		case kVcfAttackShape:
		case kVcfReleaseShape:
		case kVcaAttackShape:
		case kVcaReleaseShape:
			// THE WORD, NOT THE NUMBER, because "-73 %" does not say
			// which way the curve bends and "Exp 73" does. The ends are
			// named on their own - "Exp" with no figure is the far end
			// of the travel, which is the setting worth being able to
			// find again by eye - and the centre is just "Lin".
			if (plain <= -99.5)
				std::snprintf (text, sizeof (text), "Exp");
			else if (plain >= 99.5)
				std::snprintf (text, sizeof (text), "Log");
			else if (plain < -0.5)
				std::snprintf (text, sizeof (text), "Exp %.0f", -plain);
			else if (plain > 0.5)
				std::snprintf (text, sizeof (text), "Log %.0f", plain);
			else
				std::snprintf (text, sizeof (text), "Lin");
			break;

		case kSeqRun:
			// The three states a Run switch can be in, and the middle one
			// is the one worth showing: armed means the pattern is
			// waiting for its launch line, which can be most of a bar
			// away, and a switch that just says "on" makes that look
			// like a fault.
			std::snprintf (text, sizeof (text), "%s",
			               (normalized < 0.5) ? "off"
			                                  : ((mPlayhead >= 0) ? "running" : "armed"));
			break;

		case kSeqDivision:
			std::snprintf (text, sizeof (text), "%s",
			               divisionShortName (divisionFromIndex (
			                   static_cast<int> (def.toInternal (normalized) + 0.5))));
			break;

		case kMix:
			// THE TWO GAINS, not the knob's per cent, because per cent
			// of a crossfader means nothing on its own. They come from
			// crossfadeGainDrum1/2() - the same calls the audio path
			// makes - so the numbers under the fader are the gains that
			// are actually applied.
			if (plain >= 99.5)
				std::snprintf (text, sizeof (text), "D1 only");
			else if (plain <= 0.5)
				std::snprintf (text, sizeof (text), "D2 only");
			else
				// Leading zeros dropped: a gain is always under 1 here,
				// so ".71/.71" says as much as "0.71 / 0.71" in half
				// the width - which is the width the fader now has.
				std::snprintf (text, sizeof (text), "%.2f/%.2f",
				               crossfadeGainDrum1 (def.toInternal (normalized)) - 0.0,
				               crossfadeGainDrum2 (def.toInternal (normalized)) - 0.0);
			break;

		case kNoiseLevel:
			// AT ZERO IT SAYS SO IN WORDS, because "0 %" understates
			// it. The noise is the voice's only excitation, so at 0
			// there is nothing for the filter to ring and the plug-in
			// is silent however hard it is played and however high the
			// resonance is set. Somebody who turns this down and hears
			// nothing should be able to tell from the panel that it is
			// the knob and not a broken plug-in.
			if (plain <= 0.0)
				std::snprintf (text, sizeof (text), "silent");
			else
				std::snprintf (text, sizeof (text), "%.0f %%", plain);
			break;

		default:
			if (def.type == ParamType::Log && std::strcmp (def.units, "ms") == 0)
			{
				if (plain < 10.0)
					std::snprintf (text, sizeof (text), "%.2f ms", plain);
				else if (plain < 100.0)
					std::snprintf (text, sizeof (text), "%.1f ms", plain);
				else
					std::snprintf (text, sizeof (text), "%.0f ms", plain);
			}
			else
			{
				std::snprintf (text, sizeof (text), "%.0f %s", plain, def.units);
			}
			break;
	}

	return std::string (text);
}

//------------------------------------------------------------------------
std::string FilterDrumEditor::velocityLine () const
{
	// velocityScaled() IS THE SHARED LAW - the same call each voice
	// latches its amounts with. Velocity arrives from VST3 already
	// normalised, so 1.0 is MIDI 127.
	//
	// BOTH DRUMS ON ONE LINE, because the point of showing it is that
	// the two can be set to respond differently: a kick that ignores
	// velocity under a snap that does not is a real patch, and the panel
	// should make that visible without playing it.
	char text[240] = {};
	int written = 0;

	for (int drum = 1; drum <= 2; ++drum)
	{
		// All four of these are in the ORIGINAL per-drum block, so a
		// hand-added kDrum2Offset would in fact be correct here. It goes
		// through drumParam() anyway: `+ off` is the pattern that breaks
		// the moment it is copied onto a shape id, and leaving one
		// correct example of it in the file is how that copy happens.
		auto internal = [this, drum] (ParamID base) {
			const ParamID id = drumParam (base, drum);
			return paramDef (id).toInternal (normalizedOf (id));
		};

		const double octaves = internal (kVcfAmount);
		const double vcfSens = internal (kVcfVelocity);
		const double gain    = internal (kVcaAmount);
		const double vcaSens = internal (kVcaVelocity);

		written += std::snprintf (text + written,
		                          (written < static_cast<int> (sizeof (text)))
		                            ? sizeof (text) - written : 0,
		                          "%sD%d  v127: %+.2foct %.0f%%   v64: %+.2foct %.0f%%   v0: %+.2foct %.0f%%",
		                          (drum == 2) ? "      " : "", drum,
		                          velocityScaled (octaves, vcfSens, 1.0),
		                          velocityScaled (gain, vcaSens, 1.0) * 100.0,
		                          velocityScaled (octaves, vcfSens, 64.0 / 127.0),
		                          velocityScaled (gain, vcaSens, 64.0 / 127.0) * 100.0,
		                          velocityScaled (octaves, vcfSens, 0.0),
		                          velocityScaled (gain, vcaSens, 0.0) * 100.0);

		if (written >= static_cast<int> (sizeof (text)))
			break;
	}

	return std::string (text);
}

//------------------------------------------------------------------------
// The envelope displays
//
// Built from PARAMETERS, not from the DSP: the editor cannot see a
// DrumVoice - it lives in the processor, which in a host like Logic is
// not even in the same process. What keeps the picture honest instead is
// that traceDrumEnvelopes() drives real AREnvelope objects, so the only
// thing that could drift is the settings handed to them, and those come
// through toInternal() - the same conversion the processor applies to
// the same normalised values.
//
// DRAWN AT FULL VELOCITY, because a display cannot know how hard the
// next hit will be played. The velocity line under the panel is what
// covers the rest, and it is computed from velocityScaled().
//------------------------------------------------------------------------
void FilterDrumEditor::refreshEnvelopeDisplay (int drum)
{
	if (drum < 1 || drum > 2)
		return;

	SpyEnvelopeView* view = mEnvViews[drum - 1];
	if (!view)
		return;

	// drumParam(), not an offset added by hand: the shapes are a second
	// per-drum block with an offset of four, and `base + kDrum2Offset`
	// on one of those would quietly name a step switch.
	auto internal = [this, drum] (ParamID base) {
		const ParamID id = drumParam (base, drum);
		return paramDef (id).toInternal (normalizedOf (id));
	};

	// Times come back in SECONDS - the table's internal units - which is
	// what ArSpec wants. The milliseconds are a panel unit only. Shapes
	// come back as -1 Exponential .. +1 Logarithmic.
	ArSpec vcf;
	vcf.attack       = internal (kVcfAttack);
	vcf.release      = internal (kVcfRelease);
	vcf.attackShape  = internal (kVcfAttackShape);
	vcf.releaseShape = internal (kVcfReleaseShape);

	ArSpec vca;
	vca.attack       = internal (kVcaAttack);
	vca.release      = internal (kVcaRelease);
	vca.attackShape  = internal (kVcaAttackShape);
	vca.releaseShape = internal (kVcaReleaseShape);

	float vcfCurve[kEnvPoints];
	float vcaCurve[kEnvPoints];
	const double span = traceDrumEnvelopes (vcf, vca, vcfCurve, vcaCurve, kEnvPoints);

	char caption[24] = {};
	std::snprintf (caption, sizeof (caption), "DRUM %d ENV", drum);

	// The span in the units the knobs are in, so the figure here and the
	// figures under the Attack and Release controls are comparable
	// without arithmetic.
	char annotation[24] = {};
	if (span >= 1.0)
		std::snprintf (annotation, sizeof (annotation), "%.2f s", span);
	else
		std::snprintf (annotation, sizeof (annotation), "%.0f ms", span * 1000.0);

	// THE AMOUNTS, as marker-line heights: 0..1 of the plot.
	//
	// The VCA's is already a linear gain. The VCF's is SIGNED octaves
	// and its MAGNITUDE sets the marker - a sweep of three octaves
	// downwards is as deep as three octaves up - so the direction has
	// nowhere to go but the legend.
	const double octaves = internal (kVcfAmount);
	const double gain    = internal (kVcaAmount);

	view->setAmounts (std::fabs (octaves) / kMaxEnvOctaves, gain);

	// THE LEGEND CARRIES THE FIGURES, and the VCF's carries its sign
	// with them. "VCF -3.6oct" says the sweep is three and a half
	// octaves deep and runs downwards - the attack CLOSES the filter and
	// the release opens it back up. That replaced a bare "inv" flag,
	// which said the direction and not the depth while the depth was
	// being read off a marker two inches above it.
	char vcfLegend[24] = {};
	std::snprintf (vcfLegend, sizeof (vcfLegend), "VCF %+.1foct", octaves);

	char vcaLegend[24] = {};
	std::snprintf (vcaLegend, sizeof (vcaLegend), "VCA %.0f%%", gain * 100.0);

	view->setCaption (caption);
	view->setAnnotation (annotation);
	view->setLegend (vcfLegend, vcaLegend);
	view->setCurves (vcfCurve, vcaCurve, kEnvPoints);
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshResonanceLamp (ParamID tag)
{
	auto it = mControls.find (tag);
	if (it == mControls.end () || it->second == nullptr)
		return;

	auto* slider = dynamic_cast<SpySlider*> (it->second);
	if (slider == nullptr)
		return;

	// selfOscillating() ON THE INTERNAL VALUE - K, not per cent. The
	// threshold is a property of the filter, so it is stated once, in
	// FilterDrumDsp.h, and both lamps and both filters read it there.
	const double k = paramDef (tag).toInternal (normalizedOf (tag));
	slider->setIndicator (selfOscillating (k));
	slider->invalid ();
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshReadout (ParamID tag)
{
	auto it = mControls.find (tag);
	if (it != mControls.end () && it->second != nullptr)
		it->second->invalid ();
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshAllReadouts ()
{
	for (auto& entry : mControls)
		if (entry.second != nullptr)
			entry.second->invalid ();

	refreshResonanceLamp (kResonance);
	refreshResonanceLamp (kResonance2);

	if (mVelocityLabel != nullptr)
	{
		mVelocityLabel->setText (velocityLine ().c_str ());
		mVelocityLabel->invalid ();
	}

	if (mRateLabel != nullptr && mController != nullptr)
	{
		const double rate = mController->getHostSampleRate ();

		// The cutoff CEILING is a fraction of the sample rate, from
		// kMaxCutoffFraction, so it is worth showing next to the rate:
		// at 44.1 k the Cutoff knob's top 20 kHz is not reachable, and
		// that is a question people ask rather than a bug.
		char text[96] = {};
		std::snprintf (text, sizeof (text), "Engine: %.0f Hz    cutoff ceiling %.0f Hz",
		               rate, rate * kMaxCutoffFraction);
		mRateLabel->setText (text);
		mRateLabel->invalid ();
	}
}

//------------------------------------------------------------------------
} // namespace FilterDrum
