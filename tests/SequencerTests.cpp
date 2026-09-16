//------------------------------------------------------------------------
// FilterDrum - step sequencer tests
//
// SDK-FREE, compiled against FilterDrumSequencer.cpp and the bar clock:
//
//   c++ -std=c++17 -O2 -I../source SequencerTests.cpp
//       ../source/FilterDrumSequencer.cpp ../source/FilterDrumTransport.cpp
//       -o /tmp/seqtests  &&  /tmp/seqtests
//
// The arithmetic of WHERE the lines fall is TransportTests.cpp's job.
// This file is about what the sequencer does with them: arming,
// launching on the right line, stopping at once, and the playhead.
//------------------------------------------------------------------------

#include "FilterDrumSequencer.h"

#include <cstdio>
#include <string>

using namespace FilterDrum;

//------------------------------------------------------------------------
namespace {

int gChecks = 0;
int gFailures = 0;

void check (bool condition, const std::string& what)
{
	++gChecks;
	std::printf ("  %-64s %s\n", what.c_str (), condition ? "ok" : "FAILED");
	if (!condition)
		++gFailures;
}

/** Run a whole bar of grid lines through a sequencer and return which
    steps struck the drums. */
std::string barOf (StepSequencer& seq)
{
	std::string out;
	for (int step = 0; step < kStepCount; ++step)
		out += seq.lineFires (step) ? 'X' : '.';
	return out;
}

/** A four-on-the-floor pattern: steps 0, 4, 8, 12. */
void fourOnTheFloor (StepSequencer& seq)
{
	for (int i = 0; i < kStepCount; ++i)
		seq.setStep (i, (i % 4) == 0);
}

} // anonymous namespace

//------------------------------------------------------------------------
static void testPattern ()
{
	std::printf ("the pattern\n");

	StepSequencer seq;
	check (kStepCount == 16, "sixteen steps");
	check (kStepCount == kGridStepsPerBar,
	       "and the grid has exactly that many lines to a bar");

	for (int i = 0; i < kStepCount; ++i)
		check (!seq.step (i), i == 0 ? "every step starts off" : "");

	seq.setStep (3, true);
	check (seq.step (3), "a step can be switched on");
	check (!seq.step (4), "and its neighbour is untouched");

	// Out of range is ignored rather than written past the end.
	seq.setStep (-1, true);
	seq.setStep (kStepCount, true);
	seq.setStep (9999, true);
	check (!seq.step (-1) && !seq.step (kStepCount), "out-of-range steps read false");
	check (seq.step (3), "and writing one did not corrupt the pattern");
}

//------------------------------------------------------------------------
static void testRunStopsAtOnce ()
{
	std::printf ("run, arm and stop\n");

	StepSequencer seq;
	fourOnTheFloor (seq);

	// NOT RUNNING: nothing fires, however many lines go past.
	check (barOf (seq) == "................", "nothing fires while Run is off");
	check (!seq.armed () && !seq.launched (), "and it is neither armed nor playing");

	seq.setRunning (true);
	check (seq.armed (), "switching Run on ARMS it");
	check (!seq.launched (), "it is not playing yet");

	// STOPPING IS IMMEDIATE, where starting waits. A sequencer that kept
	// playing for most of a bar after being switched off reads as stuck.
	seq.setRunning (false);
	check (!seq.armed () && !seq.launched (), "switching Run off stops it at once");
	check (barOf (seq) == "................", "and nothing fires after it");
	check (seq.playhead () == -1, "the playhead is cleared");
}

//------------------------------------------------------------------------
static void testLaunchDivision ()
{
	std::printf ("launching on the right line\n");

	// ARMED MID-BAR ON 1/1: nothing until the next bar line, then the
	// whole pattern. This is the behaviour the feature was asked for.
	{
		StepSequencer seq;
		fourOnTheFloor (seq);
		seq.setDivision (LaunchDivision::Bar);
		seq.setRunning (true);

		// Lines 5..15 of the bar in which Run was pressed: all refused.
		std::string first;
		for (int step = 5; step < kStepCount; ++step)
			first += seq.lineFires (step) ? 'X' : '.';
		check (first == "...........", "armed on 1/1, the rest of the bar is silent");
		check (seq.armed (), "and it is still waiting");

		// The next bar, from step 0.
		check (barOf (seq) == "X...X...X...X...", "then the pattern plays from step 0");
		check (seq.launched (), "and it is playing");
	}

	// ON 1/4 it starts at the next quarter - step 4, 8 or 12 - so a
	// press just after step 4 waits only to step 8.
	{
		StepSequencer seq;
		fourOnTheFloor (seq);
		seq.setDivision (LaunchDivision::Quarter);
		seq.setRunning (true);

		std::string out;
		for (int step = 5; step < kStepCount; ++step)
			out += seq.lineFires (step) ? 'X' : '.';
		// steps 5,6,7 refused; 8 launches AND is an enabled step; 9..11
		// silent; 12 enabled; 13..15 silent.
		check (out == "...X...X...", "armed on 1/4, it launches at step 8");
	}

	// ON 1/16 it starts on the very next line, whatever that is.
	{
		StepSequencer seq;
		for (int i = 0; i < kStepCount; ++i)
			seq.setStep (i, true);
		seq.setDivision (LaunchDivision::Sixteenth);
		seq.setRunning (true);

		check (seq.lineFires (7), "armed on 1/16, it launches on the very next line");
		check (seq.launched (), "and is playing immediately");
	}

	//--------------------------------------------------------------------
	// NEGATIVE CONTROL. If the division were being ignored, the 1/1 case
	// above would have launched at step 5 like the 1/16 case does. These
	// two must NOT agree.
	//--------------------------------------------------------------------
	{
		StepSequencer bar, sixteenth;
		for (int i = 0; i < kStepCount; ++i)
		{
			bar.setStep (i, true);
			sixteenth.setStep (i, true);
		}
		bar.setDivision (LaunchDivision::Bar);
		sixteenth.setDivision (LaunchDivision::Sixteenth);
		bar.setRunning (true);
		sixteenth.setRunning (true);

		check (!bar.lineFires (5) && sixteenth.lineFires (5),
		       "NEGATIVE CONTROL: the division actually changes when it starts");
	}

	// EVERY DIVISION FIRES ON STEP 0, which is what keeps a sequencer
	// armed on 1/16 in phase with one armed on 1/1.
	for (int i = 0; i < kLaunchDivisionCount; ++i)
	{
		StepSequencer seq;
		seq.setStep (0, true);
		seq.setDivision (divisionFromIndex (i));
		seq.setRunning (true);
		check (seq.lineFires (0),
		       std::string ("division ") + divisionShortName (divisionFromIndex (i))
		           + " launches on the bar line");
	}
}

//------------------------------------------------------------------------
static void testPlayhead ()
{
	std::printf ("the playhead\n");

	StepSequencer seq;
	fourOnTheFloor (seq);
	seq.setDivision (LaunchDivision::Sixteenth);
	seq.setRunning (true);

	check (seq.playhead () == -1, "before the first line there is no playhead");

	seq.lineFires (0);
	check (seq.playhead () == 0, "it follows the grid step");
	seq.lineFires (1);
	check (seq.playhead () == 1, "including steps that do not fire");
	seq.lineFires (7);
	check (seq.playhead () == 7, "and a jump - a locate - puts it where the music is");

	// THE PLAYHEAD IS THE GRID STEP, NOT A COUNTER OF OUR OWN. A counter
	// would drift through a loop or a locate and carry the error for
	// ever; this cannot, because it is read from the bar every time.
	seq.lineFires (3);
	check (seq.playhead () == 3,
	       "NEGATIVE CONTROL: it moves backwards with the music, not forwards regardless");

	// reset() forgets the launch and the playhead but keeps the pattern.
	seq.reset ();
	check (seq.playhead () == -1, "a reset clears the playhead");
	check (seq.armed (), "leaves it armed");
	check (seq.step (0) && seq.step (4), "and keeps the pattern");
}

//------------------------------------------------------------------------
static void testTransportStop ()
{
	std::printf ("stopping and starting the transport\n");

	StepSequencer seq;
	fourOnTheFloor (seq);
	seq.setDivision (LaunchDivision::Bar);
	seq.setRunning (true);

	check (barOf (seq) == "X...X...X...X...", "it launches and plays");
	check (seq.launched (), "and is playing");

	// The processor calls reset() when the transport stops. Pressing play
	// again must LAUNCH on the next bar line rather than resume mid-bar.
	seq.reset ();
	check (!seq.launched (), "a transport stop un-launches it");
	check (seq.armed (), "and leaves it armed");

	std::string midBar;
	for (int step = 9; step < kStepCount; ++step)
		midBar += seq.lineFires (step) ? 'X' : '.';
	check (midBar == ".......", "so a restart mid-bar plays nothing until the bar line");
	check (barOf (seq) == "X...X...X...X...", "and then picks up from step 0");
}

//------------------------------------------------------------------------
int main ()
{
	std::printf ("FilterDrum sequencer tests\n");
	std::printf ("--------------------------\n");

	testPattern ();
	testRunStopsAtOnce ();
	testLaunchDivision ();
	testPlayhead ();
	testTransportStop ();

	std::printf ("--------------------------\n");
	std::printf ("%d checks, %d failures\n", gChecks, gFailures);
	return (gFailures == 0) ? 0 : 1;
}
