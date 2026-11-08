#include "vector_loader.h"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#ifndef FPA_VECTOR_DIR
#error "FPA_VECTOR_DIR must point at the directory holding the generated vector files"
#endif

namespace fpt {

static VectorFile parseFile(int fractionWords) {
    VectorFile file;
    file.fractionWords = fractionWords;
    file.path = std::string(FPA_VECTOR_DIR) + "/fpa_f" + std::to_string(fractionWords) + ".txt";

    std::ifstream in(file.path);
    if (!in)
        throw std::runtime_error("cannot open vector file " + file.path +
                                 " (generate it with: python tests/generate_vectors.py)");

    std::string line;
    int lineNo = 0;
    bool sawFormat = false;
    while (std::getline(in, line)) {
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back();   // files may be checked out with CRLF
        if (line.empty() || line[0] == '#') continue;

        std::istringstream tokens(line);
        VectorCase c;
        c.line = lineNo;
        tokens >> c.op;
        if (c.op == "format") {
            int declared = 0;
            tokens >> declared;
            if (declared != fractionWords)
                throw std::runtime_error(file.path + ": declares format " + std::to_string(declared) +
                                         " but was loaded as format " + std::to_string(fractionWords));
            sawFormat = true;
            continue;
        }
        tokens >> c.tag;
        for (std::string t; tokens >> t;) c.args.push_back(t);
        if (c.args.empty())
            throw std::runtime_error(file.path + ":" + std::to_string(lineNo) + ": malformed line: " + line);
        file.cases.push_back(std::move(c));
    }
    if (!sawFormat)
        throw std::runtime_error(file.path + ": missing 'format' line");
    return file;
}

const VectorFile& loadVectors(int fractionWords) {
    static std::map<int, VectorFile> cache;
    auto it = cache.find(fractionWords);
    if (it == cache.end())
        it = cache.emplace(fractionWords, parseFile(fractionWords)).first;
    return it->second;
}

std::vector<const VectorCase*> casesFor(const VectorFile& file, const std::string& op) {
    std::vector<const VectorCase*> result;
    for (const auto& c : file.cases)
        if (c.op == op) result.push_back(&c);
    return result;
}

}  // namespace fpt
