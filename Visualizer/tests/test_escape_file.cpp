// Tests of the escape file (src/util/EscapeFile.cpp): what is saved is what is loaded, and a file that is not
// valid is refused with a message instead of being read.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "EscapeFile.h"

namespace {

EscapeData sample() {
    EscapeData d;
    d.width = 3;
    d.height = 2;
    d.samples = 2;
    d.escapeRadius = 16.5;
    d.escape.resize(12);
    for (size_t i = 0; i < d.escape.size(); i++) {
        d.escape.iters[i] = i % 4 == 0 ? -1 : static_cast<int>(i * 7);
        d.escape.magSq[i] = i % 4 == 0 ? 0.0f : 4.25f + static_cast<float>(i);
    }
    return d;
}

TEST(EscapeFile, WhatIsSavedIsLoaded) {
    const std::string path = testing::TempDir() + "roundtrip.esc";
    const EscapeData in = sample();
    saveEscapeFile(path, in);
    const EscapeData out = loadEscapeFile(path);
    EXPECT_EQ(in.width, out.width);
    EXPECT_EQ(in.height, out.height);
    EXPECT_EQ(in.samples, out.samples);
    EXPECT_DOUBLE_EQ(in.escapeRadius, out.escapeRadius);
    EXPECT_EQ(in.escape.iters, out.escape.iters);
    EXPECT_EQ(in.escape.magSq, out.escape.magSq);
    std::remove(path.c_str());
}

TEST(EscapeFile, AMissingFileIsReported) {
    EXPECT_THROW(loadEscapeFile(testing::TempDir() + "does_not_exist.esc"), std::runtime_error);
}

TEST(EscapeFile, AFileThatIsNotAnEscapeFileIsRefused) {
    const std::string path = testing::TempDir() + "not_escape.esc";
    std::ofstream(path, std::ios::binary) << "this is a text file, not an escape file";
    EXPECT_THROW(loadEscapeFile(path), std::runtime_error);
    std::remove(path.c_str());
}

TEST(EscapeFile, ATruncatedFileIsRefused) {
    const std::string path = testing::TempDir() + "truncated.esc";
    saveEscapeFile(path, sample());
    std::string bytes;
    {
        std::ifstream in(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes.substr(0, bytes.size() - 10);
    EXPECT_THROW(loadEscapeFile(path), std::runtime_error);
    std::remove(path.c_str());
}

}  // namespace
