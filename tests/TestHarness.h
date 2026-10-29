#pragma once

// A tiny dependency-free test harness, so the engine tests build anywhere with just a C++ compiler.

#include <cmath>
#include <cstdio>
#include <vector>

#include "fdm/Engine.h"

struct TestCase { const char* name; void (*fn)(); };

inline std::vector<TestCase>& testRegistry() { static std::vector<TestCase> r; return r; }
inline int& failureCount() { static int f = 0; return f; }

struct TestRegistrar {
    TestRegistrar(const char* name, void (*fn)()) { testRegistry().push_back({name, fn}); }
};

#define TEST(name)                                         \
    static void name();                                    \
    static TestRegistrar registrar_##name(#name, name);    \
    static void name()

#define CHECK(cond)                                                                   \
    do { if (!(cond)) { std::printf("    FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failureCount(); } } while (0)

#define CHECK_NEAR(a, b, tol)                                                                          \
    do {                                                                                               \
        const double a_ = static_cast<double>(a), b_ = static_cast<double>(b);                         \
        if (!(std::fabs(a_ - b_) <= static_cast<double>(tol))) {                                       \
            std::printf("    FAILED %s:%d  %s ~ %s  (%g vs %g, tolerance %g)\n", __FILE__, __LINE__,   \
                        #a, #b, a_, b_, static_cast<double>(tol));                                     \
            ++failureCount();                                                                          \
        }                                                                                              \
    } while (0)

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/** An instrument that records exactly when it is told to start/stop notes, and
    outputs a constant level so mixer behaviour can be measured. */
struct MockInstrument : fdm::IInstrument {
    struct Event { long long sample; int note; bool on; float velocity; };
    std::vector<Event> events;
    long long rendered = 0;
    float dc = 0.0f;
    fdm::ParamList paramList;

    void prepare(double, int) override {}
    void noteOn(int n, float v) override { events.push_back({rendered, n, true, v}); }
    void noteOff(int n) override { events.push_back({rendered, n, false, 0.0f}); }
    void allNotesOff() override {}
    void render(float* l, float* r, int n) override {
        for (int i = 0; i < n; ++i) { l[i] += dc; r[i] += dc; }
        rendered += n;
    }
    fdm::ParamList& params() override { return paramList; }
    const char* name() const override { return "Mock"; }

    int countOn(int note) const {
        int c = 0;
        for (auto& e : events) c += (e.on && e.note == note) ? 1 : 0;
        return c;
    }
    const Event* nth(int note, bool on, int n) const {
        for (auto& e : events)
            if (e.note == note && e.on == on && n-- == 0) return &e;
        return nullptr;
    }
};

struct TestGain : fdm::IEffect {
    float gain;
    explicit TestGain(float g) : gain(g) {}
    void prepare(double, int) override {}
    void process(float* l, float* r, int n) override { for (int i = 0; i < n; ++i) { l[i] *= gain; r[i] *= gain; } }
    const char* name() const override { return "TestGain"; }
};

struct Rendered { std::vector<float> l, r; };

/** Runs the engine for `frames` frames in blocks of `block`, appending to `out`. */
inline void run(fdm::Engine& e, long long frames, Rendered& out, int block = 512) {
    std::vector<float> bl(static_cast<size_t>(block)), br(static_cast<size_t>(block));
    long long done = 0;
    while (done < frames) {
        const int n = static_cast<int>(std::min<long long>(block, frames - done));
        e.process(bl.data(), br.data(), n);
        out.l.insert(out.l.end(), bl.begin(), bl.begin() + n);
        out.r.insert(out.r.end(), br.begin(), br.begin() + n);
        done += n;
    }
}

inline float peakOf(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX) {
    float p = 0.0f;
    to = std::min(to, v.size());
    for (size_t i = from; i < to; ++i) p = std::max(p, std::fabs(v[i]));
    return p;
}

inline float rmsOf(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX) {
    to = std::min(to, v.size());
    if (to <= from) return 0.0f;
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += static_cast<double>(v[i]) * v[i];
    return static_cast<float>(std::sqrt(s / static_cast<double>(to - from)));
}

inline bool allFinite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}
