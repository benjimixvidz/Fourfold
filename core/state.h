#pragma once
// Patterns saved as a small line-based text document, stored in the patch:
//
//   tetrad-seq-state 4
//   gen <track> <seed> <density> <range> <voices> <ratchet> <variation>
//   row <track> <row> <level> <probability> <mute>                          (version 4)
//   step <track> <index> <active> <velocity> <gate> <probability> <ratchet> <condition> <A> <B>
//        <notes> <d0> .. <d6>
//   drum <track> <index> <hits> then, for rows 1-8: <velocity> <gate> <probability> <ratchet>
//        <condition> <A> <B>                                                   (version 4, steps
//        whose drum part is not the default only)
//   end
//
// Version 3 documents (no drum part) still load. Unknown lines are ignored, so documents that
// also list parameter values still load.

#include "pattern.h"

#include <string>

namespace tetrad {

std::string serializePatterns(const PatternBank& bank);

// Leaves `bank` untouched and returns false if the text is not a valid document.
bool deserializePatterns(const std::string& text, PatternBank& bank);

} // namespace tetrad
