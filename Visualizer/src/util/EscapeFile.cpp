#include "EscapeFile.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

using namespace std;

namespace {

// Little-endian layout, written field by field: 8 bytes magic, int32 width, height, samples, float64 escape
// radius, uint64 count, then count x int32 iterations and count x float32 |z|^2.
constexpr char MAGIC[8] = {'M', 'B', 'E', 'S', 'C', 'v', '1', '\0'};
constexpr uint64_t MAX_COUNT = 1ull << 32;

template <typename T>
void put(ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void get(ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
}

}  // namespace

void saveEscapeFile(const string& path, const EscapeData& data) {
    if (data.escape.iters.size() != data.escape.magSq.size())
        throw logic_error("escape result has different numbers of iterations and values");
    ofstream out(path, ios::binary | ios::trunc);
    if (!out) throw runtime_error("cannot write the escape file '" + path + "'");
    out.write(MAGIC, sizeof(MAGIC));
    put<int32_t>(out, data.width);
    put<int32_t>(out, data.height);
    put<int32_t>(out, data.samples);
    put<double>(out, data.escapeRadius);
    put<uint64_t>(out, data.escape.size());
    out.write(reinterpret_cast<const char*>(data.escape.iters.data()),
              static_cast<streamsize>(data.escape.iters.size() * sizeof(int32_t)));
    out.write(reinterpret_cast<const char*>(data.escape.magSq.data()),
              static_cast<streamsize>(data.escape.magSq.size() * sizeof(float)));
    out.flush();
    if (!out) throw runtime_error("failed while writing the escape file '" + path + "'");
}

EscapeData loadEscapeFile(const string& path) {
    static_assert(sizeof(int) == sizeof(int32_t), "iteration counts are stored as 32-bit integers");
    ifstream in(path, ios::binary);
    if (!in) throw runtime_error("cannot open the escape file '" + path + "'");

    char magic[sizeof(MAGIC)] = {};
    in.read(magic, sizeof(magic));
    if (!in || memcmp(magic, MAGIC, sizeof(MAGIC)) != 0)
        throw runtime_error("'" + path + "' is not an escape file (or it is from another version)");

    EscapeData data;
    int32_t width = 0, height = 0, samples = 0;
    uint64_t count = 0;
    get(in, width);
    get(in, height);
    get(in, samples);
    get(in, data.escapeRadius);
    get(in, count);
    if (!in || width <= 0 || height <= 0 || samples <= 0 || count > MAX_COUNT ||
        count != static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * static_cast<uint64_t>(samples))
        throw runtime_error("the escape file '" + path + "' is damaged");
    data.width = width;
    data.height = height;
    data.samples = samples;

    data.escape.resize(static_cast<size_t>(count));
    in.read(reinterpret_cast<char*>(data.escape.iters.data()), static_cast<streamsize>(count * sizeof(int32_t)));
    in.read(reinterpret_cast<char*>(data.escape.magSq.data()), static_cast<streamsize>(count * sizeof(float)));
    if (!in) throw runtime_error("the escape file '" + path + "' is truncated");
    return data;
}
