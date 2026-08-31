#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace phoenaux::dsp {

inline constexpr std::size_t kMaxChannels = 8;
inline constexpr std::size_t kMaxEQBands = 31;

struct ProcessSpec {
    double sampleRate = 0.0;
    std::size_t maximumFrameCount = 0;
    std::size_t maximumChannelCount = 0;

    [[nodiscard]] bool isValid() const noexcept;
};

struct AudioBufferView {
    float* const* channels = nullptr;
    std::size_t channelCount = 0;
    std::size_t frameCount = 0;

    [[nodiscard]] bool isValidFor(const ProcessSpec& spec) const noexcept;
};

[[nodiscard]] float decibelsToLinear(float decibels) noexcept;
[[nodiscard]] float linearToDecibels(float linear) noexcept;

class LinearSmoother {
public:
    void prepare(double sampleRate, double rampMilliseconds) noexcept;
    void reset(float value) noexcept;
    void setTarget(float value) noexcept;
    [[nodiscard]] float next() noexcept;
    [[nodiscard]] float current() const noexcept { return current_; }
    [[nodiscard]] float target() const noexcept { return target_; }
    [[nodiscard]] bool isSmoothing() const noexcept { return remaining_ != 0; }

private:
    std::uint32_t rampSamples_ = 1;
    std::uint32_t remaining_ = 0;
    float current_ = 0.0F;
    float target_ = 0.0F;
    float increment_ = 0.0F;
};

class DSPNode {
public:
    virtual ~DSPNode() = default;
    virtual bool prepare(const ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;
    virtual void process(AudioBufferView buffer) noexcept = 0;
    [[nodiscard]] virtual std::size_t latencyFrames() const noexcept { return 0; }
};

struct MeterSnapshot {
    float inputPeak = 0.0F;
    float outputPeak = 0.0F;
    float gainReductionDB = 0.0F;
    bool clipped = false;
};

class MeterState {
public:
    void publish(float inputPeak, float outputPeak, float gainReductionDB, bool clipped) noexcept;
    [[nodiscard]] MeterSnapshot snapshot() const noexcept;
    void reset() noexcept;

private:
    std::atomic<float> inputPeak_{0.0F};
    std::atomic<float> outputPeak_{0.0F};
    std::atomic<float> gainReductionDB_{0.0F};
    std::atomic<bool> clipped_{false};
};

class GainNode final : public DSPNode {
public:
    explicit GainNode(float initialGainDB = 0.0F) noexcept;

    void setGainDB(float gainDB) noexcept;
    [[nodiscard]] float gainDB() const noexcept;

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;

private:
    ProcessSpec spec_{};
    std::atomic<float> gainDB_{0.0F};
    LinearSmoother gain_{};
    bool prepared_ = false;
};

enum class FilterType : std::uint8_t {
    lowPass,
    highPass,
    lowShelf,
    highShelf,
    bell,
    bandPass,
    notch,
};

struct BiquadCoefficients {
    float b0 = 1.0F;
    float b1 = 0.0F;
    float b2 = 0.0F;
    float a1 = 0.0F;
    float a2 = 0.0F;

    [[nodiscard]] bool isFinite() const noexcept;
};

class BiquadDesigner {
public:
    [[nodiscard]] static BiquadCoefficients design(
        FilterType type,
        double sampleRate,
        float frequency,
        float q,
        float gainDB) noexcept;
};

class FilterNode final : public DSPNode {
public:
    FilterNode() noexcept;

    void setEnabled(bool enabled) noexcept;
    void setType(FilterType type) noexcept;
    void setFrequency(float frequency) noexcept;
    void setQ(float q) noexcept;
    void setGainDB(float gainDB) noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] FilterType type() const noexcept;
    [[nodiscard]] float frequency() const noexcept;
    [[nodiscard]] float q() const noexcept;
    [[nodiscard]] float gainDB() const noexcept;

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;

private:
    struct State {
        float z1 = 0.0F;
        float z2 = 0.0F;
    };

    [[nodiscard]] float processSample(std::size_t channel, float input) noexcept;

    ProcessSpec spec_{};
    std::atomic<bool> enabled_{true};
    std::atomic<std::uint8_t> type_{static_cast<std::uint8_t>(FilterType::highPass)};
    std::atomic<float> frequency_{30.0F};
    std::atomic<float> q_{0.70710678F};
    std::atomic<float> gainDB_{0.0F};
    LinearSmoother frequencySmoother_{};
    LinearSmoother qSmoother_{};
    LinearSmoother gainSmoother_{};
    LinearSmoother wetSmoother_{};
    BiquadCoefficients coefficients_{};
    FilterType renderedType_ = FilterType::highPass;
    std::array<State, kMaxChannels> states_{};
    bool prepared_ = false;
};

class ParametricEQ final : public DSPNode {
public:
    ParametricEQ() noexcept;

    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void setBandCount(std::size_t count) noexcept;
    [[nodiscard]] std::size_t bandCount() const noexcept;
    [[nodiscard]] FilterNode& band(std::size_t index);
    [[nodiscard]] const FilterNode& band(std::size_t index) const;

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;

private:
    ProcessSpec spec_{};
    std::atomic<bool> enabled_{true};
    std::atomic<std::size_t> bandCount_{0};
    std::array<FilterNode, kMaxEQBands> bands_{};
    LinearSmoother wetSmoother_{};
    std::vector<float> dry_{};
    bool prepared_ = false;
};

enum class BassEnhancerMode : std::uint8_t {
    psychoacoustic,
    extension,
};

class BassEnhancer final : public DSPNode {
public:
    BassEnhancer() noexcept;

    void setEnabled(bool enabled) noexcept;
    void setMode(BassEnhancerMode mode) noexcept;
    void setCrossoverFrequency(float frequency) noexcept;
    void setAmount(float amount) noexcept;
    void setDrive(float drive) noexcept;
    void setMix(float mix) noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] BassEnhancerMode mode() const noexcept;
    [[nodiscard]] float crossoverFrequency() const noexcept;
    [[nodiscard]] float amount() const noexcept;
    [[nodiscard]] float drive() const noexcept;
    [[nodiscard]] float mix() const noexcept;

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;

private:
    ProcessSpec spec_{};
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint8_t> mode_{static_cast<std::uint8_t>(BassEnhancerMode::psychoacoustic)};
    std::atomic<float> crossoverFrequency_{120.0F};
    std::atomic<float> amount_{0.35F};
    std::atomic<float> drive_{2.0F};
    std::atomic<float> mix_{0.35F};
    LinearSmoother crossoverSmoother_{};
    LinearSmoother amountSmoother_{};
    LinearSmoother driveSmoother_{};
    LinearSmoother mixSmoother_{};
    LinearSmoother wetSmoother_{};
    std::array<float, kMaxChannels> lowState_{};
    std::array<float, kMaxChannels> harmonicLowState_{};
    std::array<float, kMaxChannels> envelope_{};
    bool prepared_ = false;
};

class Exciter final : public DSPNode {
public:
    Exciter() noexcept;

    void setEnabled(bool enabled) noexcept;
    void setFrequency(float frequency) noexcept;
    void setDrive(float drive) noexcept;
    void setAmount(float amount) noexcept;
    void setMix(float mix) noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] float frequency() const noexcept;
    [[nodiscard]] float drive() const noexcept;
    [[nodiscard]] float amount() const noexcept;
    [[nodiscard]] float mix() const noexcept;

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;

private:
    ProcessSpec spec_{};
    std::atomic<bool> enabled_{false};
    std::atomic<float> frequency_{5'000.0F};
    std::atomic<float> drive_{2.0F};
    std::atomic<float> amount_{0.2F};
    std::atomic<float> mix_{0.2F};
    LinearSmoother frequencySmoother_{};
    LinearSmoother driveSmoother_{};
    LinearSmoother amountSmoother_{};
    LinearSmoother mixSmoother_{};
    LinearSmoother wetSmoother_{};
    std::array<float, kMaxChannels> lowState_{};
    std::array<float, kMaxChannels> generatedLowState_{};
    bool prepared_ = false;
};

class Crystalizer final : public DSPNode {
public:
    Crystalizer() noexcept;

    void setEnabled(bool enabled) noexcept;
    void setFrequency(float frequency) noexcept;
    void setAmount(float amount) noexcept;
    void setSensitivity(float sensitivity) noexcept;
    void setMix(float mix) noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] float frequency() const noexcept;
    [[nodiscard]] float amount() const noexcept;
    [[nodiscard]] float sensitivity() const noexcept;
    [[nodiscard]] float mix() const noexcept;

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;

private:
    ProcessSpec spec_{};
    std::atomic<bool> enabled_{false};
    std::atomic<float> frequency_{2'500.0F};
    std::atomic<float> amount_{0.35F};
    std::atomic<float> sensitivity_{0.5F};
    std::atomic<float> mix_{0.35F};
    LinearSmoother frequencySmoother_{};
    LinearSmoother amountSmoother_{};
    LinearSmoother sensitivitySmoother_{};
    LinearSmoother mixSmoother_{};
    LinearSmoother wetSmoother_{};
    std::array<float, kMaxChannels> lowState_{};
    std::array<float, kMaxChannels> fastEnvelope_{};
    std::array<float, kMaxChannels> slowEnvelope_{};
    bool prepared_ = false;
};

class StereoTools final : public DSPNode {
public:
    StereoTools() noexcept;

    void setEnabled(bool enabled) noexcept;
    void setWidth(float width) noexcept;
    void setMidGainDB(float gainDB) noexcept;
    void setSideGainDB(float gainDB) noexcept;
    void setBalance(float balance) noexcept;
    void setMonoBassFrequency(float frequency) noexcept;
    void setMonoBassAmount(float amount) noexcept;
    void setSwapChannels(bool swap) noexcept;
    void setMono(bool mono) noexcept;
    void setInvertLeft(bool invert) noexcept;
    void setInvertRight(bool invert) noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] float width() const noexcept;
    [[nodiscard]] float correlation() const noexcept;

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;

private:
    ProcessSpec spec_{};
    std::atomic<bool> enabled_{false};
    std::atomic<float> width_{1.0F};
    std::atomic<float> midGainDB_{0.0F};
    std::atomic<float> sideGainDB_{0.0F};
    std::atomic<float> balance_{0.0F};
    std::atomic<float> monoBassFrequency_{120.0F};
    std::atomic<float> monoBassAmount_{1.0F};
    std::atomic<bool> swapChannels_{false};
    std::atomic<bool> mono_{false};
    std::atomic<bool> invertLeft_{false};
    std::atomic<bool> invertRight_{false};
    std::atomic<float> correlation_{0.0F};
    LinearSmoother widthSmoother_{};
    LinearSmoother midGainSmoother_{};
    LinearSmoother sideGainSmoother_{};
    LinearSmoother balanceSmoother_{};
    LinearSmoother monoBassFrequencySmoother_{};
    LinearSmoother monoBassSmoother_{};
    LinearSmoother wetSmoother_{};
    float lowLeft_ = 0.0F;
    float lowRight_ = 0.0F;
    bool prepared_ = false;
};

class TruePeakLimiter final : public DSPNode {
public:
    TruePeakLimiter() noexcept;

    void setEnabled(bool enabled) noexcept;
    void setCeilingDB(float ceilingDB) noexcept;
    void setLookaheadMilliseconds(float milliseconds) noexcept;
    void setReleaseMilliseconds(float milliseconds) noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] float ceilingDB() const noexcept;
    [[nodiscard]] float lookaheadMilliseconds() const noexcept;
    [[nodiscard]] float releaseMilliseconds() const noexcept;
    [[nodiscard]] float gainReductionDB() const noexcept;

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;
    [[nodiscard]] std::size_t latencyFrames() const noexcept override;

private:
    static constexpr float kMaximumLookaheadMilliseconds = 20.0F;
    static constexpr std::size_t kOversamplingFactor = 4;
    static constexpr std::size_t kInterpolationTaps = 16;
    static constexpr std::size_t kDetectorLatencyFrames = kInterpolationTaps / 2;

    ProcessSpec spec_{};
    std::atomic<bool> enabled_{true};
    std::atomic<float> ceilingDB_{-1.0F};
    std::atomic<float> lookaheadMilliseconds_{5.0F};
    std::atomic<float> releaseMilliseconds_{100.0F};
    std::atomic<float> gainReductionDB_{0.0F};
    std::vector<float> delay_{};
    std::vector<float> detectorHistory_{};
    std::array<std::array<float, kInterpolationTaps>, kOversamplingFactor> detectorCoefficients_{};
    std::size_t delayStride_ = 0;
    std::size_t delayFrames_ = 0;
    std::size_t lookaheadFrames_ = 0;
    std::size_t writeIndex_ = 0;
    std::size_t detectorWriteIndex_ = 0;
    std::size_t holdFramesRemaining_ = 0;
    float envelopeGain_ = 1.0F;
    bool prepared_ = false;
};

class DSPChain final : public DSPNode {
public:
    DSPChain() noexcept;

    void setBypassed(bool bypassed) noexcept;
    [[nodiscard]] bool bypassed() const noexcept;
    void setOutputGainDB(float gainDB) noexcept;

    [[nodiscard]] GainNode& inputGain() noexcept { return inputGain_; }
    [[nodiscard]] FilterNode& filter() noexcept { return filter_; }
    [[nodiscard]] ParametricEQ& equalizer() noexcept { return equalizer_; }
    [[nodiscard]] BassEnhancer& bassEnhancer() noexcept { return bassEnhancer_; }
    [[nodiscard]] Exciter& exciter() noexcept { return exciter_; }
    [[nodiscard]] Crystalizer& crystalizer() noexcept { return crystalizer_; }
    [[nodiscard]] StereoTools& stereoTools() noexcept { return stereoTools_; }
    [[nodiscard]] const StereoTools& stereoTools() const noexcept { return stereoTools_; }
    [[nodiscard]] TruePeakLimiter& limiter() noexcept { return limiter_; }
    [[nodiscard]] const MeterState& meters() const noexcept { return meters_; }

    bool prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBufferView buffer) noexcept override;
    [[nodiscard]] std::size_t latencyFrames() const noexcept override;

private:
    ProcessSpec spec_{};
    GainNode inputGain_{};
    FilterNode filter_{};
    ParametricEQ equalizer_{};
    BassEnhancer bassEnhancer_{};
    Exciter exciter_{};
    Crystalizer crystalizer_{};
    StereoTools stereoTools_{};
    TruePeakLimiter limiter_{};
    GainNode outputGain_{};
    MeterState meters_{};
    std::atomic<bool> bypassed_{false};
    LinearSmoother processedMix_{};
    std::vector<float> dryDelay_{};
    std::vector<float> dryBlock_{};
    std::size_t dryDelayFrames_ = 0;
    std::size_t dryWriteIndex_ = 0;
    bool prepared_ = false;
};

} // namespace phoenaux::dsp
