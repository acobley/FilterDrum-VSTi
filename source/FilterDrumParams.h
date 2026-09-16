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

	// ---- drum 2 -------------------------------------------------------
	/** THE SECOND DRUM, appended whole.

	    Same eleven parameters as drum 1, in the same order, so
	    paramDef(kCutoff2 - kDrum2Offset) is drum 1's Cutoff and the two
	    blocks can be walked with one loop. That regularity is worth more
	    than grouping them by function would be: the processor wires all
	    twenty-two with a single range test rather than twenty-two case
	    labels, so drum 2's release cannot be wired to drum 1's. */
	kCutoff2,
	kResonance2,
	kVcfAttack2,
	kVcfRelease2,
	kVcfAmount2,
	kVcfVelocity2,
	kVcaAttack2,
	kVcaRelease2,
	kVcaAmount2,
	kVcaVelocity2,
	kNoiseLevel2,

	// ---- output -------------------------------------------------------
	/** The crossfader. 100 % is all drum 1, 0 % is all drum 2, and the
	    panel draws it vertically with drum 1 at the top - which is also
	    the top block on the panel, so the fader reads the way the voices
	    are laid out. Constant power; see crossfadeGainDrum1() in
	    FilterDrumDsp.h. */
	kMix,

	// ---- the sequencer ------------------------------------------------
	/** Sixteen step switches, appended as a contiguous block so the
	    processor and the panel can both walk them with one loop.
	    kStep1 + n is step n. */
	kStep1,  kStep2,  kStep3,  kStep4,
	kStep5,  kStep6,  kStep7,  kStep8,
	kStep9,  kStep10, kStep11, kStep12,
	kStep13, kStep14, kStep15, kStep16,

	/** Run. Switching it on ARMS the sequencer; it starts at the next
	    line the launch division fires on. Switching it off stops it at
	    once. */
	kSeqRun,

	/** When an armed sequencer starts: bar, 1/2, 1/4, 1/8 or 1/16 of a
	    bar. Fractions of a BAR rather than note values, which only
	    matters outside 4/4 - see FilterDrumTransport.h. */
	kSeqDivision,

	// ---- envelope shapes ----------------------------------------------
	/** FOUR SHAPE CONTROLS PER DRUM, appended as their own two blocks
	    rather than folded into the drum blocks where they belong.

	    They belong next to kVcfAttack and the rest. Putting them there
	    would have pushed every drum-2 id up by four, and a host stores
	    automation against the id - so drum 2's Cutoff lane would have
	    started driving its VCF Attack in every project already saved.
	    That is the same append rule kNoiseLevel is already an example
	    of, applied to eight parameters instead of one.

	    The cost is that drum 2's shapes are NOT kDrum2Offset away from
	    drum 1's, so there are now two offsets. drumParam() below is the
	    one place that knows which to use; nothing else should be adding
	    an offset by hand. */
	kVcfAttackShape,     // % -> -1 Exponential .. +1 Logarithmic
	kVcfReleaseShape,
	kVcaAttackShape,
	kVcaReleaseShape,

	kVcfAttackShape2,
	kVcfReleaseShape2,
	kVcaAttackShape2,
	kVcaReleaseShape2,

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
	kBypass = 1000,

	//--------------------------------------------------------------------
	/** READ-ONLY, processor -> panel: which step the playhead is on,
	    or -1 when the sequencer is not playing.

	    THE FIRST USE OF THE SPACE RESERVED AT 1001 SINCE THE SCAFFOLD,
	    and it is the case that space was reserved for. The playhead is a
	    value the DSP works out per block and the panel needs; a
	    sendMessage from process() would be silently discarded by the
	    host's connection proxy, whereas data.outputParameterChanges is
	    delivered on the UI thread and cannot be lost.
	
	    ONE parameter and not sixteen. Sixteen continuously-changing
	    parameters would put thousands of points a second into a host's
	    automation queue to light lamps that redraw at thirty frames;
	    Project6 reached for a request-and-reply message pair rather than
	    do that for its sixty-four progress bars. One step index is small
	    enough to publish, and only changes on grid lines.
	
	    Carried as a NORMALISED value over 0..kStepCount, with 0 meaning
	    "not playing" - see playheadToNormalized below, which both sides
	    call so they cannot disagree about the encoding. */
	kPlayheadOut = 1001
};

//------------------------------------------------------------------------
/** The encoding of kPlayheadOut, written once so the processor and the
    panel cannot disagree about it.

    -1 (not playing) is 0.0, and step n is (n + 1) / 17. A scheme where
    step 0 was 0.0 would make "not playing" and "on the first step"
    indistinguishable, and the lamp would sit lit on step 1 whenever the
    sequencer stopped. */
inline double playheadToNormalized (int step)
{
	if (step < 0 || step >= 16)
		return 0.0;
	return static_cast<double> (step + 1) / 17.0;
}

inline int playheadFromNormalized (double normalized)
{
	const int raw = static_cast<int> (normalized * 17.0 + 0.5) - 1;
	return (raw < 0 || raw >= 16) ? -1 : raw;
}

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
/** How far drum 2's block sits after drum 1's.

    kCutoff2 - kDrum2Offset == kCutoff, and so on for all eleven. This is
    what lets the processor route every per-drum parameter with one range
    test instead of twenty-two case labels - and a test asserts the
    identity for every id in the block, so the two lists cannot drift. */
constexpr Steinberg::Vst::ParamID kDrum2Offset = kCutoff2 - kCutoff;

/** The first and last ids of drum 1's block, for that range test. */
constexpr Steinberg::Vst::ParamID kDrum1First = kCutoff;
constexpr Steinberg::Vst::ParamID kDrum1Last  = kNoiseLevel;

/** The SECOND per-drum block - the four shapes - and its own offset,
    which is four and not eleven. See the note on kVcfAttackShape. */
constexpr Steinberg::Vst::ParamID kShape1First = kVcfAttackShape;
constexpr Steinberg::Vst::ParamID kShape1Last  = kVcaReleaseShape;
constexpr Steinberg::Vst::ParamID kShapeDrum2Offset =
    kVcfAttackShape2 - kVcfAttackShape;

/** Is this one of drum 1's four shape ids? The predicate drumParam()
    switches on, kept separate so the two callers that need to ask
    directly do not repeat the range test. */
inline bool isShapeParam (Steinberg::Vst::ParamID base)
{
	return base >= kShape1First && base <= kShape1Last;
}

/** DRUM 1'S ID FOR A PARAMETER, TRANSLATED TO THIS DRUM.

    The only supported way to go from a base id to drum 2's, now that
    there are two blocks with two different offsets. Writing `id +
    kDrum2Offset` by hand works for eleven parameters and silently
    produces a step switch id for the other four. */
inline Steinberg::Vst::ParamID drumParam (Steinberg::Vst::ParamID base, int drum)
{
	if (drum != 2)
		return base;
	return base + (isShapeParam (base) ? kShapeDrum2Offset : kDrum2Offset);
}

//------------------------------------------------------------------------
// THE TWO BLOCKS MUST STAY IN THE SAME ORDER, and these fail the BUILD
// rather than a test if they ever do not.
//
// Everything downstream leans on the offset: the processor routes
// twenty-two parameters through eleven case labels, and the editor lays
// out both rows from one function. Reorder drum 2's enum by one line and
// all of that quietly wires Cutoff to Resonance - it compiles, it runs,
// and it sounds almost right. A static_assert is the only check that
// cannot be forgotten to run, and it costs nothing.
//------------------------------------------------------------------------
static_assert (kCutoff2      - kDrum2Offset == kCutoff,      "drum 2 block order");
static_assert (kResonance2   - kDrum2Offset == kResonance,   "drum 2 block order");
static_assert (kVcfAttack2   - kDrum2Offset == kVcfAttack,   "drum 2 block order");
static_assert (kVcfRelease2  - kDrum2Offset == kVcfRelease,  "drum 2 block order");
static_assert (kVcfAmount2   - kDrum2Offset == kVcfAmount,   "drum 2 block order");
static_assert (kVcfVelocity2 - kDrum2Offset == kVcfVelocity, "drum 2 block order");
static_assert (kVcaAttack2   - kDrum2Offset == kVcaAttack,   "drum 2 block order");
static_assert (kVcaRelease2  - kDrum2Offset == kVcaRelease,  "drum 2 block order");
static_assert (kVcaAmount2   - kDrum2Offset == kVcaAmount,   "drum 2 block order");
static_assert (kVcaVelocity2 - kDrum2Offset == kVcaVelocity, "drum 2 block order");
static_assert (kNoiseLevel2  - kDrum2Offset == kNoiseLevel,  "drum 2 block order");

/** Eleven per drum, twenty-two in all, plus the trim, the mix, sixteen
    steps, Run and the launch division. */
static_assert (kDrum1Last - kDrum1First + 1 == 11, "eleven parameters per drum");

/** The shape block, on the same terms as the drum block above. */
static_assert (kVcfReleaseShape2 - kShapeDrum2Offset == kVcfReleaseShape, "shape block order");
static_assert (kVcaAttackShape2  - kShapeDrum2Offset == kVcaAttackShape,  "shape block order");
static_assert (kVcaReleaseShape2 - kShapeDrum2Offset == kVcaReleaseShape, "shape block order");
static_assert (kShape1Last - kShape1First + 1 == 4, "four shapes per drum");
static_assert (kShapeDrum2Offset == 4, "and drum 2's shapes are four away, not eleven");

/** THE TWO BLOCKS MUST NOT OVERLAP, or drumParam() would translate an
    id into the wrong block and the processor would wire a shape knob to
    a cutoff. They cannot, given the enum order - but the enum order is
    exactly what an edit changes. */
static_assert (kShape1First > kDrum1Last + kDrum2Offset,
               "the shape block must sit past both drum blocks");

static_assert (kNumParams == 50,
               "42 parameters: 2 x 11, trim, mix, 16 steps, run, division");

/** The step block is contiguous and in order, so kStep1 + n is step n.
    The processor and the panel both rely on that. */
static_assert (kStep16 - kStep1 + 1 == 16, "sixteen contiguous step parameters");
static_assert (kStep8 - kStep1 == 7, "and they are in order");

/** Which drum an id belongs to, and what it does. Returns false for
    anything that is not a per-drum parameter - the trim, the mix,
    kBypass, a garbled id from a broken host. */
inline bool splitDrumParam (Steinberg::Vst::ParamID id,
                            int& drumOut, Steinberg::Vst::ParamID& baseOut)
{
	if (id >= kDrum1First && id <= kDrum1Last)
	{
		drumOut = 1;
		baseOut = id;
		return true;
	}
	if (id >= kDrum1First + kDrum2Offset && id <= kDrum1Last + kDrum2Offset)
	{
		drumOut = 2;
		baseOut = id - kDrum2Offset;
		return true;
	}

	// THE SECOND BLOCK, four wide. Appended after the sequencer, so it
	// is nowhere near the first and carries its own offset.
	if (id >= kShape1First && id <= kShape1Last)
	{
		drumOut = 1;
		baseOut = id;
		return true;
	}
	if (id >= kShape1First + kShapeDrum2Offset && id <= kShape1Last + kShapeDrum2Offset)
	{
		drumOut = 2;
		baseOut = id - kShapeDrum2Offset;
		return true;
	}

	return false;
}

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
