// Fast exact solver: bitmask constraint propagation (naked + hidden singles) with
// minimum-remaining-values branching. Used for uniqueness checks and grid generation.
#pragma once

#include "grid.h"
#include "rng.h"

namespace sudoku {

// Counts solutions, stopping once `limit` is reached. Optionally returns the first solution.
int countSolutions(const Grid& puzzle, int limit = 2, Grid* firstSolution = nullptr);

// True if the puzzle has at least one solution where `cell` does not contain `digit`.
// This is the fast uniqueness test used while removing clues.
bool hasSolutionWithout(const Grid& puzzle, int cell, int digit);

// A random, complete and valid grid.
Grid randomSolution(Rng& rng);

}  // namespace sudoku
