//------------------------------------------------------------------------
// FilterDrum - parameter definitions
//
// THREE RANGES PER PARAMETER:
//
//   normalised   0..1, what VST3 automates and what the host stores
//   plain        what the USER sees - the number on the panel and in
//                the host's own generic editor
//   internal     what the DSP is handed, via toInternal()
//
// The shape is inherited from the DXi ports in this folder, where the
// last two were explicit - an "external" range in Parameters.h and an
// "internal" one reached through CParamEnvelope::MapToInternal.
//
// THE INTERNAL RANGE IS NOW DOING REAL WORK, which it was not when this
// project was a blank scaffold. The DSP takes SECONDS where the panel
// says milliseconds, OCTAVES where the panel says per cent, the MS-20
// feedback gain K where the panel says per cent of resonance, and a
// linear gain where the panel says per cent of level. Every one of
// those conversions happens in toInternal() and nowhere else, so there
// is exactly one place a unit can be got wrong.
//
// Note what is NOT a range mapping and does not belong here: dB to
// linear gain, the velocity law, and the envelope-to-cutoff
// exponential. Those are the DSP's arithmetic, they live in
// FilterDrumDsp.h, and the editor calls the same copies the audio path
// does.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/vst/vsttypes.h"

#include <cmath>

namespace FilterDrum {

//------------------------------------------------------------------------
enum Param : Steinberg::Vst::ParamID
{
	/** Output trim, dB. The one parameter the blank scaffold shipped
	    with, and it keeps id 0 - see the append rule below. */
	kOutputTrim = 0,

	// ---- VCF: the MS-20 lowpass ---------------------------------------
	kCutoff,            // Hz, logarithmic
	kResonance,         // % of travel -> MS-20 feedback gain K, 0..2.4

	kVcfAttack,         // ms -> s
	kVcfRelease,        // ms -> s
	kVcfAmount,         // % -> octaves of cutoff, SIGNED
	kVcfVelocity,       // % -> 0..1 sensitivity

	// ---- VCA ----------------------------------------------------------
	kVcaAttack,         // ms -> s
	kVcaRelease,        // ms -> s
	kVcaAmount,         // % -> linear gain, the voice level
	kVcaVelocity,       // % -> 0..1 sensitivity

	// ---- source -------------------------------------------------------
	/** How much noise reaches the filter, % -> 0..1 linear.

	    APPENDED HERE RATHER THAN PUT WITH THE VCF, where it belongs on
	    the panel and where it reads far better in this list. It cannot
	    move: inserting it at the front would renumber all ten
	    parameters after it, and every project saved by the previous
	    build would restore its values into the wrong ones - quietly.
	    The panel puts it where it belongs; the table records where it
	    arrived. */
	kNoiseLevel,

	kNumParams,

	//--------------------------------------------------------------------
	/** Standard VST3 bypass.

	    1000, DELIBERATELY FAR PAST THE END OF THE TABLE, and this is the
	    trap it exists to make obvious. kParams is indexed by ParamID,
	    and every id that reaches an index must be range-checked first -
	    paramDef() below is the only place that indexing happens, and so
	    it is the only place that check has to be right. An unchecked
	    kParams[kBypass] reads a thousand entries past the end.

	    IDs 1001 upwards are the space for READ-ONLY OUTPUT parameters -
	    the mechanism for a value the DSP computes per block that the
	    panel needs. That is what they are for and not messages: a
	    sendMessage from process() is silently discarded by the host's
	    connection proxy (see FilterDrumIDs.h), whereas
	    data.outputParameterChanges is delivered on the UI thread and
	    cannot be lost. Nothing publishes one yet - the panel's
	    velocity readouts compute from the knobs through the shared
	    velocityScaled(), which needs no traffic from the processor. */
	kBypass = 1000
};

//------------------------------------------------------------------------
/** APPEND, NEVER INSERT.

    A parameter added in the middle renumbers every id after it, and a
    project saved by the old build then restores its values into the
    wrong parameters - quietly, with no error anywhere. New parameters go
    immediately before kNumParams, and a parameter that is no longer
    wanted is left in place and hidden rather than removed.

    The ten VCF and VCA parameters above were appended after
    kOutputTrim for exactly this reason, even though grouping the trim
    with the VCA would have read better. */
constexpr int kFirstFreeParamSlot = kNumParams;

//------------------------------------------------------------------------
enum class ParamType
{
	/** Linear between plainMin and plainMax. */
	Float,

	/** LOGARITHMIC between plainMin and plainMax, both of which must be
	    greater than zero.

	    Needed because the ear is logarithmic and a linear knob is
	    useless for the quantities this plug-in has most of. A cutoff
	    swept linearly from 20 Hz to 20 kHz spends its first two per
	    cent of travel covering the bottom five octaves and the rest
	    covering the top one; an attack time from 0.1 ms to 1 s
	    linearly has every drum-length value in the first thousandth of
	    the knob. */
	Log,

	Bool,
	Enum
};

//------------------------------------------------------------------------
struct ParamDef
{
	Steinberg::Vst::ParamID id;
	const char* title;          // shown by the host - NEVER null
	const char* units;          // likewise
	ParamType   type;
	double      plainMin;       // what the user sees
	double      plainMax;
	double      plainDefault;
	double      internalMin;    // what the DSP is handed
	double      internalMax;
	int         stepCount;      // 0 = continuous
	bool        smoothed;       // interpolated per sample by the DSP

	//--------------------------------------------------------------------
	double toPlain (double normalized) const
	{
		if (type == ParamType::Log)
			return plainMin * std::pow (plainMax / plainMin, normalized);

		return plainMin + normalized * (plainMax - plainMin);
	}

	double toNormalized (double plain) const
	{
		if (type == ParamType::Log)
		{
			if (plain <= plainMin) return 0.0;
			if (plain >= plainMax) return 1.0;
			return std::log (plain / plainMin) / std::log (plainMax / plainMin);
		}

		if (plainMax == plainMin)
			return 0.0;
		return (plain - plainMin) / (plainMax - plainMin);
	}

	/** The DXi's ParamInfo::MapToInternal, generalised.

	    A LOG PARAMETER MAPS LOGARITHMICALLY ON BOTH SCALES, which is
	    what makes the millisecond parameters work out to exactly
	    plain / 1000: the internal range is the plain one scaled by a
	    constant, and an exponential map commutes with that. If a future
	    parameter ever needs a log plain range and a differently-shaped
	    internal one, this is the function that has to grow, not the
	    call sites. */
	double toInternal (double normalized) const
	{
		if (type == ParamType::Bool)
			return (normalized < 0.5) ? 0.0 : 1.0;

		if (type == ParamType::Log)
			return internalMin * std::pow (internalMax / internalMin, normalized);

		const double v = internalMin + normalized * (internalMax - internalMin);
		if (type == ParamType::Enum)
		{
			// Enums were rounded to the nearest step, and the step count
			// is preserved even where two steps show the same name.
			return static_cast<double> (static_cast<long> (v + 0.5));
		}
		return v;
	}

	double defaultNormalized () const { return toNormalized (plainDefault); }
};

//------------------------------------------------------------------------
extern const ParamDef kParams[kNumParams];

/** THE ONLY PLACE kParams IS INDEXED, and therefore the only place the
    range check has to be right. ids are dense from 0 to kNumParams, and
    anything outside that - kBypass, an output parameter, a garbled id
    from a broken host - gets entry 0 rather than a read off the end.

    Callers that must distinguish "not a table parameter" from "the
    first one" use isTableParam() instead. */
inline bool isTableParam (Steinberg::Vst::ParamID id)
{
	return id < static_cast<Steinberg::Vst::ParamID> (kNumParams);
}

inline const ParamDef& paramDef (Steinberg::Vst::ParamID id)
{
	return kParams[isTableParam (id) ? id : 0];
}

//------------------------------------------------------------------------
} // namespace FilterDrum
