#ifndef TOOLS_TRAINING_DATA_GENERATOR_H_
#define TOOLS_TRAINING_DATA_GENERATOR_H_

#include <sstream>

namespace Tigerfish {
class Engine;
}

namespace Tigerfish::Tools {

    // UCI command: generate_training_data depth 9 count 10000000 ...
    void generate_training_data(Engine& engine, std::istringstream& is);
}

#endif
