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
void StepSequencer::setStepEnabled (int index, bool enabled)
{
	if (index < 0 || index >= kStepCount || mEnabled[index] == enabled)
		return;
	mEnabled[index] = enabled;
	rebuildCycle ();
}

//------------------------------------------------------------------------
bool StepSequencer::stepEnabled (int index) const
{
	if (index < 0 || index >= kStepCount)
		return false;
	return mEnabled[index];
}

//------------------------------------------------------------------------
void StepSequencer::rebuildCycle ()
{
	// Rebuilt when a step is enabled or disabled - a click or an
	// automation point, never per sample - so lineFires only ever
	// indexes a table.
	mCycleLength = 0;
	for (int i = 0; i < kStepCount; ++i)
		if (mEnabled[i])
			mCycle[mCycleLength++] = i;
}

//------------------------------------------------------------------------
int StepSequencer::stepAtPlace (long long place) const
{
	if (mCycleLength <= 0)
		return -1;

	// THE MATHEMATICAL MODULO. C++'s % keeps the sign of the dividend, so
	// a pre-roll line at index -1 would come out as -1 rather than the
	// last place in the cycle.
	long long m = place % mCycleLength;
	if (m < 0)
		m += mCycleLength;
	return mCycle[m];
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
bool StepSequencer::lineFires (int gridStep, long long songIndex)
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
	//
	// WITH STEPS DISABLED the same rule holds one level up: the place in
	// the shortened cycle is the line's index in the SONG, modulo the
	// cycle length - see the banner. With all sixteen enabled the cycle
	// is the bar, and the step in the bar is the answer exactly as it
	// was before steps could be disabled.
	const int stepNow = (mCycleLength == kStepCount) ? gridStep : stepAtPlace (songIndex);

	mPlayhead = stepNow;

	if (stepNow < 0)
		return false;   // every step disabled: nothing plays

	return mSteps[stepNow];
}

//------------------------------------------------------------------------
} // namespace FilterDrum
