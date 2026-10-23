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

	//--------------------------------------------------------------------
	// THE LAMP AND THE HIT ARE THE SAME STEP.
	//
	// The panel lights step n's lamp from playhead(), and the drums are
	// struck when lineFires returns true, so those two must agree about
	// which step it is - otherwise the lamp runs beside the sound rather
	// than on it. Checked for every step of a full pass, with a pattern
	// that has both on and off steps in it.
	//--------------------------------------------------------------------
	{
		StepSequencer s2;
		for (int i = 0; i < kStepCount; ++i)
			s2.setStep (i, (i % 3) == 0);
		s2.setDivision (LaunchDivision::Sixteenth);
		s2.setRunning (true);

		bool aligned = true;
		for (int pass = 0; pass < 2; ++pass)
		{
			for (int stepIndex = 0; stepIndex < kStepCount; ++stepIndex)
			{
				const bool hit = s2.lineFires (stepIndex);
				if (s2.playhead () != stepIndex)
					aligned = false;
				if (hit != ((stepIndex % 3) == 0))
					aligned = false;
			}
		}
		check (aligned,
		       "the playhead is always the step that fired - lamp and hit agree");
	}

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
/** Run `count` lines of the SONG, from line `first`, through a sequencer
    - step in the bar and song index both, as the processor does - and
    return which struck the drums. */
static std::string songOf (StepSequencer& seq, long long first, int count)
{
	std::string out;
	for (long long i = first; i < first + count; ++i)
	{
		const int step = static_cast<int> (((i % kStepCount) + kStepCount) % kStepCount);
		out += seq.lineFires (step, i) ? 'X' : '.';
	}
	return out;
}

//------------------------------------------------------------------------
static void testDisabledSteps ()
{
	std::printf ("disabled steps: other time signatures\n");

	{
		StepSequencer seq;
		check (seq.cycleLength () == 16, "every step starts enabled");
		for (int i = 0; i < kStepCount; ++i)
			check (seq.stepEnabled (i), "step " + std::to_string (i + 1) + " enabled by default");
		check (!seq.stepEnabled (-1) && !seq.stepEnabled (kStepCount), "out of range reads disabled");
		seq.setStepEnabled (kStepCount, false);
		seq.setStepEnabled (-1, false);
		check (seq.cycleLength () == 16, "and out-of-range writes are ignored");
	}

	// 3/4 IN A 4/4 HOST: steps 13-16 out. Beats on 1, 5, 9; a twelve-step
	// cycle that meets the bar line again every third bar.
	{
		StepSequencer seq;
		fourOnTheFloor (seq);
		for (int i = 12; i < 16; ++i)
			seq.setStepEnabled (i, false);
		check (seq.cycleLength () == 12, "four disabled leaves a cycle of twelve");
		seq.setRunning (true);

		const std::string three = songOf (seq, 0, 48);
		check (three == "X...X...X...X...X...X...X...X...X...X...X...X...",
		       "3/4: a beat every four sixteenths, straight across the bar lines");

		// The tell that it is not just steps 13-16 switched off: step 13's
		// hit (on by default above) never plays, and the cycle restarts
		// on host step 12, not on the bar line.
		StepSequencer off;
		fourOnTheFloor (off);
		off.setStep (12, false);
		off.setRunning (true);
		check (songOf (off, 0, 32) == "X...X...X.......X...X...X.......",
		       "NEGATIVE CONTROL: switching 13 off is a gap, not a 3/4 bar");
	}

	// THE PLAYHEAD SKIPS disabled steps and wraps to the first enabled.
	{
		StepSequencer seq;
		for (int i = 12; i < 16; ++i)
			seq.setStepEnabled (i, false);
		seq.setDivision (LaunchDivision::Sixteenth);   // launch on the first line given
		seq.setRunning (true);
		seq.lineFires (11, 11);
		check (seq.playhead () == 11, "the playhead reaches step 12");
		seq.lineFires (12, 12);
		check (seq.playhead () == 0, "and the next sixteenth is step 1 again, not 13");
		seq.lineFires (0, 16);
		check (seq.playhead () == 4, "so the host's next bar line is step 5 of the pattern");
	}

	// A HOLE IN THE MIDDLE: disable step 4 alone and the cycle is fifteen.
	{
		StepSequencer seq;
		seq.setStep (4, true);              // step 5
		seq.setStepEnabled (3, false);      // step 4 out
		seq.setRunning (true);
		check (seq.cycleLength () == 15, "one disabled leaves fifteen");
		check (seq.stepAtPlace (3) == 4, "the fourth sixteenth lands on step 5, skipping 4");
		check (seq.stepAtPlace (15) == 0, "and the sixteenth is step 1 again");
		check (songOf (seq, 0, 30) == "...X..............X...........",
		       "and its hit comes round every fifteen sixteenths");
	}

	// PLACED BY THE SONG, not by when it launched.
	{
		StepSequencer a;
		fourOnTheFloor (a);
		for (int i = 12; i < 16; ++i)
			a.setStepEnabled (i, false);
		a.setRunning (true);
		const std::string fromTop = songOf (a, 0, 64);

		StepSequencer b;
		fourOnTheFloor (b);
		for (int i = 12; i < 16; ++i)
			b.setStepEnabled (i, false);
		b.setRunning (true);
		// Launched on the bar line of bar 2 (line 16).
		const std::string fromBar2 = songOf (b, 16, 48);
		check (fromBar2 == fromTop.substr (16), "a launch at bar 2 joins the cycle where the song is");
		b.lineFires (0, 16);
		b.reset ();
		b.lineFires (0, 32);
		check (b.playhead () == 8, "bar 3's line is place 32 mod 12 = 8, i.e. step 9");
	}

	// Pre-roll: a negative index is the end of the cycle, not garbage.
	{
		StepSequencer seq;
		for (int i = 12; i < 16; ++i)
			seq.setStepEnabled (i, false);
		check (seq.stepAtPlace (-1) == 11, "place -1 is the last enabled step");
		check (seq.stepAtPlace (-12) == 0, "place -12 is the first");
		check (seq.stepAtPlace (12) == 0 && seq.stepAtPlace (25) == 1, "and it wraps upwards");
	}

	// Everything disabled: silence and no playhead, rather than a crash.
	{
		StepSequencer seq;
		fourOnTheFloor (seq);
		for (int i = 0; i < kStepCount; ++i)
			seq.setStepEnabled (i, false);
		seq.setRunning (true);
		check (seq.cycleLength () == 0, "all disabled is a cycle of nothing");
		check (songOf (seq, 0, 32) == std::string (32, '.'), "which plays nothing");
		check (seq.playhead () == -1, "and has no playhead");
		check (seq.stepAtPlace (5) == -1, "and no step at any place");
	}

	// Re-enabling restores the old sixteen-step behaviour exactly, hits
	// kept, and the index is then ignored.
	{
		StepSequencer seq;
		fourOnTheFloor (seq);
		seq.setStepEnabled (12, false);
		seq.setStepEnabled (12, true);
		check (seq.step (12), "a disabled step keeps its hit");
		seq.setRunning (true);
		check (seq.lineFires (0, 12345) && seq.playhead () == 0,
		       "all enabled: the step in the bar decides, whatever the index");
	}
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
	testDisabledSteps ();

	std::printf ("--------------------------\n");
	std::printf ("%d checks, %d failures\n", gChecks, gFailures);
	return (gFailures == 0) ? 0 : 1;
}
