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

#ifndef SCORE_H_INCLUDED
#define SCORE_H_INCLUDED

#include <variant>
#include <utility>

#include "types.h"

namespace Tigerfish {

class Position;

class Score {
   public:
    struct Mate {
        int plies;
    };

    struct Tablebase {
        int  plies;
        bool win;
    };

    struct InternalUnits {
        int value;
    };

    Score() = default;
    Score(Value v, const Position& pos);

    template<typename T>
    bool is() const {
        return std::holds_alternative<T>(score);
    }

    template<typename T>
    T get() const {
        return std::get<T>(score);
    }

    template<typename F>
    decltype(auto) visit(F&& f) const {
        return std::visit(std::forward<F>(f), score);
    }

   private:
    std::variant<Mate, Tablebase, InternalUnits> score;
};

}

#endif  // #ifndef SCORE_H_INCLUDED
