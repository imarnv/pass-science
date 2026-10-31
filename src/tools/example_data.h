#pragma once
// Generators for the self-created example files shipped in data/.

#include <string>

namespace wb::tools {

// ASCII STL of a small finned heat sink (millimetres): a 40 x 40 x 4 base plate
// with 7 straight fins 1.6 mm thick and 18 mm tall. Built from closed boxes
// that touch at shared faces (a visualisation model, not a boolean union).
std::string heatSinkStl();

// SYNTHETIC nodal temperature field over the same heat sink (metres, C).
// Not an ANSYS export: an analytic field (heat source under the base centre,
// exponential decay up the fins) written in the ANSYS-style CSV schema.
std::string syntheticNodalCsv();

} // namespace wb::tools
