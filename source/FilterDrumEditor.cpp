//------------------------------------------------------------------------
// FilterDrum - editor implementation (the black panel, Mirror Columns)
//------------------------------------------------------------------------

#include "FilterDrumEditor.h"
#include "PanelWear.h"
#include "FilterDrumController.h"
#include "FilterDrumDsp.h"
#include "FilterDrumTransport.h"

#include <algorithm>
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

CRect toCRect (const Layout::Rect& r)
{
	return CRect (r.left, r.top, r.right, r.bottom);
}

/** How long a STRIKE lamp takes to fade, and how often it steps. A
    quarter of a second is long enough to see a single hit and short
    enough that sixteenths at 120 bpm (125 ms apart) still flicker. */
constexpr uint32_t kStrikeTickMs  = 30;
constexpr float    kStrikeFadeMs  = 250.f;

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
Panel::TextLine* FilterDrumEditor::addTextLine (const Layout::Rect& r, const Lettering::Style& style,
                                                const CColor& colour, Layout::Align align)
{
	auto* line = new Panel::TextLine (toCRect (r), style, colour, align);
	frame->addView (line);
	return line;
}

//------------------------------------------------------------------------
bool PLUGIN_API FilterDrumEditor::open (void* parent, const PlatformType& platformType)
{
	if (frame)
		return false;

	using namespace Layout;
	namespace Pal = Panel::Palette;

	CRect frameSize (0, 0, kEditorWidth, kEditorHeight);
	frame = new CFrame (frameSize, this);
	frame->setBackgroundColor (Pal::kGround);
	frame->open (parent, platformType);

	// ---- everything that never changes, behind everything else --------
	frame->addView (new Panel::Backdrop (frameSize));

	// ---- per column: the screens, the lamps, the velocity line ----------
	for (int drum = 1; drum <= 2; ++drum)
	{
		const int i = drum - 1;

		// The filter at the knobs (solid, filled), and the filter at the
		// peak of a velocity-127 hit (dashed) - so the screen shows where
		// the sweep starts from and where it reaches.
		const int fi = filterScreenIndex (drum);
		mFilterScope[i] = new Panel::Scope (toCRect (kScreens[fi]));
		mFilterScope[i]->setColours (Pal::accent (Accent::Filter), Pal::accentAlpha (Accent::Filter, 190));
		mFilterScope[i]->setFill (true);
		mFilterScope[i]->setCompanionDashed (true);
		mFilterScope[i]->setWear (Wear::glassWear (fi));
		frame->addView (mFilterScope[i]);

		// The VCA envelope (solid, filled) over the VCF envelope (dashed),
		// on one time axis - traceDrumEnvelopes() draws both.
		const int ei = envScreenIndex (drum);
		mEnvScope[i] = new Panel::Scope (toCRect (kScreens[ei]));
		mEnvScope[i]->setColours (Pal::accent (Accent::Amp), Pal::accent (Accent::Filter));
		mEnvScope[i]->setFill (true);
		mEnvScope[i]->setCompanionDashed (true);
		mEnvScope[i]->setWear (Wear::glassWear (ei));
		frame->addView (mEnvScope[i]);

		mSelfOsc[i] = new Panel::Lamp (toCRect (columnRect (Column::kSelfOscLamp, drum)), "Self-osc");
		frame->addView (mSelfOsc[i]);

		// THE STRIKE LAMP in the drum's own title row.
		mStrikeLamp[i] = new Panel::Lamp (toCRect (columnRect (Column::kStrikeLamp, drum)), "Strike");
		frame->addView (mStrikeLamp[i]);

		mVelocityLine[i] = addTextLine (columnRect (Column::kVelocityLine, drum), kTextInfo, Pal::kInkSoft,
		                                Align::Centre);
	}

	// ---- the header and the spine's lines -------------------------------
	mEngineLine = addTextLine (kEngineLine, kTextEngine, Pal::kInk, Align::Left);
	mMixLine = addTextLine (kMixGainsLine, kTextInfo, Pal::kInkSoft, Align::Centre);
	mStrikeVelLine = addTextLine (kStrikeVelLine, kTextInfo, Pal::kInkSoft, Align::Centre);

	// ---- every control, from the table ----------------------------------
	for (int i = 0; i < kNumControls; ++i)
		addControl (kControls[i], i);

	refreshAllReadouts ();

	return true;
}

//------------------------------------------------------------------------
void FilterDrumEditor::addControl (const Layout::Control& c, int index)
{
	using namespace Layout;
	const CRect r = toCRect (controlRect (c));
	const int32_t tag = static_cast<int32_t> (c.tag);
	const std::string label = c.label ? std::string (c.label) : shortLabelFor (c.tag);

	switch (c.kind)
	{
		case Kind::Knob:
		{
			auto* knob = new Panel::Knob (r, this, tag, c.accent, c.diameter, c.bipolar, c.reversed);
			knob->setLabel (label);
			// Its own wear, seeded by its place in the table, so every knob
			// is scuffed differently and the same on every machine.
			knob->setWear (Wear::knobWear (index));
			// The mix's ends are the drums: D1 on the left, D2 on the right.
			if (c.tag == kMix)
				knob->setEnds ("D1", "D2");
			// THE PANEL BORROWS ONE FORMATTER for every reading, so the
			// panel cannot disagree with itself about what a position
			// means.
			const ParamID id = c.tag;
			knob->setFormatter ([this, id] (float) { return readoutFor (id); });
			knob->setDefaultValue (static_cast<float> (paramDef (c.tag).defaultNormalized ()));
			registerControl (c.tag, knob);
			break;
		}

		case Kind::Switch:
		{
			auto* sw = new Panel::RotarySwitch (r, this, tag, c);
			std::vector<std::string> names;
			for (int i = 0; i < kLaunchDivisionCount; ++i)
				names.push_back (launchPositionName (i));
			sw->setNames (names);
			sw->setLabel (label);
			sw->setWear (Wear::selectorGrit (index));
			registerControl (c.tag, sw);
			break;
		}

		case Kind::Step:
		{
			char number[8] = {};
			std::snprintf (number, sizeof (number), "%d", static_cast<int> (c.tag - kStep1) + 1);
			const int step = static_cast<int> (c.tag - kStep1);
			auto* pad = new Panel::StepPad (r, this, tag, number);
			// THE DISABLE IS A SECOND PARAMETER on the same pad - see
			// StepPad. It has no control of its own in the layout.
			pad->setDisabled (normalizedOf (stepDisableParam (step)) >= 0.5);
			pad->setDisableHandler ([this, step] () { toggleStepDisabled (step); });
			registerControl (c.tag, pad);
			break;
		}

		case Kind::Toggle:
		{
			auto* button = new Panel::ToggleButton (r, this, tag, label);
			const ParamID id = c.tag;
			button->setFormatter ([this, id] (float) { return readoutFor (id); });
			registerControl (c.tag, button);
			break;
		}
	}
}

//------------------------------------------------------------------------
void PLUGIN_API FilterDrumEditor::close ()
{
	if (mStrikeTimer)
	{
		mStrikeTimer->stop ();
		mStrikeTimer = nullptr;
	}

	mControls.clear ();
	for (int i = 0; i < 2; ++i)
	{
		mFilterScope[i] = nullptr;
		mEnvScope[i] = nullptr;
		mSelfOsc[i] = nullptr;
		mStrikeLamp[i] = nullptr;
		mVelocityLine[i] = nullptr;
	}
	mEngineLine = nullptr;
	mMixLine = nullptr;
	mStrikeVelLine = nullptr;
	mPlayhead = -1;

	if (frame)
	{
		frame->forget ();
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
std::string FilterDrumEditor::shortLabelFor (ParamID tag)
{
	int drum = 0;
	ParamID base = tag;
	splitDrumParam (tag, drum, base);

	std::string label = paramDef (base).title;
	if (label.rfind ("VCF ", 0) == 0 || label.rfind ("VCA ", 0) == 0)
		label.erase (0, 4);
	return label;
}

//------------------------------------------------------------------------
void FilterDrumEditor::registerControl (ParamID tag, CControl* control)
{
	mControls[tag] = control;
	if (mController)
		control->setValueNormalized (static_cast<float> (mController->getParamNormalized (tag)));
	else
		control->setValueNormalized (static_cast<float> (paramDef (tag).defaultNormalized ()));
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

double FilterDrumEditor::internalOf (int drum, ParamID base) const
{
	const ParamID id = static_cast<ParamID> (base + ((drum == 2) ? kDrum2Offset : 0));
	return paramDef (id).toInternal (normalizedOf (id));
}

double FilterDrumEditor::sampleRate () const
{
	return mController ? mController->getHostSampleRate () : 44100.0;
}

//------------------------------------------------------------------------
void FilterDrumEditor::valueChanged (CControl* control)
{
	if (!control || !mController || mUpdating)
		return;

	const ParamID    tag   = static_cast<ParamID> (control->getTag ());
	const ParamValue value = control->getValueNormalized ();

	// BOTH CALLS, in this order. setParamNormalized updates the
	// controller's own copy so the panel and the host agree at once;
	// performEdit tells the HOST, which is what makes the move
	// automatable and undoable.
	mController->setParamNormalized (tag, value);
	mController->performEdit (tag, value);

	parameterMoved (tag);
}

//------------------------------------------------------------------------
Panel::StepPad* FilterDrumEditor::stepPad (int step) const
{
	if (step < 0 || step >= 16)
		return nullptr;
	auto it = mControls.find (static_cast<ParamID> (kStep1 + step));
	if (it == mControls.end () || it->second == nullptr)
		return nullptr;
	return dynamic_cast<Panel::StepPad*> (it->second);
}

//------------------------------------------------------------------------
void FilterDrumEditor::toggleStepDisabled (int step)
{
	if (!mController || step < 0 || step >= 16)
		return;

	const ParamID id = stepDisableParam (step);
	const ParamValue value = (mController->getParamNormalized (id) >= 0.5) ? 0.0 : 1.0;

	// ONE EDIT, bracketed, so a host records a single undoable change.
	// setParamNormalized comes back through updateControl below, which is
	// what redraws the pad - the same path as automation.
	mController->beginEdit (id);
	mController->setParamNormalized (id, value);
	mController->performEdit (id, value);
	mController->endEdit (id);
}

//------------------------------------------------------------------------
void FilterDrumEditor::updateControl (ParamID tag, ParamValue normalized)
{
	// A step's disable has no control of its own: it is drawn by the pad.
	if (const int step = stepOfDisableParam (tag); step >= 0)
	{
		if (auto* pad = stepPad (step))
			pad->setDisabled (normalized >= 0.5);
		return;
	}

	auto it = mControls.find (tag);
	if (it != mControls.end () && it->second != nullptr)
	{
		mUpdating = true;
		it->second->setValueNormalized (static_cast<float> (normalized));
		mUpdating = false;
	}

	parameterMoved (tag);
}

//------------------------------------------------------------------------
void FilterDrumEditor::parameterMoved (ParamID tag)
{
	auto it = mControls.find (tag);
	if (it != mControls.end () && it->second != nullptr)
		it->second->invalid ();

	int drum = 0;
	ParamID base = tag;
	if (splitDrumParam (tag, drum, base))
		refreshDrum (drum);
	else if (tag == kMix)
		refreshMixLine ();
}

//------------------------------------------------------------------------
std::string FilterDrumEditor::readoutFor (ParamID tag) const
{
	// THE SAME FUNCTION the tests sweep and the panel picture prints.
	return readoutText (tag, normalizedOf (tag), mPlayhead >= 0);
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshDrum (int drum)
{
	if (drum < 1 || drum > 2)
		return;
	const int i = drum - 1;

	refreshFilterScreen (drum);
	refreshEnvelopeScreen (drum);

	// selfOscillating() ON K, the same predicate the filter's threshold
	// is written in, so the lamp cannot disagree with what you hear.
	if (mSelfOsc[i])
		mSelfOsc[i]->setOn (selfOscillating (internalOf (drum, kResonance)));

	if (mVelocityLine[i])
	{
		mVelocityLine[i]->setText (
			velocityLineText ([this] (int d, ParamID base) { return internalOf (d, base); }, drum));
	}
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshFilterScreen (int drum)
{
	Panel::Scope* scope = mFilterScope[drum - 1];
	if (!scope)
		return;

	using namespace Layout;
	const double rate = sampleRate ();
	const double cutoff = internalOf (drum, kCutoff);
	const double k = internalOf (drum, kResonance);

	// Where a full-velocity strike takes the cutoff: cutoffWithEnv() at
	// the envelope's peak, with the amount through velocityScaled() -
	// the voice's own two calls.
	const double octaves = velocityScaled (internalOf (drum, kVcfAmount), internalOf (drum, kVcfVelocity), 1.0);
	const double peakHz = cutoffWithEnv (cutoff, octaves, 1.0, rate);

	float atKnobs[kDisplayPoints];
	float atStrike[kDisplayPoints];
	const double dotY = traceFilterResponse (cutoff, k, rate, kFilterMinHz, kFilterMaxHz,
	                                         kFilterTopDb, kFilterBottomDb, atKnobs, kDisplayPoints);
	traceFilterResponse (peakHz, k, rate, kFilterMinHz, kFilterMaxHz,
	                     kFilterTopDb, kFilterBottomDb, atStrike, kDisplayPoints);

	// The dot at the cutoff the filter will actually use.
	const double used = cutoffWithEnv (cutoff, 0.0, 0.0, rate);
	scope->setMarker (std::log (used / kFilterMinHz) / std::log (kFilterMaxHz / kFilterMinHz), dotY);
	scope->setAnnotation (filterPeakText (peakHz));
	scope->setCurves (atKnobs, atStrike, kDisplayPoints);
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshEnvelopeScreen (int drum)
{
	Panel::Scope* scope = mEnvScope[drum - 1];
	if (!scope)
		return;

	using namespace Layout;

	// Times in SECONDS - the table's internal units - which is what
	// ArSpec wants.
	ArSpec vcf;
	vcf.attack  = internalOf (drum, kVcfAttack);
	vcf.release = internalOf (drum, kVcfRelease);
	ArSpec vca;
	vca.attack  = internalOf (drum, kVcaAttack);
	vca.release = internalOf (drum, kVcaRelease);

	float vcfCurve[kDisplayPoints];
	float vcaCurve[kDisplayPoints];
	const double span = traceDrumEnvelopes (vcf, vca, vcfCurve, vcaCurve, kDisplayPoints);

	// HEIGHTS ARE THE AMOUNTS: the VCA's linear gain, and the VCF's depth
	// as a fraction of the most it can be. A sweep of three octaves down
	// is as deep as three up - the caption carries the sign.
	const double octaves = internalOf (drum, kVcfAmount);
	const double gain    = internalOf (drum, kVcaAmount);
	const float vcfScale = static_cast<float> (std::min (1.0, std::fabs (octaves) / kMaxEnvOctaves));
	for (int i = 0; i < kDisplayPoints; ++i)
	{
		vcfCurve[i] *= vcfScale;
		vcaCurve[i] *= static_cast<float> (gain);
	}

	// The depth, and the span in the units the knobs are in.
	scope->setCaption (envCaptionText (octaves));
	scope->setAnnotation (envSpanText (span));
	scope->setCurves (vcaCurve, vcfCurve, kDisplayPoints);
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshMixLine ()
{
	if (!mMixLine)
		return;

	mMixLine->setText (mixGainsText (paramDef (kMix).toInternal (normalizedOf (kMix))));
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshEngineLine ()
{
	if (!mEngineLine)
		return;

	mEngineLine->setText (engineLineText (sampleRate ()));
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshAllReadouts ()
{
	for (auto& entry : mControls)
		if (entry.second != nullptr)
			entry.second->invalid ();

	refreshDrum (1);
	refreshDrum (2);
	refreshMixLine ();
	refreshEngineLine ();

	if (mStrikeVelLine && mStrikeVelLine->getText ().empty ())
		mStrikeVelLine->setText (lastStrikeText (-1.0));
}

//------------------------------------------------------------------------
void FilterDrumEditor::setPlayhead (int step)
{
	if (step == mPlayhead)
		return;

	// ONLY THE TWO THAT CHANGE are repainted, not all sixteen.
	auto mark = [this] (int index, bool here) {
		if (index < 0 || index >= 16)
			return;
		if (auto* pad = stepPad (index))
			pad->setPlayhead (here);
	};

	const bool startedOrStopped = (mPlayhead < 0) != (step < 0);

	mark (mPlayhead, false);
	mark (step, true);
	mPlayhead = step;

	// Run's status line reads "armed" or "running" off the playhead.
	if (startedOrStopped)
	{
		auto it = mControls.find (kSeqRun);
		if (it != mControls.end () && it->second != nullptr)
			it->second->invalid ();
	}
}

//------------------------------------------------------------------------
void FilterDrumEditor::strike (double velocity)
{
	if (!frame)
		return;

	// A LAMP LIGHTS FOR A DRUM THAT SOUNDS: both drums are struck by
	// every note, but one whose VCA amount at this velocity is nothing
	// made no sound, and a lamp that says otherwise is lying.
	for (int drum = 1; drum <= 2; ++drum)
	{
		const double g = velocityScaled (internalOf (drum, kVcaAmount), internalOf (drum, kVcaVelocity), velocity);
		if (auto* lamp = mStrikeLamp[drum - 1])
			lamp->setLevel ((g > 0.001) ? 1.f : 0.f);
	}

	if (mStrikeVelLine)
		mStrikeVelLine->setText (lastStrikeText (velocity));

	if (!mStrikeTimer)
		mStrikeTimer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { fadeStrike (); },
		                                        kStrikeTickMs, false);
	mStrikeTimer->start ();
}

//------------------------------------------------------------------------
void FilterDrumEditor::fadeStrike ()
{
	const float step = static_cast<float> (kStrikeTickMs) / kStrikeFadeMs;
	bool lit = false;
	for (auto* lamp : mStrikeLamp)
	{
		if (!lamp)
			continue;
		lamp->setLevel (lamp->level () - step);
		lit = lit || lamp->isOn ();
	}
	if (!lit && mStrikeTimer)
		mStrikeTimer->stop ();
}

//------------------------------------------------------------------------
} // namespace FilterDrum
