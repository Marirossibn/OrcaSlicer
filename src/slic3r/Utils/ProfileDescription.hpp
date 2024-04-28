#include <I18N.hpp>
#include <wx/string.h>
#ifndef _L
#define _L(s) Slic3r::I18N::translate(s)
#endif

namespace ProfileDescrption {
    const std::string PROFILE_DESCRIPTION_0 = _L("It has a small layer height, and results in almost negligible layer lines and high printing quality. It is suitable for most general printing cases.");
    const std::string PROFILE_DESCRIPTION_1 = _L("Compared with the default profile of a 0.2 mm nozzle, it has lower speeds and acceleration, and the sparse infill pattern is Gyroid. So, it results in much higher printing quality, but a much longer printing time.");
    const std::string PROFILE_DESCRIPTION_2 = _L("Compared with the default profile of a 0.2 mm nozzle, it has a slightly bigger layer height, and results in almost negligible layer lines, and slightly shorter printing time.");
    const std::string PROFILE_DESCRIPTION_3 = _L("Compared with the default profile of a 0.2 mm nozzle, it has a bigger layer height, and results in slightly visible layer lines, but shorter printing time.");
    const std::string PROFILE_DESCRIPTION_4 = _L("Compared with the default profile of a 0.2 mm nozzle, it has a smaller layer height, and results in almost invisible layer lines and higher printing quality, but shorter printing time.");
    const std::string PROFILE_DESCRIPTION_5 = _L("Compared with the default profile of a 0.2 mm nozzle, it has a smaller layer lines, lower speeds and acceleration, and the sparse infill pattern is Gyroid. So, it results in almost invisible layer lines and much higher printing quality, but much longer printing time.");
    const std::string PROFILE_DESCRIPTION_6 = _L("Compared with the default profile of 0.2 mm nozzle, it has a smaller layer height, and results in minimal layer lines and higher printing quality, but shorter printing time.");
    const std::string PROFILE_DESCRIPTION_7 = _L("Compared with the default profile of a 0.2 mm nozzle, it has a smaller layer lines, lower speeds and acceleration, and the sparse infill pattern is Gyroid. So, it results in minimal layer lines and much higher printing quality, but much longer printing time.");
    const std::string PROFILE_DESCRIPTION_8 = _L("It has a general layer height, and results in general layer lines and printing quality. It is suitable for most general printing cases.");
    const std::string PROFILE_DESCRIPTION_9 = _L("Compared with the default profile of a 0.4 mm nozzle, it has more wall loops and a higher sparse infill density. So, it results in higher strength of the prints, but more filament consumption and longer printing time.");
    const std::string PROFILE_DESCRIPTION_10 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a bigger layer height, and results in more apparent layer lines and lower printing quality, but slightly shorter printing time.");
    const std::string PROFILE_DESCRIPTION_11 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a bigger layer height, and results in more apparent layer lines and lower printing quality, but shorter printing time.");
    const std::string PROFILE_DESCRIPTION_12 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height, and results in less apparent layer lines and higher printing quality, but longer printing time.");
    const std::string PROFILE_DESCRIPTION_13 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height, lower speeds and acceleration, and the sparse infill pattern is Gyroid. So, it results in less apparent layer lines and much higher printing quality, but much longer printing time.");
    const std::string PROFILE_DESCRIPTION_14 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height, and results in almost negligible layer lines and higher printing quality, but longer printing time.");
    const std::string PROFILE_DESCRIPTION_15 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height, lower speeds and acceleration, and the sparse infill pattern is Gyroid. So, it results in almost negligible layer lines and much higher printing quality, but much longer printing time.");
    const std::string PROFILE_DESCRIPTION_16 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height, and results in almost negligible layer lines and longer printing time.");
    const std::string PROFILE_DESCRIPTION_17 = _L("It has a big layer height, and results in apparent layer lines and ordinary printing quality and printing time.");
    const std::string PROFILE_DESCRIPTION_18 = _L("Compared with the default profile of a 0.6 mm nozzle, it has more wall loops and a higher sparse infill density. So, it results in higher strength of the prints, but more filament consumption and longer printing time.");
    const std::string PROFILE_DESCRIPTION_19 = _L("Compared with the default profile of a 0.6 mm nozzle, it has a bigger layer height, and results in more apparent layer lines and lower printing quality, but shorter printing time in some printing cases.");
    const std::string PROFILE_DESCRIPTION_20 = _L("Compared with the default profile of a 0.6 mm nozzle, it has a bigger layer height, and results in much more apparent layer lines and much lower printing quality, but shorter printing time in some printing cases.");
    const std::string PROFILE_DESCRIPTION_21 = _L("Compared with the default profile of a 0.6 mm nozzle, it has a smaller layer height, and results in less apparent layer lines and slight higher printing quality, but longer printing time.");
    const std::string PROFILE_DESCRIPTION_22 = _L("Compared with the default profile of a 0.6 mm nozzle, it has a smaller layer height, and results in less apparent layer lines and higher printing quality, but longer printing time.");
    const std::string PROFILE_DESCRIPTION_23 = _L("It has a very big layer height, and results in very apparent layer lines, low printing quality and general printing time.");
    const std::string PROFILE_DESCRIPTION_24 = _L("Compared with the default profile of a 0.8 mm nozzle, it has a bigger layer height, and results in very apparent layer lines and much lower printing quality, but shorter printing time in some printing cases.");
    const std::string PROFILE_DESCRIPTION_25 = _L("Compared with the default profile of a 0.8 mm nozzle, it has a much bigger layer height, and results in extremely apparent layer lines and much lower printing quality, but much shorter printing time in some printing cases.");
    const std::string PROFILE_DESCRIPTION_26 = _L("Compared with the default profile of a 0.8 mm nozzle, it has a slightly smaller layer height, and results in slightly less but still apparent layer lines and slightly higher printing quality, but longer printing time in some printing cases.");
    const std::string PROFILE_DESCRIPTION_27 = _L("Compared with the default profile of a 0.8 mm nozzle, it has a smaller layer height, and results in less but still apparent layer lines and slightly higher printing quality, but longer printing time in some printing cases.");
}