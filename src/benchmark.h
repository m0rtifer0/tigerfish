/*
  Tigerfish, an aggressive-style UCI chess engine.
  Copyright (C) 2026 The Tigerfish developers

  Tigerfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Tigerfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef BENCHMARK_H_INCLUDED
#define BENCHMARK_H_INCLUDED

#include <iosfwd>
#include <string>
#include <vector>

namespace Tigerfish::Benchmark {

std::vector<std::string> setup_bench(const std::string&, std::istream&);

struct BenchmarkSetup {
    int                      ttSize;
    int                      threads;
    std::vector<std::string> commands;
    std::string              originalInvocation;
    std::string              filledInvocation;
};

BenchmarkSetup setup_benchmark(std::istream&);

}  // namespace Tigerfish

#endif  // #ifndef BENCHMARK_H_INCLUDED
