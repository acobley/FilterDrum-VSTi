//------------------------------------------------------------------------
// FilterDrum - editor
//
// TWO DRUMS, one above the other, and the crossfader between them on
// the right. Twenty-four controls.
//
// DRUM 1 IS THE TOP BLOCK AND THE TOP OF THE FADER, which is the whole
// reason the fader is drawn vertically: laid out that way the control
// and the panel agree about which drum is which without a legend.
//
// TWO RULES THIS PANEL KEEPS, and they are the ones that get broken
// first:
//
//   1. Every number shown comes from the parameter table or from a
//      function in FilterDrumDsp.h that the AUDIO PATH also calls -
//      never from arithmetic written a second time here. The
//      self-oscillation lamp is the clearest case: it lights from
//      selfOscillating(), the same predicate the filter's own threshold
//      is expressed in, so the lamp cannot disagree with what you hear.
//
//   2. The layout is DATA, at the top of the .cpp, not positions typed
//      into the middle of open().
//
// NO BITMAPS. The controls in FilterDrumControls.* draw everything with
// rectangles and text, inherited from a DXi property page that used
// GDI, so the panel is resolution-independent for free and resource/
// needs no artwork at all. Keep any new control the same way.
//
// TOOLTIPS MAY NEVER APPEAR ON macOS - see PORTING-GUIDE.md section 6.
// So the velocity law, which is the one thing about this plug-in a user
// cannot guess, is printed on the panel rather than hidden in a hover.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumControls.h"
#include "FilterDrumParams.h"

#include "public.sdk/source/vst/vstguieditor.h"

#include <map>
#include <string>

namespace FilterDrum {

class FilterDrumController;

//------------------------------------------------------------------------
class FilterDrumEditor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
	explicit FilterDrumEditor (FilterDrumController* controller);

	bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
	void PLUGIN_API close () SMTG_OVERRIDE;

	// IControlListener
	void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;

	/** Called by the controller when a parameter changes anywhere else -
	    automation, the host's generic editor, a second panel. */
	void updateControl (Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue normalized);

	/** Rebuild every readout. Called when the sample rate changes,
	    because the cutoff ceiling is a fraction of it. */
	void refreshAllReadouts ();

	/** The text drawn on one control. PUBLIC SO IT CAN BE UNIT TESTED
	    without a host - which is the only way it gets tested at all on
	    this project, since the session writing it cannot run a build. */
	std::string readoutFor (Steinberg::Vst::ParamID tag) const;

	/** The line along the bottom: what the two Amount knobs actually
	    become at full and half velocity, through the shared
	    velocityScaled(). Public for the same reason. */
	std::string velocityLine () const;

	static const int kEditorWidth  = 840;
	static const int kEditorHeight = 348;

private:
	void addSlider (Steinberg::Vst::ParamID tag, int column, int y);
	void addSectionLabel (const char* text, int y);
	void addDrumBlock (int drum, int labelY, int vcfRowY, int vcaRowY);
	void registerControl (Steinberg::Vst::ParamID tag, VSTGUI::CControl* control);
	void refreshReadout (Steinberg::Vst::ParamID tag);

	/** The row label for a control: drum 1's title with the section
	    prefix stripped. BOTH ROWS READ IT FROM DRUM 1, so "Release"
	    under drum 2 is guaranteed to be the same word as the one under
	    drum 1 - the section heading above each row is what says which
	    drum you are looking at. */
	static std::string shortLabelFor (Steinberg::Vst::ParamID tag);

	/** The current normalised value of a parameter, from the
	    controller. Falls back to the table's default if there is no
	    controller, so readoutFor() is testable standalone. */
	double normalizedOf (Steinberg::Vst::ParamID tag) const;

	/** Light or clear a resonance lamp from selfOscillating(). Called
	    for both drums' knobs - each has its own. */
	void refreshResonanceLamp (Steinberg::Vst::ParamID tag);

	FilterDrumController* mController = nullptr;

	std::map<Steinberg::Vst::ParamID, VSTGUI::CControl*> mControls;
	VSTGUI::CTextLabel* mVelocityLabel = nullptr;
	VSTGUI::CTextLabel* mMixLabel = nullptr;
	VSTGUI::CTextLabel* mRateLabel = nullptr;

	/** Set while the controller is pushing a value INTO a control, so
	    valueChanged does not send it straight back out as an edit. A
	    host that automates a parameter would otherwise get a feedback
	    loop it cannot break. */
	bool mUpdating = false;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
