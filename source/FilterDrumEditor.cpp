//------------------------------------------------------------------------
// FilterDrum - editor implementation (Design 2.0)
//------------------------------------------------------------------------

#include "FilterDrumEditor.h"
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

/** "0.71" as ".71" - a gain under one reads in half the width. */
std::string gainText (double g)
{
	char text[16] = {};
	std::snprintf (text, sizeof (text), "%.2f", g);
	return (text[0] == '0') ? std::string (text + 1) : std::string (text);
}

/** A frequency for a screen's corner: "800 Hz", "9.7k". */
std::string hzText (double hz)
{
	char text[24] = {};
	if (hz >= 1000.0)
		std::snprintf (text, sizeof (text), "%.1fk", hz / 1000.0);
	else
		std::snprintf (text, sizeof (text), "%.0f Hz", hz);
	return text;
}

const char* const kMiddleDot = "\xC2\xB7";

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
CTextLabel* FilterDrumEditor::addTextLine (const Layout::Rect& r, CFontRef font,
                                           const CColor& colour, CHoriTxtAlign align)
{
	auto* label = new CTextLabel (toCRect (r), "");
	label->setFont (font);
	label->setFontColor (colour);
	label->setTransparency (true);
	label->setStyle (CParamDisplay::kNoFrame);
	label->setHoriAlign (align);
	label->setMouseEnabled (false);
	frame->addView (label);
	return label;
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

	// ---- per lane: the screens, the lamp, the velocity line -------------
	for (int drum = 1; drum <= 2; ++drum)
	{
		const int top = laneTop (drum);
		const int i = drum - 1;

		// The filter at the knobs (solid, filled), and the filter at the
		// top of a velocity-127 strike (dashed) - so the screen shows
		// where the sweep starts from and where it reaches.
		mFilterScope[i] = new Panel::Scope (toCRect (laneRect (Lane::kFilterScreen, top)));
		mFilterScope[i]->setColours (Pal::accent (Accent::Filter), Pal::accentAlpha (Accent::Filter, 150));
		mFilterScope[i]->setFill (true);
		mFilterScope[i]->setCompanionDashed (true);
		mFilterScope[i]->setCaption ("at the knobs");
		frame->addView (mFilterScope[i]);

		// The VCA envelope (solid, filled) over the VCF envelope (dashed),
		// on one time axis - traceDrumEnvelopes() draws both.
		mEnvScope[i] = new Panel::Scope (toCRect (laneRect (Lane::kEnvScreen, top)));
		mEnvScope[i]->setColours (Pal::accent (Accent::Amp), Pal::accent (Accent::Filter));
		mEnvScope[i]->setFill (true);
		mEnvScope[i]->setCompanionDashed (true);
		mEnvScope[i]->setCaptionColour (Pal::accent (Accent::Filter));
		frame->addView (mEnvScope[i]);

		mSelfOsc[i] = new Panel::Lamp (toCRect (laneRect (Lane::kSelfOscLamp, top)), "Self-osc");
		frame->addView (mSelfOsc[i]);

		mVelocityLine[i] = addTextLine (laneRect (Lane::kVelocityLine, top), Panel::fontValue (),
		                                Pal::kSubtle, kRightText);
	}

	// ---- the header and the mix column's lines --------------------------
	mEngineLine = addTextLine (kEngineLine, Panel::fontSubtitle (), Pal::kSubtle, kLeftText);
	mMixLine = addTextLine (kMixGainsLine, Panel::fontValue (), Pal::kMuted, kCenterText);
	mStrikeVelLine = addTextLine (kStrikeVelLine, Panel::fontValue (), Pal::kSubtle, kCenterText);

	mStrikeLamp[0] = new Panel::Lamp (toCRect (kStrikeLamp1), "D1");
	mStrikeLamp[1] = new Panel::Lamp (toCRect (kStrikeLamp2), "D2");
	for (auto* lamp : mStrikeLamp)
	{
		lamp->setColour (Pal::accent (Accent::Noise));
		frame->addView (lamp);
	}

	// ---- every control, from the table ----------------------------------
	for (const auto& c : kControls)
		addControl (c);

	refreshAllReadouts ();

	return true;
}

//------------------------------------------------------------------------
void FilterDrumEditor::addControl (const Layout::Control& c)
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
			auto* sw = new Panel::SegmentSwitch (r, this, tag, c.accent);
			std::vector<std::string> names;
			for (int i = 0; i < kLaunchDivisionCount; ++i)
				names.push_back (divisionShortName (divisionFromIndex (i)));
			sw->setNames (names);
			registerControl (c.tag, sw);
			break;
		}

		case Kind::Step:
		{
			char number[8] = {};
			std::snprintf (number, sizeof (number), "%d", static_cast<int> (c.tag - kStep1) + 1);
			const int step = static_cast<int> (c.tag - kStep1);
			auto* pad = new Panel::StepPad (r, this, tag, c.accent, number);
			// THE DISABLE IS A SECOND PARAMETER on the same pad - see
			// StepPad. It has no control of its own in the layout.
			pad->setDisabled (normalizedOf (stepDisableParam (step)) >= 0.5);
			pad->setDisableHandler ([this, step] () { toggleStepDisabled (step); });
			registerControl (c.tag, pad);
			break;
		}

		case Kind::Toggle:
		{
			auto* button = new Panel::ToggleButton (r, this, tag, c.accent, label);
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
	if (!isTableParam (tag))
		return std::string ();

	const ParamDef& def = paramDef (tag);
	const double normalized = normalizedOf (tag);
	const double plain = def.toPlain (normalized);

	char text[64] = {};

	int drum = 0;
	ParamID base = tag;
	splitDrumParam (tag, drum, base);

	switch (base)
	{
		case kCutoff:
			if (plain >= 1000.0)
				std::snprintf (text, sizeof (text), "%.2f kHz", plain / 1000.0);
			else
				std::snprintf (text, sizeof (text), "%.0f Hz", plain);
			break;

		case kResonance:
			// PER CENT AND K: the per cent is the knob, K is the number
			// the MS-20 literature quotes - self-oscillation at 2.
			std::snprintf (text, sizeof (text), "%.0f%%  K%.2f", plain, def.toInternal (normalized));
			break;

		case kVcfAmount:
			// OCTAVES, signed, because octaves is what it does.
			std::snprintf (text, sizeof (text), "%+.1f oct", def.toInternal (normalized));
			break;

		case kOutputTrim:
			std::snprintf (text, sizeof (text), "%+.1f dB", plain);
			break;

		case kMix:
			if (plain >= 99.5)
				std::snprintf (text, sizeof (text), "D1 only");
			else if (plain <= 0.5)
				std::snprintf (text, sizeof (text), "D2 only");
			else
				std::snprintf (text, sizeof (text), "%.0f%%", plain);
			break;

		case kNoiseLevel:
			// AT ZERO IT SAYS SO IN WORDS: the noise is the voice's only
			// excitation, so at 0 the drum is silent however hard it is
			// played - and the panel should say that is the knob.
			if (plain <= 0.0)
				std::snprintf (text, sizeof (text), "silent");
			else
				std::snprintf (text, sizeof (text), "%.0f%%", plain);
			break;

		case kSeqRun:
			// Armed is the state worth showing: the pattern waits for its
			// launch line, which can be most of a bar away.
			std::snprintf (text, sizeof (text), "%s",
			               (normalized < 0.5) ? "off" : ((mPlayhead >= 0) ? "running" : "armed"));
			break;

		case kSeqDivision:
			std::snprintf (text, sizeof (text), "%s",
			               divisionShortName (divisionFromIndex (
			                   static_cast<int> (def.toInternal (normalized) + 0.5))));
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
			else if (std::strcmp (def.units, "%") == 0)
				std::snprintf (text, sizeof (text), "%.0f%%", plain);
			else
				std::snprintf (text, sizeof (text), "%.0f %s", plain, def.units);
			break;
	}

	return std::string (text);
}

//------------------------------------------------------------------------
std::string FilterDrumEditor::velocityLineFor (int drum) const
{
	// velocityScaled() IS THE SHARED LAW - the call each voice latches
	// its amounts with. Velocity arrives from VST3 already normalised, so
	// 1.0 is MIDI 127.
	const double octaves = internalOf (drum, kVcfAmount);
	const double vcfSens = internalOf (drum, kVcfVelocity);
	const double gain    = internalOf (drum, kVcaAmount);
	const double vcaSens = internalOf (drum, kVcaVelocity);

	std::string line = "velocity";
	const struct { const char* name; double v; } points[] = {
		{ "127", 1.0 }, { "64", 64.0 / 127.0 }, { "0", 0.0 } };

	for (const auto& p : points)
	{
		const double o = velocityScaled (octaves, vcfSens, p.v);
		const double g = velocityScaled (gain, vcaSens, p.v);
		char text[64] = {};
		if (std::fabs (o) < 0.05 && g < 0.005)
			std::snprintf (text, sizeof (text), "    %s: off", p.name);
		else
			std::snprintf (text, sizeof (text), "    %s: %+.1f oct %s %.0f%%", p.name, o, kMiddleDot, g * 100.0);
		line += text;
	}
	return line;
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
		mVelocityLine[i]->setText (velocityLineFor (drum).c_str ());
		mVelocityLine[i]->invalid ();
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
	scope->setAnnotation ("strike " + hzText (peakHz));
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

	char caption[32] = {};
	std::snprintf (caption, sizeof (caption), "VCF %+.1f oct", octaves);

	// The span in the units the knobs are in.
	char annotation[24] = {};
	if (span >= 1.0)
		std::snprintf (annotation, sizeof (annotation), "%.2f s", span);
	else
		std::snprintf (annotation, sizeof (annotation), "%.0f ms", span * 1000.0);

	scope->setCaption (caption);
	scope->setAnnotation (annotation);
	scope->setCurves (vcaCurve, vcfCurve, kDisplayPoints);
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshMixLine ()
{
	if (!mMixLine)
		return;

	// The two gains crossfadeGainDrum1/2() apply - the audio path's own
	// calls - so the numbers under the knob are the gains in use.
	const double mix = paramDef (kMix).toInternal (normalizedOf (kMix));
	const std::string text = gainText (crossfadeGainDrum1 (mix)) + "  " + kMiddleDot + "  "
	                         + gainText (crossfadeGainDrum2 (mix));
	mMixLine->setText (text.c_str ());
	mMixLine->invalid ();
}

//------------------------------------------------------------------------
void FilterDrumEditor::refreshEngineLine ()
{
	if (!mEngineLine)
		return;

	// The cutoff CEILING is a fraction of the sample rate, so it is shown
	// next to the rate: at 44.1 k the Cutoff knob's top 20 kHz is not
	// reachable, and that is a question people ask rather than a bug.
	const double rate = sampleRate ();
	char text[96] = {};
	std::snprintf (text, sizeof (text), "Engine: %.0f Hz      cutoff ceiling %.0f Hz",
	               rate, rate * kMaxCutoffFraction);
	mEngineLine->setText (text);
	mEngineLine->invalid ();
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
		mStrikeVelLine->setText ("vel -");
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
	{
		char text[24] = {};
		std::snprintf (text, sizeof (text), "vel %d", static_cast<int> (velocity * 127.0 + 0.5));
		mStrikeVelLine->setText (text);
		mStrikeVelLine->invalid ();
	}

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
