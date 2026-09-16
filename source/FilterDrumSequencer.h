//------------------------------------------------------------------------
// FilterDrum - the sixteen step sequencer
//
// SDK-FREE, like the bar clock it sits on top of, and for the same
// reason: deciding whether a step fires is a small state machine with
// several ways to be quietly wrong - a pattern that starts on the wrong
// step, a Run switch that takes effect a bar late, a sequencer that
// keeps playing after the transport stops - and none of those show up as
// a crash. They show up as a pattern that is off by one, once, in
// somebody else's session.
//
// IT HAS NO CLOCK OF ITS OWN. FilterDrumTransport's grid runs at
// sixteenths, so one grid line IS one step: the processor hands over the
// step index that came back from gridLinesInBlock and this says whether
// to strike the drums. That is the whole reason the lifted file was
// changed from eight steps a bar to sixteen.
//
// ARM, THEN LAUNCH, which is Project6's idiom and what "it will start at
// a bar line" means. Switching Run on does not start the pattern; it
// ARMS it, and the pattern begins at the next grid line that the launch
// division fires on. Starting immediately would put the pattern wherever
// the user's finger landed, which is never what anybody wants from a
// step sequencer.
//------------------------------------------------------------------------

#pragma once

#include "FilterDrumTransport.h"

namespace FilterDrum {

//------------------------------------------------------------------------
/** Sixteen steps, one bar of sixteenths in 4/4.

    The count is kGridStepsPerBar and not a number of its own, because
    the two have to agree: a sequencer with more steps than the grid has
    lines would have steps that can never fire. */
constexpr int kStepCount = kGridStepsPerBar;

//------------------------------------------------------------------------
class StepSequencer
{
public:
	//--------------------------------------------------------------------
	// The pattern
	//--------------------------------------------------------------------
	void setStep (int index, bool on);
	bool step (int index) const;

	/** Run on or off. Switching it ON only ARMS the sequencer - see the
	    banner. Switching it OFF stops it at once, because a user who
	    turns a sequencer off expects silence now, not at the next bar. */
	void setRunning (bool run);
	bool running () const { return mRun; }

	void setDivision (LaunchDivision division) { mDivision = division; }
	LaunchDivision division () const { return mDivision; }

	//--------------------------------------------------------------------
	// State
	//--------------------------------------------------------------------

	/** Armed and waiting for a launch line, but not yet playing. The
	    panel shows this - a Run switch that lights instantly while
	    nothing happens for most of a bar looks broken. */
	bool armed () const { return mRun && !mLaunched; }

	/** Playing. */
	bool launched () const { return mRun && mLaunched; }

	/** The step the playhead last fired on, or -1 before the first one.
	    Published to the panel so the lamps can follow it. */
	int playhead () const { return mPlayhead; }

	/** Forget everything but the pattern and the switches.

	    Called when the transport stops, so that pressing play again
	    launches on the next line rather than continuing from wherever
	    the pattern happened to be - and so that a sequencer left armed
	    stays armed. */
	void reset ();

	//--------------------------------------------------------------------
	/** THE ONE DECISION. Given a grid line's step index, say whether the
	    drums are struck.

	    Call it for every line gridLinesInBlock returns, in order. It
	    handles the launch itself: while armed it waits for a step the
	    division fires on, and from then on it plays.

	    Returns false for every line when Run is off, which is what makes
	    the switch immediate. */
	bool lineFires (int gridStep);

private:
	bool mSteps[kStepCount] = {};
	bool mRun = false;
	bool mLaunched = false;
	int  mPlayhead = -1;
	LaunchDivision mDivision = LaunchDivision::Bar;
};

//------------------------------------------------------------------------
} // namespace FilterDrum
