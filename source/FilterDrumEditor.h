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

	/** The playhead, from the controller. -1 when the sequencer is not
	    playing, otherwise the step it is on. Lights the step lamps. */
	void setPlayhead (int step);

	// 1013 = the drum box - seven columns and the envelope strip inside
	// it - plus the fader's box. It was 986 before the boxes; the
	// padding for a drum box and a group box inside it moved the columns
	// inboard by eleven on each side. The layout banner at the top of
	// FilterDrumEditor.cpp has the arithmetic, and static_asserts there
	// keep it true.
	static const int kEditorWidth  = 1013;

	/** 521 = two two-row drum boxes, the two readout lines and the
	    sequencer box, plus a bottom margin.

	    424 before the boxes; 649 with a third SHAPE row per drum and
	    every row boxed; 521 once the shape controls were taken back out
	    and each drum returned to two rows. A box costs 12 a row for the
	    title on the top edge and the clearance under it. Every Y in
	    FilterDrumEditor.cpp is derived from kRowPitch and kBlockPitch,
	    and a static_assert there checks the sequencer row still fits
	    inside this - so a row added to a drum fails the build rather
	    than pushing the steps off the bottom edge. */
	static const int kEditorHeight = 521;

private:
	void addSlider (Steinberg::Vst::ParamID tag, int column, int y);
	void addSectionLabel (const char* text, int y);

	/** One SpyGroupBox. `title` may be null for an untitled border, and
	    `drumBox` picks the outer colour over the inner one. */
	void addGroupBox (int x, int y, int w, int h, const char* title,
	                  bool drumBox = false);
	void addDrumBlock (int drum, int labelY, int vcfRowY, int vcaRowY);
	void addStepRow ();
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

	/** Redraw one drum's envelope display from that drum's parameters.
	    `drum` is 1 or 2, as everywhere else in this class. */
	void refreshEnvelopeDisplay (int drum);

	FilterDrumController* mController = nullptr;

	std::map<Steinberg::Vst::ParamID, VSTGUI::CControl*> mControls;
	/** The two envelope displays, indexed by drum - 1. Raw pointers to
	    views the FRAME owns, like everything in mControls; close()
	    forgets them at the same time and for the same reason. */
	SpyEnvelopeView* mEnvViews[2] = { nullptr, nullptr };

	VSTGUI::CTextLabel* mVelocityLabel = nullptr;
	VSTGUI::CTextLabel* mMixLabel = nullptr;

	/** Where the lamps currently say the playhead is, so only the two
	    that change are redrawn. Repainting all sixteen on every step
	    would be sixteen invalidations a sixteenth note. */
	int mPlayhead = -1;
	VSTGUI::CTextLabel* mRateLabel = nullptr;

	/** Set while the controller is pushing a value INTO a control, so
	    valueChanged does not send it straight back out as an edit. A
	    host that automates a parameter would otherwise get a feedback
	    loop it cannot break. */
	bool mUpdating = false;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
