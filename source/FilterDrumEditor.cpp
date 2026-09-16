//------------------------------------------------------------------------
// FilterDrum - editor implementation
//------------------------------------------------------------------------

#include "FilterDrumEditor.h"
#include "FilterDrumController.h"
#include "FilterDrumDsp.h"

#include <cstdio>
#include <cstring>

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

    kEditorWidth is the seven columns plus the crossfader:
    2*kMargin + 7*kColumnWidth + 7*kColumnGap + kMixWidth = 798. The
    fader is NOT a column - see kMixWidth - so adding or removing one
    moves that number by kColumnPitch and the fader with it. */
constexpr int kMargin       = 16;
constexpr int kColumnWidth  = 94;
constexpr int kColumnGap    = 8;
constexpr int kColumnPitch  = kColumnWidth + kColumnGap;
constexpr int kSliderHeight = 44;

constexpr int kTitleY       = 10;
constexpr int kLabelHeight  = 16;

/** The two drum blocks. Each is a section heading, a seven-column VCF
    row and a four-column VCA row; drum 2's is the same shape 132 pixels
    further down. */
constexpr int kDrum1LabelY  = 38;
constexpr int kDrum1VcfY    = 58;
constexpr int kDrum1VcaY    = 110;

constexpr int kDrum2LabelY  = 170;
constexpr int kDrum2VcfY    = 190;
constexpr int kDrum2VcaY    = 242;

constexpr int kVelocityY    = 300;
constexpr int kRateY        = 322;

/** The crossfader, to the right of both drum blocks.

    IT IS NOT ON THE COLUMN GRID. It was, at a full 94-pixel column, and
    it looked wrong - a fader given a slider's width reads as a slider
    that grew rather than as a different kind of control. A fader is
    narrow; only its HEIGHT is meant to be large, and that height is what
    makes it read as the thing the two drums meet in.

    So it gets its own width, and the panel is only as wide as the seven
    columns plus this. */
constexpr int kMixWidth     = 52;
constexpr int kMixX         = kMargin + 7 * kColumnPitch;
constexpr int kMixTop       = kDrum1VcfY;
constexpr int kMixBottom    = kDrum2VcaY + kSliderHeight;

/** The output trim sits in drum 2's VCA row, two columns clear of the
    envelope controls. That gap is the only thing on the panel saying the
    trim is not part of the VCA, and it is cheaper than a box or a rule. */
constexpr int kTrimColumn   = 5;

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
void FilterDrumEditor::addSectionLabel (const char* text, int y)
{
	CRect r (kMargin, y, kEditorWidth - kMargin, y + kLabelHeight);
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
		CRect r (kMargin, kTitleY, kEditorWidth - kMargin, kTitleY + kLabelHeight + 2);
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
	addDrumBlock (1, kDrum1LabelY, kDrum1VcfY, kDrum1VcaY);
	addDrumBlock (2, kDrum2LabelY, kDrum2VcfY, kDrum2VcaY);

	// ---- the crossfader ------------------------------------------------
	{
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
	addSlider (kOutputTrim, kTrimColumn, kDrum2VcaY);

	// ---- what velocity actually does -----------------------------------
	//
	// THE ONE THING A USER CANNOT GUESS, so it is printed rather than
	// left to a tooltip that macOS may never show. Computed through
	// velocityScaled(), the same function each voice latches its amounts
	// with, so the line cannot describe a law the audio does not follow.
	{
		CRect r (kMargin, kVelocityY, kEditorWidth - kMargin, kVelocityY + kLabelHeight);
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
		CRect r (kMargin, kRateY, kEditorWidth - kMargin, kRateY + kLabelHeight);
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
void FilterDrumEditor::addDrumBlock (int drum, int labelY, int vcfRowY, int vcaRowY)
{
	// The offset that turns drum 1's ids into drum 2's. One constant,
	// defined in FilterDrumParams.h next to the table it describes.
	const ParamID off = (drum == 2) ? kDrum2Offset : 0;

	addSectionLabel ((drum == 1)
	                   ? "DRUM 1   noise -> MS-20 lowpass -> VCA   (lamp = self-oscillating)"
	                   : "DRUM 2   same voice, its own settings",
	                 labelY);

	// NOISE LEVEL FIRST, because it is what feeds the filter and the row
	// then reads left to right in signal order. Its parameter id is the
	// last of the drum's eleven - it was appended - so this is where
	// panel order and id order deliberately disagree.
	addSlider (kNoiseLevel  + off, 0, vcfRowY);
	addSlider (kCutoff      + off, 1, vcfRowY);
	addSlider (kResonance   + off, 2, vcfRowY);
	addSlider (kVcfAttack   + off, 3, vcfRowY);
	addSlider (kVcfRelease  + off, 4, vcfRowY);
	addSlider (kVcfAmount   + off, 5, vcfRowY);
	addSlider (kVcfVelocity + off, 6, vcfRowY);

	addSlider (kVcaAttack   + off, 0, vcaRowY);
	addSlider (kVcaRelease  + off, 1, vcaRowY);
	addSlider (kVcaAmount   + off, 2, vcaRowY);
	addSlider (kVcaVelocity + off, 3, vcaRowY);

	// THE LAMP, on this drum's resonance knob. setUseIndicator is the
	// DXi SlideSpin's own 10 x 10 corner lamp and this is exactly what it
	// was for. It lights from selfOscillating() - the same predicate the
	// filter's threshold is written in - so it cannot claim the tone is
	// on when it is not.
	if (auto* res = mControls[kResonance + off])
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

	return label;
}

//------------------------------------------------------------------------
void FilterDrumEditor::addSlider (ParamID tag, int column, int y)
{
	const int x = kMargin + column * kColumnPitch;
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
		const ParamID off = (drum == 2) ? kDrum2Offset : 0;

		const double octaves = paramDef (kVcfAmount   + off).toInternal (normalizedOf (kVcfAmount   + off));
		const double vcfSens = paramDef (kVcfVelocity + off).toInternal (normalizedOf (kVcfVelocity + off));
		const double gain    = paramDef (kVcaAmount   + off).toInternal (normalizedOf (kVcaAmount   + off));
		const double vcaSens = paramDef (kVcaVelocity + off).toInternal (normalizedOf (kVcaVelocity + off));

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
