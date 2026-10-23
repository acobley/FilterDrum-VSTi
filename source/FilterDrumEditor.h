//------------------------------------------------------------------------
// FilterDrum - editor
//
// THE BLACK PANEL, "MIRROR COLUMNS" (branch PolivoksUI, Oct 2026), from
// Designs/FilterDrum-Polivoks-3-MirrorColumns.png: R2Synth 1.1's black
// panel - white printing, knurled knobs, OLED screens, the 70s sci-fi
// lettering, the "Studio Worn" wear - on the drums as two side-by-side
// COLUMNS that read downward the way the sound travels (the screens,
// NOISE > VCF, the VCF envelope, the VCA), with the mix, the output and
// the transport in a spine between them and the sequencer along the foot.
// (It replaces Design 2.0's two lanes, Designs/FilterDrum-MainDesign.png.)
//
// The layout is DATA, in FilterDrumLayout.h; the views are in
// FilterDrumPanelViews.*; every piece of text is a function in
// FilterDrumReadouts.*. This file builds the panel from the tables and
// keeps its readings current.
//
// TWO RULES THIS PANEL KEEPS:
//
//   1. Every number shown comes from the parameter table or from a
//      function in FilterDrumDsp.h that the AUDIO PATH also calls (or
//      that the DSP tests hold to it) - never from arithmetic written a
//      second time here. The SELF-OSC lamps light from selfOscillating();
//      the filter screen is ms20ResponseGain(), within 0.25 dB of the
//      measured filter; the strike curve is cutoffWithEnv() at
//      velocityScaled(); the gains under the mix are crossfadeGain*().
//
//   2. NO BITMAPS. Paths only - the lettering included - so resource/
//      needs no artwork.
//
// TOOLTIPS MAY NEVER APPEAR ON macOS - see PORTING-GUIDE.md section 6.
// So the velocity law is printed along the foot of each column rather
// than hidden in a hover.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumLayout.h"
#include "FilterDrumPanelViews.h"
#include "FilterDrumParams.h"
#include "FilterDrumReadouts.h"

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

	/** Rebuild every reading. Called when the sample rate changes,
	    because the cutoff ceiling and the filter screen depend on it. */
	void refreshAllReadouts ();

	/** The text under one control - readoutText() at its value. */
	std::string readoutFor (Steinberg::Vst::ParamID tag) const;

	/** The playhead, from the controller. -1 when the sequencer is not
	    playing, otherwise the step it is on. */
	void setPlayhead (int step);

	/** THE DRUMS WERE STRUCK, at `velocity` 0..1 - from kStrikeOut,
	    through the controller. Lights each STRIKE lamp whose drum makes
	    a sound at that velocity, and lets them fade. */
	void strike (double velocity);

	static const int kEditorWidth  = Layout::kEditorWidth;
	static const int kEditorHeight = Layout::kEditorHeight;

private:
	void addControl (const Layout::Control& c, int index);
	Panel::TextLine* addTextLine (const Layout::Rect& r, const Lettering::Style& style,
	                              const VSTGUI::CColor& colour, Layout::Align align);
	void registerControl (Steinberg::Vst::ParamID tag, VSTGUI::CControl* control);

	/** Everything that follows from one parameter having moved. One
	    place, called from valueChanged and updateControl alike. */
	void parameterMoved (Steinberg::Vst::ParamID tag);

	/** One column's screens, lamp and velocity line. `drum` is 1 or 2. */
	void refreshDrum (int drum);
	void refreshFilterScreen (int drum);
	void refreshEnvelopeScreen (int drum);
	void refreshMixLine ();
	void refreshEngineLine ();

	/** A right-click on step `step`'s pad: flip its disable parameter,
	    as one edit the host can record and undo. */
	void toggleStepDisabled (int step);

	/** Step `step`'s pad, or null. */
	Panel::StepPad* stepPad (int step) const;

	/** The STRIKE lamps' fade, one tick of it. */
	void fadeStrike ();

	/** The label under a control: the table's title, less its section
	    and its drum number - "VCF 2 Release" is "Release". */
	static std::string shortLabelFor (Steinberg::Vst::ParamID tag);

	/** The current normalised value of a parameter, from the controller,
	    or the table's default with no controller. */
	double normalizedOf (Steinberg::Vst::ParamID tag) const;

	/** A drum parameter's value in INTERNAL units (seconds, octaves, K,
	    linear gain) - toInternal(), the conversion the processor uses. */
	double internalOf (int drum, Steinberg::Vst::ParamID base) const;

	double sampleRate () const;

	FilterDrumController* mController = nullptr;

	/** RAW pointers to views the FRAME owns. close() forgets them all at
	    once, before the frame goes - see the note in
	    FilterDrumController::editorDestroyed for the other half. */
	std::map<Steinberg::Vst::ParamID, VSTGUI::CControl*> mControls;
	Panel::Scope* mFilterScope[2] = { nullptr, nullptr };
	Panel::Scope* mEnvScope[2]    = { nullptr, nullptr };
	Panel::Lamp*  mSelfOsc[2]     = { nullptr, nullptr };
	Panel::Lamp*  mStrikeLamp[2]  = { nullptr, nullptr };
	Panel::TextLine* mVelocityLine[2] = { nullptr, nullptr };
	Panel::TextLine* mEngineLine = nullptr;
	Panel::TextLine* mMixLine = nullptr;
	Panel::TextLine* mStrikeVelLine = nullptr;

	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mStrikeTimer;

	/** Where the pads currently say the playhead is, so only the two
	    that change are redrawn. */
	int mPlayhead = -1;

	/** Set while the controller is pushing a value INTO a control, so
	    valueChanged does not send it straight back out as an edit. */
	bool mUpdating = false;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
