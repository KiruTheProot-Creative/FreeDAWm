#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fdm {

/** A single automatable value. Written from the UI thread, read from the audio
    thread, so the value is an atomic float. */
class Param {
public:
    Param(std::string id, std::string name, float minValue, float maxValue, float defaultValue,
          bool logarithmic = false, bool stepped = false)
        : id_(std::move(id)), name_(std::move(name)), min_(minValue), max_(maxValue),
          default_(defaultValue), log_(logarithmic), stepped_(stepped), value_(defaultValue) {}

    Param(const Param&) = delete;
    Param& operator=(const Param&) = delete;

    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    float min() const { return min_; }
    float max() const { return max_; }
    float defaultValue() const { return default_; }
    bool isLogarithmic() const { return log_; }
    bool isStepped() const { return stepped_; }

    float get() const { return value_.load(std::memory_order_relaxed); }

    void set(float v) {
        v = std::clamp(v, min_, max_);
        if (stepped_) v = std::round(v);
        value_.store(v, std::memory_order_relaxed);
    }

    void reset() { set(default_); }

    /** 0..1 position of the current value, honouring the log curve. */
    float normalised() const {
        const float v = get();
        if (log_) return std::log(v / min_) / std::log(max_ / min_);
        return (v - min_) / (max_ - min_);
    }

    void setNormalised(float n) {
        n = std::clamp(n, 0.0f, 1.0f);
        set(log_ ? min_ * std::pow(max_ / min_, n) : min_ + n * (max_ - min_));
    }

private:
    std::string id_, name_;
    float min_, max_, default_;
    bool log_, stepped_;
    std::atomic<float> value_;
};

/** Owns a set of Params. Params never move once created, so references stay valid. */
class ParamList {
public:
    Param& add(std::string id, std::string name, float minValue, float maxValue, float defaultValue,
               bool logarithmic = false, bool stepped = false) {
        params_.push_back(std::make_unique<Param>(std::move(id), std::move(name), minValue, maxValue,
                                                  defaultValue, logarithmic, stepped));
        return *params_.back();
    }

    Param* find(const std::string& id) const {
        for (auto& p : params_)
            if (p->id() == id) return p.get();
        return nullptr;
    }

    int size() const { return static_cast<int>(params_.size()); }
    Param& at(int i) const { return *params_[static_cast<size_t>(i)]; }

    void resetAll() { for (auto& p : params_) p->reset(); }

private:
    std::vector<std::unique_ptr<Param>> params_;
};

}  // namespace fdm
