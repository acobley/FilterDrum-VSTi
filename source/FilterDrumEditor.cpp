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
    kEditorWidth is 2*kMargin + 7*kColumnWidth + 6*kColumnGap = 738; if
    a column is ever added or removed, that number moves with it. */
constexpr int kMargin       = 16;
constexpr int kColumnWidth  = 94;
constexpr int kColumnGap    = 8;
constexpr int kColumnPitch  = kColumnWidth + kColumnGap;
constexpr int kSliderHeight = 44;

constexpr int kTitleY       = 10;
constexpr int kVcfLabelY    = 38;
constexpr int kVcfRowY      = 56;
constexpr int kVcaLabelY    = 116;
constexpr int kVcaRowY      = 134;
constexpr int kVelocityY    = 196;
constexpr int kRateY        = 218;

constexpr int kLabelHeight  = 16;

/** Which column each control sits in.
 
    The VCA row uses columns 0-3 and puts the output trim in the LAST
    one, leaving two empty between them, so the trim reads as a separate
    output stage rather than as a fifth VCA control. That gap is the
    only thing on the panel telling you the trim is not part of the
    envelope, and it is cheaper than a box or a rule. */
constexpr int kTrimColumn = 6;

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
		auto* title = new CTextLabel (r, "FilterDrum   -   monophonic MS-20 drum voice");
		title->setFont (panelFont ());
		title->setFontColor (Colours::kValue);
		title->setBackColor (kPanelBack);
		title->setFrameColor (kPanelBack);
		title->setHoriAlign (kLeftText);
		title->setMouseEnabled (false);
		frame->addView (title);
	}

	// ---- VCF -----------------------------------------------------------
	addSectionLabel ("VCF   noise + trigger -> MS-20 lowpass   (lamp = self-oscillating)", kVcfLabelY);

	// NOISE LEVEL FIRST, because it is what feeds the filter and the
	// row then reads left to right in signal order. Its parameter id is
	// the LAST in the table - it was appended, since inserting it would
	// have renumbered everything after it - so this is the one place
	// where panel order and id order deliberately disagree.
	addSlider (kNoiseLevel,   0, kVcfRowY);
	addSlider (kCutoff,       1, kVcfRowY);
	addSlider (kResonance,    2, kVcfRowY);
	addSlider (kVcfAttack,    3, kVcfRowY);
	addSlider (kVcfRelease,   4, kVcfRowY);
	addSlider (kVcfAmount,    5, kVcfRowY);
	addSlider (kVcfVelocity,  6, kVcfRowY);

	// THE LAMP. setUseIndicator is the DXi SlideSpin's own 10 x 10
	// corner lamp, and this is exactly what it was for. It lights from
	// selfOscillating() - the same predicate the filter's threshold is
	// written in - so the panel cannot claim the ping is on when it is
	// not.
	if (auto* res = mControls[kResonance])
		if (auto* slider = dynamic_cast<SpySlider*> (res))
			slider->setUseIndicator (true);

	// ---- VCA -----------------------------------------------------------
	addSectionLabel ("VCA", kVcaLabelY);
	addSlider (kVcaAttack,    0, kVcaRowY);
	addSlider (kVcaRelease,   1, kVcaRowY);
	addSlider (kVcaAmount,    2, kVcaRowY);
	addSlider (kVcaVelocity,  3, kVcaRowY);

	// ---- output --------------------------------------------------------
	// Column 5, with column 4 left empty - see kTrimColumn.
	addSlider (kOutputTrim, kTrimColumn, kVcaRowY);

	// ---- what velocity actually does -----------------------------------
	//
	// THE ONE THING A USER CANNOT GUESS, so it is printed rather than
	// left to a tooltip that macOS may never show. It is computed
	// through velocityScaled(), the same function the DSP latches its
	// amounts with, so the line cannot describe a law the audio does
	// not follow.
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
	//
	// A readout with no parameter behind it, and worth having because it
	// is the only thing on the panel that can ONLY have arrived by
	// message, from setActive, on the UI thread. If it ever reads 44100
	// in a 96 k session, the message route is broken - and the cutoff
	// ceiling, which is a fraction of the rate, is being drawn wrong
	// too.
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
void PLUGIN_API FilterDrumEditor::close ()
{
	// The map holds RAW pointers to views the FRAME owns. Clearing it
	// before forgetting the frame, and never dereferencing it after, is
	// what keeps that safe - see the note in
	// FilterDrumController::editorDestroyed for the other half.
	mControls.clear ();
	mVelocityLabel = nullptr;
	mRateLabel = nullptr;

	if (frame)
	{
		frame->forget ();
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
void FilterDrumEditor::addSlider (ParamID tag, int column, int y)
{
	const ParamDef& def = paramDef (tag);

	const int x = kMargin + column * kColumnPitch;
	CRect r (x, y, x + kColumnWidth, y + kSliderHeight);

	auto* slider = new SpySlider (r, this, static_cast<int32_t> (tag));

	// The label is the table's title with the section prefix stripped -
	// the section heading above the row already says VCF or VCA, and
	// repeating it costs the characters that make "Release" legible.
	const char* label = def.title;
	if (std::strncmp (label, "VCF ", 4) == 0 || std::strncmp (label, "VCA ", 4) == 0)
		label += 4;
	slider->setLabel (label);

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
	if (tag == kVcfAmount || tag == kVcfVelocity ||
	    tag == kVcaAmount || tag == kVcaVelocity)
		refreshAllReadouts ();

	if (tag == kResonance)
		refreshResonanceLamp ();
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

	if (tag == kVcfAmount || tag == kVcfVelocity ||
	    tag == kVcaAmount || tag == kVcaVelocity)
		refreshAllReadouts ();

	if (tag == kResonance)
		refreshResonanceLamp ();
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

	switch (tag)
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
	const ParamDef& vcfAmt  = paramDef (kVcfAmount);
	const ParamDef& vcfVel  = paramDef (kVcfVelocity);
	const ParamDef& vcaAmt  = paramDef (kVcaAmount);
	const ParamDef& vcaVel  = paramDef (kVcaVelocity);

	const double octaves = vcfAmt.toInternal (normalizedOf (kVcfAmount));
	const double vcfSens = vcfVel.toInternal (normalizedOf (kVcfVelocity));
	const double gain    = vcaAmt.toInternal (normalizedOf (kVcaAmount));
	const double vcaSens = vcaVel.toInternal (normalizedOf (kVcaVelocity));

	// velocityScaled() IS THE SHARED LAW - the same call
	// FilterDrumDsp::trigger latches its amounts with. Velocity arrives
	// from VST3 already normalised, so 1.0 is MIDI 127 and 64/127 is
	// the middle of the scale.
	const double vcfFull = velocityScaled (octaves, vcfSens, 1.0);
	const double vcfHalf = velocityScaled (octaves, vcfSens, 64.0 / 127.0);
	const double vcaFull = velocityScaled (gain, vcaSens, 1.0);
	const double vcaHalf = velocityScaled (gain, vcaSens, 64.0 / 127.0);

	char text[160] = {};
	std::snprintf (text, sizeof (text),
	               "vel 127:  VCF %+.2f oct   VCA %.0f%%       "
	               "vel 64:  VCF %+.2f oct   VCA %.0f%%       vel 0:  VCF %+.2f oct   VCA %.0f%%",
	               vcfFull, vcaFull * 100.0,
	               vcfHalf, vcaHalf * 100.0,
	               velocityScaled (octaves, vcfSens, 0.0),
	               velocityScaled (gain, vcaSens, 0.0) * 100.0);

	return std::string (text);
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshResonanceLamp ()
{
	auto it = mControls.find (kResonance);
	if (it == mControls.end () || it->second == nullptr)
		return;

	auto* slider = dynamic_cast<SpySlider*> (it->second);
	if (slider == nullptr)
		return;

	// selfOscillating() ON THE INTERNAL VALUE - K, not per cent. The
	// threshold is a property of the filter, so it is stated once, in
	// FilterDrumDsp.h, and both the lamp and the filter read it there.
	const double k = paramDef (kResonance).toInternal (normalizedOf (kResonance));
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

	refreshResonanceLamp ();

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
