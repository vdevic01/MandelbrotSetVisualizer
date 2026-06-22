#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

class ScopedTimer {
public:
    explicit ScopedTimer(std::string name)
        : m_name(std::move(name))
        , m_start(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer() {
        auto end      = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - m_start);
        std::cout << std::left << std::setw(36) << m_name + ":" << duration.count() << " ms\n";
        std::cout << "=====================================================\n";
    }

    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string m_name;
    std::chrono::high_resolution_clock::time_point m_start;
};
