#pragma once
// Patterns saved as a small line-based text document, stored in the patch:
//
//   tetrad-seq-state 3
//   gen <track> <seed> <density> <range> <voices> <ratchet> <variation>
//   step <track> <index> <active> <velocity> <gate> <probability> <ratchet> <condition> <A> <B>
//        <notes> <d0> .. <d6>
//   end
//
// Unknown lines are ignored, so documents that also list parameter values still load.

#include "pattern.h"

#include <string>

namespace tetrad {

std::string serializePatterns(const PatternBank& bank);

// Leaves `bank` untouched and returns false if the text is not a valid document.
bool deserializePatterns(const std::string& text, PatternBank& bank);

} // namespace tetrad
