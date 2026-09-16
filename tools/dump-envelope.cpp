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
//   dump-envelope <vcfAttack> <vcfRelease> <vcfHeight>
//                 <vcfAtkShape> <vcfRelShape>
//                 <vcaAttack> <vcaRelease> <vcaHeight>
//                 <vcaAtkShape> <vcaRelShape> <points>
//
// Times in seconds, heights 0..1, shapes -1 Exponential .. +1
// Logarithmic. Prints the span in seconds on the first line, then
// `points` lines of "<vcf> <vca>".
//
// THE SHAPES ARE PASSED IN rather than left to ArSpec's defaults, even
// though those defaults happen to match the table's today. The point of
// this tool is that the docs picture is not a copy of anything; a
// default read from one place and relied on from another is a copy.
//------------------------------------------------------------------------
#include "FilterDrumDsp.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
	if (argc != 12)
	{
		std::fprintf (stderr,
		              "usage: dump-envelope vcfA vcfR vcfH vcfAtkShp vcfRelShp "
		              "vcaA vcaR vcaH vcaAtkShp vcaRelShp points\n");
		return 2;
	}

	FilterDrum::ArSpec vcf;
	vcf.attack       = std::atof (argv[1]);
	vcf.release      = std::atof (argv[2]);
	vcf.height       = std::atof (argv[3]);
	vcf.attackShape  = std::atof (argv[4]);
	vcf.releaseShape = std::atof (argv[5]);

	FilterDrum::ArSpec vca;
	vca.attack       = std::atof (argv[6]);
	vca.release      = std::atof (argv[7]);
	vca.height       = std::atof (argv[8]);
	vca.attackShape  = std::atof (argv[9]);
	vca.releaseShape = std::atof (argv[10]);

	const int points = std::atoi (argv[11]);
	if (points < 2)
		return 2;

	std::vector<float> a (points), b (points);
	const double span = FilterDrum::traceDrumEnvelopes (vcf, vca, a.data (), b.data (), points);

	std::printf ("%.9f\n", span);
	for (int i = 0; i < points; ++i)
		std::printf ("%.6f %.6f\n", a[i], b[i]);

	return 0;
}
