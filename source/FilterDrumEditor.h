//------------------------------------------------------------------------
// FilterDrum - editor
//
// A blank panel with the ONE placeholder parameter on it and nothing
// else. It is here for two reasons that survive the scaffold:
//
//   1. It proves the whole UI path end to end - control moves,
//      performEdit reaches the host, the host tells the controller, the
//      controller tells this editor, and the panel agrees with the
//      host's own generic editor. A plug-in whose panel is added at the
//      end has all of that to debug at once.
//
//   2. It establishes the two rules that get broken later. Every number
//      shown comes from a function in FilterDrumDsp.h or from the
//      parameter table - never from arithmetic written twice. And the
//      layout is DATA, at the top of the .cpp, not positions typed into
//      the middle of open().
//
// NO BITMAPS, deliberately. The controls in FilterDrumControls.* draw
// everything with rectangles and text, inherited from a DXi property
// page that used GDI, so the panel is resolution-independent for free
// and resource/ needs no artwork at all. Keep any new control the same
// way.
//
// TOOLTIPS MAY NEVER APPEAR ON macOS - see PORTING-GUIDE.md section 6.
// Anything the user has to know is drawn on the panel, not hidden in a
// hover.
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
	    because anything quoted in Hz or in milliseconds depends on it. */
	void refreshAllReadouts ();

	/** The text drawn on one control. PUBLIC SO IT CAN BE UNIT TESTED
	    without a host - which is the only way it gets tested at all on
	    this project, since the session writing it cannot run a build. */
	std::string readoutFor (Steinberg::Vst::ParamID tag) const;

	static const int kEditorWidth  = 420;
	static const int kEditorHeight = 150;

private:
	void addSlider (Steinberg::Vst::ParamID tag, int x, int y, int w, int h);
	void registerControl (Steinberg::Vst::ParamID tag, VSTGUI::CControl* control);
	void refreshReadout (Steinberg::Vst::ParamID tag);

	FilterDrumController* mController = nullptr;

	std::map<Steinberg::Vst::ParamID, VSTGUI::CControl*> mControls;
	VSTGUI::CTextLabel* mRateLabel = nullptr;

	/** Set while the controller is pushing a value INTO a control, so
	    valueChanged does not send it straight back out as an edit. A
	    host that automates a parameter would otherwise get a feedback
	    loop it cannot break. */
	bool mUpdating = false;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
