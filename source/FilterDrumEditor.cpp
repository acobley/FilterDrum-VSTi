//------------------------------------------------------------------------
// FilterDrum - editor implementation
//------------------------------------------------------------------------

#include "FilterDrumEditor.h"
#include "FilterDrumController.h"
#include "FilterDrumDsp.h"

#include <cstdio>

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
    they are simply chosen - but they are chosen in one place, so the
    panel can be re-proportioned without reading the construction code.
 
    The SlideSpin the controls descend from was 69 x 44 pixels, and the
    width below is that rounded up to something a dB reading fits in. */
constexpr int kMargin        = 16;
constexpr int kTitleY        = 12;
constexpr int kTitleHeight   = 18;
constexpr int kSliderX       = kMargin;
constexpr int kSliderY       = 48;
constexpr int kSliderWidth   = 120;
constexpr int kSliderHeight  = 44;
constexpr int kRateY         = 116;
constexpr int kRateHeight    = 16;

/** The panel's own background. Darker than the controls' bar fill so the
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
		CRect r (kMargin, kTitleY, kEditorWidth - kMargin, kTitleY + kTitleHeight);
		auto* title = new CTextLabel (r, "FilterDrum");
		title->setFont (panelFont ());
		title->setFontColor (Colours::kLabel);
		title->setBackColor (kPanelBack);
		title->setFrameColor (kPanelBack);
		title->setHoriAlign (kLeftText);
		title->setMouseEnabled (false);
		frame->addView (title);
	}

	// ---- the one parameter ---------------------------------------------
	addSlider (kOutputTrim, kSliderX, kSliderY, kSliderWidth, kSliderHeight);

	// ---- the sample rate the DSP is really running at -------------------
	//
	// A readout with no parameter behind it, which is exactly why it is
	// worth having in the scaffold: it is the only thing on the panel
	// that can only have arrived by message, from setActive, on the UI
	// thread. If it ever reads 44100 in a 96 k session, the message
	// route is broken and everything built on it - filter displays,
	// tempo readouts - will be wrong in the same silent way.
	{
		CRect r (kMargin, kRateY, kEditorWidth - kMargin, kRateY + kRateHeight);
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
	// before forgetting the frame, and never dereferencing it after,
	// is what keeps that safe - see the note in
	// FilterDrumController::editorDestroyed for the other half of this.
	mControls.clear ();
	mRateLabel = nullptr;

	if (frame)
	{
		frame->forget ();
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
void FilterDrumEditor::addSlider (ParamID tag, int x, int y, int w, int h)
{
	const ParamDef& def = paramDef (tag);

	CRect r (x, y, x + w, y + h);
	auto* slider = new SpySlider (r, this, static_cast<int32_t> (tag));
	slider->setLabel (def.title);

	// THE PANEL BORROWS THE PARAMETER'S OWN FORMATTING rather than
	// writing its own. The control is handed the NORMALISED value and
	// the table turns it into the number and the units, so the panel and
	// the host's generic editor cannot disagree about what a position
	// means - which they will, the first time a range changes and only
	// one of the two copies is updated.
	slider->setFormatter ([tag] (float normalized) {
		char text[64] = {};
		const ParamDef& d = paramDef (tag);
		std::snprintf (text, sizeof (text), "%+.1f %s", d.toPlain (normalized), d.units);
		return std::string (text);
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
}

//------------------------------------------------------------------------
std::string FilterDrumEditor::readoutFor (ParamID tag) const
{
	if (!isTableParam (tag))
		return std::string ();

	const ParamDef& def = paramDef (tag);

	double normalized = def.defaultNormalized ();
	auto it = mControls.find (tag);
	if (it != mControls.end () && it->second != nullptr)
		normalized = it->second->getValueNormalized ();
	else if (mController)
		normalized = mController->getParamNormalized (tag);

	char text[64] = {};

	if (tag == kOutputTrim)
	{
		// The dB and the LINEAR GAIN it means, from dbToGain() in
		// FilterDrumDsp.h - the same call the DSP makes. Not a
		// second implementation of the conversion, which is the rule
		// this readout exists to demonstrate.
		std::snprintf (text, sizeof (text), "%+.1f %s  (x%.3f)",
		               def.toPlain (normalized), def.units,
		               dbToGain (def.toInternal (normalized)));
	}
	else
	{
		std::snprintf (text, sizeof (text), "%.2f %s", def.toPlain (normalized), def.units);
	}

	return std::string (text);
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

	if (mRateLabel != nullptr && mController != nullptr)
	{
		char text[64] = {};
		std::snprintf (text, sizeof (text), "Engine: %.0f Hz",
		               mController->getHostSampleRate ());
		mRateLabel->setText (text);
		mRateLabel->invalid ();
	}
}

//------------------------------------------------------------------------
} // namespace FilterDrum
