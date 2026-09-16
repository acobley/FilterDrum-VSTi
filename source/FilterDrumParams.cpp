//------------------------------------------------------------------------
// FilterDrum - parameter table
//
// Column order matches the DXi ports in this folder, which in turn
// matched CMediaParams::m_aParamInfo:
//   id, title, units, type, plain min/max/default, internal min/max,
//   step count, smoothed
//
// TITLE AND UNITS MUST NEVER BE NULL. RangeParameter hands both straight
// to UString::assign, which dereferences without a null check; the
// symptom is not a crash in the plug-in but the SDK VALIDATOR
// segfaulting in the post-build step, which sends you looking in
// entirely the wrong place. Empty string, never null.
//
// EVERY Log PARAMETER NEEDS plainMin > 0 AND internalMin > 0, because
// the mapping is a ratio. There is no runtime check: a zero would give
// a silent inf rather than an error, so it is caught in
// tests/DspTests.cpp instead, where it is checked for every row of the
// table at once.
//------------------------------------------------------------------------

#include "FilterDrumParams.h"

namespace FilterDrum {

//------------------------------------------------------------------------
const ParamDef kParams[kNumParams] = {
//   id              title              units  type               pMin    pMax    pDef   iMin     iMax    steps smooth

	{kOutputTrim,   "Output Trim",     "dB",  ParamType::Float,  -24.,   12.,    0.,    -24.,    12.,    0,    true },

	// ---- VCF ----------------------------------------------------------
	// Cutoff is logarithmic and internal == plain: the DSP wants Hz too.
	{kCutoff,       "Cutoff",          "Hz",  ParamType::Log,      20.,   20000., 800.,   20.,    20000., 0,    true },

	// Resonance's internal range is the MS-20 FEEDBACK GAIN K. 2.0 is
	// the self-oscillation threshold, so the top of the knob (2.4) sits
	// past it - the panel's lamp lights at 83 %, from
	// selfOscillating() in FilterDrumDsp.h.
	{kResonance,    "Resonance",       "%",   ParamType::Float,     0.,   100.,   40.,     0.,     2.4,   0,    true },

	// Times: milliseconds on the panel, SECONDS to the DSP. Log, so the
	// short end where drums live gets most of the travel.
	{kVcfAttack,    "VCF Attack",      "ms",  ParamType::Log,       0.1,  1000.,   1.,     0.0001, 1.0,   0,    false},
	{kVcfRelease,   "VCF Release",     "ms",  ParamType::Log,       1.,   4000.,  120.,    0.001,  4.0,   0,    false},

	// Amount is SIGNED, and the negative half is not decoration: a
	// positive amount opens the filter on the attack and closes it
	// through the release, which is the down-sweep a kick wants, and a
	// negative one does the reverse for a reverse-sweep zap. Internal
	// units are OCTAVES, +/- kMaxEnvOctaves.
	{kVcfAmount,    "VCF Amount",      "%",   ParamType::Float,  -100.,   100.,   60.,    -6.,      6.,   0,    false},

	// Velocity sensitivity, 0..1 to the DSP. 100 % means velocity 127
	// gives the full amount and velocity 0 gives none; 0 % means
	// velocity is ignored. See velocityScaled() in FilterDrumDsp.h.
	{kVcfVelocity,  "VCF Velocity",    "%",   ParamType::Float,     0.,   100.,  100.,     0.,      1.,   0,    false},

	// ---- VCA ----------------------------------------------------------
	{kVcaAttack,    "VCA Attack",      "ms",  ParamType::Log,       0.1,  1000.,   1.,     0.0001, 1.0,   0,    false},
	{kVcaRelease,   "VCA Release",     "ms",  ParamType::Log,       1.,   4000.,  150.,    0.001,  4.0,   0,    false},

	// The VCA's amount IS the voice level - internal units are a linear
	// gain. At 0 the voice is silent however hard it is played, which is
	// the correct reading of "0 velocity gives 0 amount" taken to the
	// knob.
	{kVcaAmount,    "VCA Amount",      "%",   ParamType::Float,     0.,   100.,  100.,     0.,      1.,   0,    false},
	{kVcaVelocity,  "VCA Velocity",    "%",   ParamType::Float,     0.,   100.,  100.,     0.,      1.,   0,    false},

	// ---- source -------------------------------------------------------
	// How much noise the filter is fed, as a linear gain. DEFAULT 100 %,
	// which is what the plug-in did before this knob existed - a new
	// parameter whose default changes the sound silently rewrites every
	// preset made before it.
	//
	// At 0 the per-note trigger ping is the only excitation left, which
	// is the pure-tone setting rather than an off switch - see
	// kTriggerCharge in FilterDrumDsp.h.
	{kNoiseLevel,   "Noise Level",     "%",   ParamType::Float,     0.,   100.,  100.,     0.,      1.,   0,    true },

	// ---- drum 2 -------------------------------------------------------
	//
	// THE SAME ELEVEN, IN THE SAME ORDER, and that regularity is load
	// bearing rather than tidy: kDrum2Offset and splitDrumParam() in the
	// header depend on it, and a block of static_asserts there fails the
	// BUILD if a row ever stops matching its twin above. (It cannot be
	// checked from tests/DspTests.cpp - that file is SDK-free and this
	// table is not.)
	//
	// THE DEFAULTS ARE NOT IDENTICAL. Two drums with the same settings
	// are one drum 6 dB louder, so an out-of-the-box patch where the
	// pair does nothing would look broken. Drum 2 is voiced as the SNAP
	// over drum 1's body: higher cutoff, more resonance, much shorter
	// decay. The architecture is identical; only the numbers differ, and
	// every one of them is reachable from drum 1's own range.
	{kCutoff2,      "Cutoff 2",        "Hz",  ParamType::Log,      20.,   20000., 2400.,  20.,    20000., 0,    true },
	{kResonance2,   "Resonance 2",     "%",   ParamType::Float,     0.,   100.,   62.,     0.,     2.4,   0,    true },
	{kVcfAttack2,   "VCF 2 Attack",    "ms",  ParamType::Log,       0.1,  1000.,   0.5,    0.0001, 1.0,   0,    false},
	{kVcfRelease2,  "VCF 2 Release",   "ms",  ParamType::Log,       1.,   4000.,  45.,     0.001,  4.0,   0,    false},
	{kVcfAmount2,   "VCF 2 Amount",    "%",   ParamType::Float,  -100.,   100.,   35.,    -6.,      6.,   0,    false},
	{kVcfVelocity2, "VCF 2 Velocity",  "%",   ParamType::Float,     0.,   100.,  100.,     0.,      1.,   0,    false},
	{kVcaAttack2,   "VCA 2 Attack",    "ms",  ParamType::Log,       0.1,  1000.,   0.5,    0.0001, 1.0,   0,    false},
	{kVcaRelease2,  "VCA 2 Release",   "ms",  ParamType::Log,       1.,   4000.,  60.,     0.001,  4.0,   0,    false},
	{kVcaAmount2,   "VCA 2 Amount",    "%",   ParamType::Float,     0.,   100.,  100.,     0.,      1.,   0,    false},
	{kVcaVelocity2, "VCA 2 Velocity",  "%",   ParamType::Float,     0.,   100.,  100.,     0.,      1.,   0,    false},
	{kNoiseLevel2,  "Noise Level 2",   "%",   ParamType::Float,     0.,   100.,  100.,     0.,      1.,   0,    true },

	// ---- output -------------------------------------------------------
	//
	// The crossfader. 100 % is all drum 1 and 0 % is all drum 2; the
	// panel draws it vertically with drum 1 at the top, which is also
	// the top block on the panel.
	//
	// DEFAULT 50 %, an even blend, because that is the setting where a
	// new user hears that there are two drums at all. Constant power, so
	// the centre does not dip - see crossfadeGainDrum1() in
	// FilterDrumDsp.h.
	{kMix,          "Mix D1/D2",       "%",   ParamType::Float,     0.,   100.,   50.,     0.,      1.,   0,    true },

	// ---- the sequencer ------------------------------------------------
	//
	// Sixteen Bool steps. THE DEFAULT PATTERN IS FOUR ON THE FLOOR -
	// steps 1, 5, 9 and 13 - rather than empty. An empty pattern with the
	// Run switch off is two things a new user has to find before the
	// sequencer does anything at all, and a plug-in that appears to do
	// nothing is one nobody investigates. Run still defaults to OFF, so
	// nothing plays until it is asked to.
	{kStep1,        "Step 1",          "",    ParamType::Bool,      0.,   1.,     1.,      0.,      1.,   1,    false},
	{kStep2,        "Step 2",          "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep3,        "Step 3",          "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep4,        "Step 4",          "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep5,        "Step 5",          "",    ParamType::Bool,      0.,   1.,     1.,      0.,      1.,   1,    false},
	{kStep6,        "Step 6",          "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep7,        "Step 7",          "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep8,        "Step 8",          "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep9,        "Step 9",          "",    ParamType::Bool,      0.,   1.,     1.,      0.,      1.,   1,    false},
	{kStep10,       "Step 10",         "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep11,       "Step 11",         "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep12,       "Step 12",         "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep13,       "Step 13",         "",    ParamType::Bool,      0.,   1.,     1.,      0.,      1.,   1,    false},
	{kStep14,       "Step 14",         "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep15,       "Step 15",         "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},
	{kStep16,       "Step 16",         "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},

	// OFF by default: a sequencer that starts playing the moment the
	// plug-in is loaded is a plug-in that plays over whatever the user
	// was listening to.
	{kSeqRun,       "Sequencer Run",   "",    ParamType::Bool,      0.,   1.,     0.,      0.,      1.,   1,    false},

	// The launch division, as an Enum with kLaunchDivisionCount choices.
	// Defaults to the BAR - the coarsest and least surprising: a
	// sequencer that starts on the downbeat is what pressing Run is
	// expected to do.
	{kSeqDivision,  "Launch On",       "",    ParamType::Enum,      0.,   4.,     0.,      0.,      4.,   4,    false},

	// ---- envelope shapes ----------------------------------------------
	//
	// SIGNED, and the sign is the whole readout: -100 is Exponential,
	// 0 is Linear, +100 is Logarithmic. A 0..100 range would have put
	// Linear at an unmemorable 50 and given the control no centre to
	// snap to; signed, the middle of the travel is the straight line.
	//
	// DEFAULT -100, THE EXPONENTIAL END, because that is what this
	// plug-in did before these controls existed. The whole point of
	// choosing the RC curve family was that the old envelope is a member
	// of it, so a patch saved before this build sounds the same after
	// it - see testShapedEnvelope, which asserts the release against a
	// copy of the old one-pole. A default of Linear would have been
	// tidier and would have restyled every existing patch.
	//
	// Internal units are -1..+1; shapeToCurve() in FilterDrumDsp.h turns
	// that into the curvature, and it is the only place the sign flip
	// between "control" and "curve" lives.
	{kVcfAttackShape,  "VCF Atk Shape",  "",  ParamType::Float,  -100., 100., -100.,   -1.,     1.,   0,    false},
	{kVcfReleaseShape, "VCF Rel Shape",  "",  ParamType::Float,  -100., 100., -100.,   -1.,     1.,   0,    false},
	{kVcaAttackShape,  "VCA Atk Shape",  "",  ParamType::Float,  -100., 100., -100.,   -1.,     1.,   0,    false},
	{kVcaReleaseShape, "VCA Rel Shape",  "",  ParamType::Float,  -100., 100., -100.,   -1.,     1.,   0,    false},

	{kVcfAttackShape2,  "VCF 2 Atk Shape",  "", ParamType::Float, -100., 100., -100.,  -1.,     1.,   0,    false},
	{kVcfReleaseShape2, "VCF 2 Rel Shape",  "", ParamType::Float, -100., 100., -100.,  -1.,     1.,   0,    false},
	{kVcaAttackShape2,  "VCA 2 Atk Shape",  "", ParamType::Float, -100., 100., -100.,  -1.,     1.,   0,    false},
	{kVcaReleaseShape2, "VCA 2 Rel Shape",  "", ParamType::Float, -100., 100., -100.,  -1.,     1.,   0,    false},
};

//------------------------------------------------------------------------
} // namespace FilterDrum
