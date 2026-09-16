//------------------------------------------------------------------------
// FilterDrum - the sixteen step sequencer, implementation
//------------------------------------------------------------------------

#include "FilterDrumSequencer.h"

namespace FilterDrum {

//------------------------------------------------------------------------
void StepSequencer::setStep (int index, bool on)
{
	if (index < 0 || index >= kStepCount)
		return;
	mSteps[index] = on;
}

//------------------------------------------------------------------------
bool StepSequencer::step (int index) const
{
	if (index < 0 || index >= kStepCount)
		return false;
	return mSteps[index];
}

//------------------------------------------------------------------------
void StepSequencer::setRunning (bool run)
{
	if (run == mRun)
		return;

	mRun = run;

	// SWITCHING ON ARMS, SWITCHING OFF STOPS. The asymmetry is
	// deliberate: waiting for a bar line before starting is what the
	// user asked for, but waiting for one before STOPPING would mean a
	// sequencer that keeps playing for most of a bar after it has been
	// turned off, which reads as a stuck plug-in.
	mLaunched = false;

	if (!run)
		mPlayhead = -1;
}

//------------------------------------------------------------------------
void StepSequencer::reset ()
{
	// The pattern, the Run switch and the division all survive - they are
	// settings, not state. What goes is where the playhead had got to and
	// whether the arm has fired, so that pressing play again launches on
	// the next line rather than resuming mid-pattern.
	mLaunched = false;
	mPlayhead = -1;
}

//------------------------------------------------------------------------
bool StepSequencer::lineFires (int gridStep)
{
	if (!mRun)
		return false;

	if (gridStep < 0 || gridStep >= kStepCount)
		return false;

	if (!mLaunched)
	{
		// ARMED: wait for a line the division fires on. Step 0 is the bar
		// line and EVERY division fires on it, which is what keeps a
		// sequencer armed on 1/16 and one armed on 1/1 in phase with each
		// other - see divisionFires in FilterDrumTransport.h.
		if (!divisionFires (mDivision, gridStep))
			return false;

		mLaunched = true;
	}

	// THE PLAYHEAD IS THE GRID STEP, not a counter of our own.
	//
	// A counter would drift: the host can loop, locate, or drop a block,
	// and a sequencer counting its own steps would carry the error
	// forward for ever. Taking the step from the bar means the pattern is
	// always where the music is, and a locate to the middle of a bar puts
	// the playhead in the middle of the pattern - which is what every
	// hardware sequencer does and what makes it usable with a looped
	// section.
	mPlayhead = gridStep;

	return mSteps[gridStep];
}

//------------------------------------------------------------------------
} // namespace FilterDrum
