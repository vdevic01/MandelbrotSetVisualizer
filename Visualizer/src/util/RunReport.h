#pragma once

#include <optional>
#include <string>
#include <vector>

// What one run of the renderer has to report besides the image: how long each stage took, what the remote worker
// answered, and warnings. The stages record themselves (ScopedTimer) and the remote client records what it got;
// main prints the report at the end, as text lines while the run goes (the default) or as one JSON object with
// --format json, which is what the app reads. There is one report per process.
struct RunReport {
    struct Stage {
        std::string name;
        double ms;
    };

    struct Remote {
        int httpStatus = 0;
        double roundTripMs = -1;   // -1: not reported
        double samplingMs = -1;    // the worker's own timings, -1 when it did not send them
        double kernelMs = -1;
    };

    std::vector<Stage> stages;
    std::optional<Remote> remote;      // set when the remote worker was used
    std::vector<std::string> warnings;

    static RunReport& instance() {
        static RunReport report;
        return report;
    }
};
