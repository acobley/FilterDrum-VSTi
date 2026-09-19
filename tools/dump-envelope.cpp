//------------------------------------------------------------------------
// tools/dump-envelope.cpp
//
// Prints one drum's two envelope curves as text, so that
// tools/render-panel.py can draw the docs picture from THE PLUG-IN'S OWN
// ENVELOPE CODE rather than from a second copy of the maths in Python.
//
// A second copy is the thing worth avoiding here. The picture in docs/
// is meant to be evidence about the panel; a picture drawn from a
// re-implementation is evidence about the re-implementation, and it
// would go on looking right for exactly as long as nobody changed the
// envelope.
//
//   dump-envelope <vcfAttack> <vcfRelease> <vcaAttack> <vcaRelease> <points>
//
// Times in seconds. Prints the span in seconds on the first line, then
// `points` lines of "<vcf> <vca>", both curves drawn full scale.
//
// NO HEIGHTS. The curves are the shape; the Amount controls are drawn
// as marker lines by the panel, and render-panel.py draws those itself
// from the same table defaults.
//------------------------------------------------------------------------
#include "FilterDrumDsp.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
	if (argc != 6)
	{
		std::fprintf (stderr, "usage: dump-envelope vcfA vcfR vcaA vcaR points\n");
		return 2;
	}

	FilterDrum::ArSpec vcf;
	vcf.attack  = std::atof (argv[1]);
	vcf.release = std::atof (argv[2]);

	FilterDrum::ArSpec vca;
	vca.attack  = std::atof (argv[3]);
	vca.release = std::atof (argv[4]);

	const int points = std::atoi (argv[5]);
	if (points < 2)
		return 2;

	std::vector<float> a (points), b (points);
	const double span = FilterDrum::traceDrumEnvelopes (vcf, vca, a.data (), b.data (), points);

	std::printf ("%.9f\n", span);
	for (int i = 0; i < points; ++i)
		std::printf ("%.6f %.6f\n", a[i], b[i]);

	return 0;
}
