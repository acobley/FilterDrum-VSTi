//------------------------------------------------------------------------
// FilterDrum - every piece of text the panel shows, as functions
//
// MOVED OUT OF THE EDITOR (branch PolivoksUI, Oct 2026), as R2Synth's
// R2SynthReadouts.* were. This file includes NO VSTGUI, so three things
// call the same code:
//
//   * FilterDrumEditor, for the panel;
//   * tests/LayoutTests.cpp, which sweeps every parameter through
//     readoutText() and holds the WIDEST reading to its knob's width in
//     the 70s sci-fi lettering - no hand-kept list of widest readings;
//   * tools/dump-layout.cpp, for docs/panel.png - which used to keep a
//     second copy of the editor's formats ("THE ONE COPY") and now has
//     none.
//
// Every number still comes from the parameter table or from a function
// in FilterDrumDsp.h that the AUDIO PATH also calls - velocityScaled(),
// crossfadeGain*(), the ceiling kMaxCutoffFraction. Nothing here does
// that arithmetic a second time.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumParams.h"

#include <functional>
#include <string>

namespace FilterDrum {

/** A drum parameter's value in INTERNAL units (seconds, octaves, K,
    linear gain), for drum 1 or 2 - the editor reads its controls, the
    tools read the table's defaults. */
using InternalOf = std::function<double (int drum, Steinberg::Vst::ParamID base)>;

/** THE READING UNDER A CONTROL at `normalized`. `running` is for Run's
    status line: on and the playhead moving ("running") rather than on
    and waiting for its launch line ("armed"). Empty for an id that is
    not in the table. */
std::string readoutText (Steinberg::Vst::ParamID tag, double normalized, bool running = false);

/** A LAUNCH ON position's printed name: "Bar", then 1/2 .. 1/16. */
std::string launchPositionName (int index);

/** One column's foot line: what the drum's two Amount knobs become at
    velocity 127, 64 and 0, through velocityScaled() -
    "vel 127: +3.6 oct / 100%    64: +1.8 oct / 50%    0: off". */
std::string velocityLineText (const InternalOf& internalOf, int drum);

/** The gains crossfadeGainDrum1/2() apply at `mix` (internal, 0..1),
    and the law: ".71  /  .71   constant power". */
std::string mixGainsText (double mix);

/** The header's engine line: the rate, and the cutoff ceiling it sets. */
std::string engineLineText (double sampleRate);

/** The filter screen's corner: where a full-velocity hit takes the
    cutoff - "peak 9.7k", "peak 800 Hz". */
std::string filterPeakText (double peakHz);

/** The envelope screen's caption, the VCF envelope's depth: "VCF +3.6 oct". */
std::string envCaptionText (double octaves);

/** The envelope screen's corner, the time the screen spans: "160 ms", "1.20 s". */
std::string envSpanText (double seconds);

/** The spine's last-strike line: "last strike  vel 127", or
    "last strike  -" before anything has been struck (velocity < 0). */
std::string lastStrikeText (double velocity);

/** ".71" for 0.71 - a gain under one reads in half the width. */
std::string gainText (double gain);

} // namespace FilterDrum
