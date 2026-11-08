#pragma once

#include <string>
#include <vector>

namespace fpt {

// One line of a vector file: "<op> <tag> <args...>" (see generate_vectors.py for the exact layout).
struct VectorCase {
    std::string op;
    std::string tag;
    std::vector<std::string> args;   // hex tokens, kept as text and parsed by the test
    int line = 0;                    // 1-based line number in the file, for failure messages
};

struct VectorFile {
    int fractionWords = 0;
    std::string path;
    std::vector<VectorCase> cases;
};

// Loads (and caches) tests/vectors/fpa_f<fractionWords>.txt.
// Throws std::runtime_error with a clear message when the file is missing or malformed.
const VectorFile& loadVectors(int fractionWords);

// All cases of one operation, in file order.
std::vector<const VectorCase*> casesFor(const VectorFile& file, const std::string& op);

}  // namespace fpt
