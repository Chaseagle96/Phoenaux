#include <PhoenauxDSP/PhoenauxDSP.hpp>
#include <PhoenauxDSP/PhoenauxDSP.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace phoenaux::dsp {
namespace {

static_assert(std::atomic<float>::is_always_lock_free,
    "PhoenauxDSP requires lock-free float atomics on the render platform");
static_assert(std::atomic<std::size_t>::is_always_lock_free,
    "PhoenauxDSP requires lock-free size atomics on the render platform");
static_assert(std::atomic<bool>::is_always_lock_free,
    "PhoenauxDSP requires lock-free bool atomics on the render platform");
static_assert(std::atomic<std::uint8_t>::is_always_lock_free,
    "PhoenauxDSP requires lock-free byte atomics on the render platform");

constexpr float kMinimumDecibels = -120.0F;
constexpr float kMaximumGainDecibels = 24.0F;
constexpr float kMinimumFrequency = 5.0F;
constexpr float kMinimumQ = 0.05F;
constexpr float kMaximumQ = 40.0F;
constexpr double kPi = 3.14159265358979323846264338327950288;

[[nodiscard]] float finiteOrZero(float value) noexcept {
    return std::isfinite(value) && std::abs(value) >= 1.0e-30F ? value : 0.0F;
}

[[nodiscard]] float clampGainDB(float value) noexcept {
    return std::clamp(finiteOrZero(value), -kMaximumGainDecibels, kMaximumGainDecibels);
}

[[nodiscard]] float clampFrequency(float value, double sampleRate) noexcept {
    const auto upper = static_cast<float>(std::max(10.0, sampleRate * 0.495));
    return std::clamp(finiteOrZero(value), kMinimumFrequency, upper);
}

[[nodiscard]] float onePoleCoefficient(float frequency, double sampleRate) noexcept {
    const auto safeFrequency = static_cast<double>(clampFrequency(frequency, sampleRate));
    return static_cast<float>(1.0 - std::exp(-2.0 * kPi * safeFrequency / sampleRate));
}

[[nodiscard]] float measurePeak(AudioBufferView buffer) noexcept {
    float peak = 0.0F;
    for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
        const auto* samples = buffer.channels[channel];
        for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
            peak = std::max(peak, std::abs(finiteOrZero(samples[frame])));
        }
    }
    return peak;
}

} // namespace

bool ProcessSpec::isValid() const noexcept {
    return std::isfinite(sampleRate) && sampleRate >= 8'000.0 && sampleRate <= 768'000.0
        && maximumFrameCount != 0 && maximumChannelCount != 0
        && maximumChannelCount <= kMaxChannels;
}

bool AudioBufferView::isValidFor(const ProcessSpec& spec) const noexcept {
    if (!spec.isValid() || channels == nullptr || channelCount == 0
        || channelCount > spec.maximumChannelCount || frameCount > spec.maximumFrameCount) {
        return false;
    }
    for (std::size_t channel = 0; channel < channelCount; ++channel) {
        if (channels[channel] == nullptr) {
            return false;
        }
    }
    return true;
}

float decibelsToLinear(float decibels) noexcept {
    if (!std::isfinite(decibels) || decibels <= kMinimumDecibels) {
        return 0.0F;
    }
    return std::pow(10.0F, decibels / 20.0F);
}

float linearToDecibels(float linear) noexcept {
    if (!std::isfinite(linear) || linear <= 0.0F) {
        return kMinimumDecibels;
    }
    return std::max(kMinimumDecibels, 20.0F * std::log10(linear));
}

void LinearSmoother::prepare(double sampleRate, double rampMilliseconds) noexcept {
    const auto requested = sampleRate * std::max(0.0, rampMilliseconds) / 1'000.0;
    const auto bounded = std::clamp(requested, 1.0, static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
    rampSamples_ = static_cast<std::uint32_t>(std::llround(bounded));
}

void LinearSmoother::reset(float value) noexcept {
    current_ = finiteOrZero(value);
    target_ = current_;
    increment_ = 0.0F;
    remaining_ = 0;
}

void LinearSmoother::setTarget(float value) noexcept {
    const auto safeValue = finiteOrZero(value);
    if (safeValue == target_) {
        return;
    }
    target_ = safeValue;
    remaining_ = rampSamples_;
    increment_ = (target_ - current_) / static_cast<float>(remaining_);
}

float LinearSmoother::next() noexcept {
    if (remaining_ == 0) {
        return current_;
    }
    current_ += increment_;
    --remaining_;
    if (remaining_ == 0) {
        current_ = target_;
    }
    return current_;
}

void MeterState::publish(float inputPeak, float outputPeak, float gainReductionDB, bool clipped) noexcept {
    inputPeak_.store(finiteOrZero(inputPeak), std::memory_order_relaxed);
    outputPeak_.store(finiteOrZero(outputPeak), std::memory_order_relaxed);
    gainReductionDB_.store(finiteOrZero(gainReductionDB), std::memory_order_relaxed);
    clipped_.store(clipped, std::memory_order_release);
}

MeterSnapshot MeterState::snapshot() const noexcept {
    MeterSnapshot result{};
    result.inputPeak = inputPeak_.load(std::memory_order_relaxed);
    result.outputPeak = outputPeak_.load(std::memory_order_relaxed);
    result.gainReductionDB = gainReductionDB_.load(std::memory_order_relaxed);
    result.clipped = clipped_.load(std::memory_order_acquire);
    return result;
}

void MeterState::reset() noexcept {
    publish(0.0F, 0.0F, 0.0F, false);
}

GainNode::GainNode(float initialGainDB) noexcept
    : gainDB_(clampGainDB(initialGainDB)) {}

void GainNode::setGainDB(float gainDB) noexcept {
    gainDB_.store(clampGainDB(gainDB), std::memory_order_relaxed);
}

float GainNode::gainDB() const noexcept {
    return gainDB_.load(std::memory_order_relaxed);
}

bool GainNode::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    gain_.prepare(spec.sampleRate, 10.0);
    gain_.reset(decibelsToLinear(gainDB()));
    prepared_ = true;
    return true;
}

void GainNode::reset() noexcept {
    gain_.reset(decibelsToLinear(gainDB()));
}

void GainNode::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_)) {
        return;
    }
    gain_.setTarget(decibelsToLinear(gainDB()));
    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        const auto gain = gain_.next();
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            buffer.channels[channel][frame] = finiteOrZero(buffer.channels[channel][frame]) * gain;
        }
    }
}

bool BiquadCoefficients::isFinite() const noexcept {
    return std::isfinite(b0) && std::isfinite(b1) && std::isfinite(b2)
        && std::isfinite(a1) && std::isfinite(a2);
}

BiquadCoefficients BiquadDesigner::design(
    FilterType type,
    double sampleRate,
    float frequency,
    float q,
    float gainDB) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
        return {};
    }

    const auto safeFrequency = static_cast<double>(clampFrequency(frequency, sampleRate));
    const auto safeQ = static_cast<double>(std::clamp(finiteOrZero(q), kMinimumQ, kMaximumQ));
    const auto safeGain = static_cast<double>(clampGainDB(gainDB));
    const auto omega = 2.0 * kPi * safeFrequency / sampleRate;
    const auto sine = std::sin(omega);
    const auto cosine = std::cos(omega);
    const auto alpha = sine / (2.0 * safeQ);
    const auto amplitude = std::pow(10.0, safeGain / 40.0);
    const auto squareRootAmplitude = std::sqrt(amplitude);

    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;

    switch (type) {
    case FilterType::lowPass:
        b0 = (1.0 - cosine) * 0.5;
        b1 = 1.0 - cosine;
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
        break;
    case FilterType::highPass:
        b0 = (1.0 + cosine) * 0.5;
        b1 = -(1.0 + cosine);
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
        break;
    case FilterType::bandPass:
        b0 = sine * 0.5;
        b1 = 0.0;
        b2 = -sine * 0.5;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
        break;
    case FilterType::notch:
        b0 = 1.0;
        b1 = -2.0 * cosine;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
        break;
    case FilterType::bell:
        b0 = 1.0 + alpha * amplitude;
        b1 = -2.0 * cosine;
        b2 = 1.0 - alpha * amplitude;
        a0 = 1.0 + alpha / amplitude;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha / amplitude;
        break;
    case FilterType::lowShelf: {
        const auto shelfAlpha = sine * std::sqrt(2.0) * 0.5;
        const auto beta = 2.0 * squareRootAmplitude * shelfAlpha;
        b0 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine + beta);
        b1 = 2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cosine);
        b2 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine - beta);
        a0 = (amplitude + 1.0) + (amplitude - 1.0) * cosine + beta;
        a1 = -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cosine);
        a2 = (amplitude + 1.0) + (amplitude - 1.0) * cosine - beta;
        break;
    }
    case FilterType::highShelf: {
        const auto shelfAlpha = sine * std::sqrt(2.0) * 0.5;
        const auto beta = 2.0 * squareRootAmplitude * shelfAlpha;
        b0 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine + beta);
        b1 = -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosine);
        b2 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine - beta);
        a0 = (amplitude + 1.0) - (amplitude - 1.0) * cosine + beta;
        a1 = 2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosine);
        a2 = (amplitude + 1.0) - (amplitude - 1.0) * cosine - beta;
        break;
    }
    }

    if (!std::isfinite(a0) || std::abs(a0) < std::numeric_limits<double>::epsilon()) {
        return {};
    }

    BiquadCoefficients result{
        static_cast<float>(b0 / a0),
        static_cast<float>(b1 / a0),
        static_cast<float>(b2 / a0),
        static_cast<float>(a1 / a0),
        static_cast<float>(a2 / a0),
    };
    return result.isFinite() ? result : BiquadCoefficients{};
}

FilterNode::FilterNode() noexcept = default;

void FilterNode::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_relaxed);
}

void FilterNode::setType(FilterType type) noexcept {
    type_.store(static_cast<std::uint8_t>(type), std::memory_order_relaxed);
}

void FilterNode::setFrequency(float frequency) noexcept {
    const auto sampleRate = spec_.sampleRate > 0.0 ? spec_.sampleRate : 48'000.0;
    frequency_.store(clampFrequency(frequency, sampleRate), std::memory_order_relaxed);
}

void FilterNode::setQ(float q) noexcept {
    q_.store(std::clamp(finiteOrZero(q), kMinimumQ, kMaximumQ), std::memory_order_relaxed);
}

void FilterNode::setGainDB(float gainDB) noexcept {
    gainDB_.store(clampGainDB(gainDB), std::memory_order_relaxed);
}

bool FilterNode::enabled() const noexcept {
    return enabled_.load(std::memory_order_relaxed);
}

FilterType FilterNode::type() const noexcept {
    const auto raw = type_.load(std::memory_order_relaxed);
    if (raw > static_cast<std::uint8_t>(FilterType::notch)) {
        return FilterType::highPass;
    }
    return static_cast<FilterType>(raw);
}

float FilterNode::frequency() const noexcept {
    return frequency_.load(std::memory_order_relaxed);
}

float FilterNode::q() const noexcept {
    return q_.load(std::memory_order_relaxed);
}

float FilterNode::gainDB() const noexcept {
    return gainDB_.load(std::memory_order_relaxed);
}

bool FilterNode::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    frequency_.store(clampFrequency(frequency(), spec.sampleRate), std::memory_order_relaxed);
    frequencySmoother_.prepare(spec.sampleRate, 15.0);
    qSmoother_.prepare(spec.sampleRate, 15.0);
    gainSmoother_.prepare(spec.sampleRate, 15.0);
    wetSmoother_.prepare(spec.sampleRate, 5.0);
    frequencySmoother_.reset(frequency());
    qSmoother_.reset(q());
    gainSmoother_.reset(gainDB());
    wetSmoother_.reset(enabled() ? 1.0F : 0.0F);
    renderedType_ = type();
    coefficients_ = BiquadDesigner::design(renderedType_, spec.sampleRate, frequency(), q(), gainDB());
    reset();
    prepared_ = true;
    return true;
}

void FilterNode::reset() noexcept {
    for (auto& state : states_) {
        state = {};
    }
    frequencySmoother_.reset(frequency());
    qSmoother_.reset(q());
    gainSmoother_.reset(gainDB());
    wetSmoother_.reset(enabled() ? 1.0F : 0.0F);
}

float FilterNode::processSample(std::size_t channel, float input) noexcept {
    auto& state = states_[channel];
    const auto output = coefficients_.b0 * input + state.z1;
    state.z1 = coefficients_.b1 * input - coefficients_.a1 * output + state.z2;
    state.z2 = coefficients_.b2 * input - coefficients_.a2 * output;
    if (!std::isfinite(output) || !std::isfinite(state.z1) || !std::isfinite(state.z2)) {
        state = {};
        return 0.0F;
    }
    state.z1 = finiteOrZero(state.z1);
    state.z2 = finiteOrZero(state.z2);
    return finiteOrZero(output);
}

void FilterNode::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_)) {
        return;
    }

    frequencySmoother_.setTarget(clampFrequency(frequency(), spec_.sampleRate));
    qSmoother_.setTarget(std::clamp(q(), kMinimumQ, kMaximumQ));
    gainSmoother_.setTarget(clampGainDB(gainDB()));
    wetSmoother_.setTarget(enabled() ? 1.0F : 0.0F);
    const auto selectedType = type();
    auto parametersMoving = frequencySmoother_.isSmoothing()
        || qSmoother_.isSmoothing() || gainSmoother_.isSmoothing();
    auto typeChanged = selectedType != renderedType_;

    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        const auto smoothedFrequency = frequencySmoother_.next();
        const auto smoothedQ = qSmoother_.next();
        const auto smoothedGain = gainSmoother_.next();
        if (parametersMoving || typeChanged) {
            coefficients_ = BiquadDesigner::design(
                selectedType, spec_.sampleRate, smoothedFrequency, smoothedQ, smoothedGain);
            renderedType_ = selectedType;
            typeChanged = false;
            parametersMoving = frequencySmoother_.isSmoothing()
                || qSmoother_.isSmoothing() || gainSmoother_.isSmoothing();
        }
        const auto wet = wetSmoother_.next();
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            const auto dry = finiteOrZero(buffer.channels[channel][frame]);
            const auto filtered = processSample(channel, dry);
            buffer.channels[channel][frame] = dry + (filtered - dry) * wet;
        }
    }
}

ParametricEQ::ParametricEQ() noexcept {
    for (auto& filterBand : bands_) {
        filterBand.setEnabled(false);
        filterBand.setType(FilterType::bell);
        filterBand.setFrequency(1'000.0F);
        filterBand.setQ(1.0F);
        filterBand.setGainDB(0.0F);
    }
}

void ParametricEQ::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_relaxed);
}

bool ParametricEQ::enabled() const noexcept {
    return enabled_.load(std::memory_order_relaxed);
}

void ParametricEQ::setBandCount(std::size_t count) noexcept {
    bandCount_.store(std::min(count, kMaxEQBands), std::memory_order_relaxed);
}

std::size_t ParametricEQ::bandCount() const noexcept {
    return std::min(bandCount_.load(std::memory_order_relaxed), kMaxEQBands);
}

FilterNode& ParametricEQ::band(std::size_t index) {
    if (index >= kMaxEQBands) {
        throw std::out_of_range("Phoenaux EQ band index is out of range");
    }
    return bands_[index];
}

const FilterNode& ParametricEQ::band(std::size_t index) const {
    if (index >= kMaxEQBands) {
        throw std::out_of_range("Phoenaux EQ band index is out of range");
    }
    return bands_[index];
}

bool ParametricEQ::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    dry_.assign(spec.maximumChannelCount * spec.maximumFrameCount, 0.0F);
    wetSmoother_.prepare(spec.sampleRate, 5.0);
    wetSmoother_.reset(enabled() ? 1.0F : 0.0F);
    for (auto& filterBand : bands_) {
        if (!filterBand.prepare(spec)) {
            return false;
        }
    }
    prepared_ = true;
    return true;
}

void ParametricEQ::reset() noexcept {
    std::fill(dry_.begin(), dry_.end(), 0.0F);
    wetSmoother_.reset(enabled() ? 1.0F : 0.0F);
    for (auto& filterBand : bands_) {
        filterBand.reset();
    }
}

void ParametricEQ::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_)) {
        return;
    }

    for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
        auto* destination = dry_.data() + channel * spec_.maximumFrameCount;
        std::copy_n(buffer.channels[channel], buffer.frameCount, destination);
    }

    const auto activeBandCount = bandCount();
    for (std::size_t index = 0; index < activeBandCount; ++index) {
        bands_[index].process(buffer);
    }

    wetSmoother_.setTarget(enabled() ? 1.0F : 0.0F);
    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        const auto wet = wetSmoother_.next();
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            const auto dry = dry_[channel * spec_.maximumFrameCount + frame];
            const auto processed = finiteOrZero(buffer.channels[channel][frame]);
            buffer.channels[channel][frame] = dry + (processed - dry) * wet;
        }
    }
}

BassEnhancer::BassEnhancer() noexcept = default;

void BassEnhancer::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_relaxed);
}

void BassEnhancer::setMode(BassEnhancerMode mode) noexcept {
    mode_.store(static_cast<std::uint8_t>(mode), std::memory_order_relaxed);
}

void BassEnhancer::setCrossoverFrequency(float frequency) noexcept {
    const auto sampleRate = spec_.sampleRate > 0.0 ? spec_.sampleRate : 48'000.0;
    crossoverFrequency_.store(
        std::clamp(clampFrequency(frequency, sampleRate), 40.0F, 300.0F),
        std::memory_order_relaxed);
}

void BassEnhancer::setAmount(float amount) noexcept {
    amount_.store(std::clamp(finiteOrZero(amount), 0.0F, 1.0F), std::memory_order_relaxed);
}

void BassEnhancer::setDrive(float drive) noexcept {
    drive_.store(std::clamp(finiteOrZero(drive), 1.0F, 6.0F), std::memory_order_relaxed);
}

void BassEnhancer::setMix(float mix) noexcept {
    mix_.store(std::clamp(finiteOrZero(mix), 0.0F, 1.0F), std::memory_order_relaxed);
}

bool BassEnhancer::enabled() const noexcept {
    return enabled_.load(std::memory_order_relaxed);
}

BassEnhancerMode BassEnhancer::mode() const noexcept {
    return mode_.load(std::memory_order_relaxed) == static_cast<std::uint8_t>(BassEnhancerMode::extension)
        ? BassEnhancerMode::extension : BassEnhancerMode::psychoacoustic;
}

float BassEnhancer::crossoverFrequency() const noexcept {
    return crossoverFrequency_.load(std::memory_order_relaxed);
}

float BassEnhancer::amount() const noexcept {
    return amount_.load(std::memory_order_relaxed);
}

float BassEnhancer::drive() const noexcept {
    return drive_.load(std::memory_order_relaxed);
}

float BassEnhancer::mix() const noexcept {
    return mix_.load(std::memory_order_relaxed);
}

bool BassEnhancer::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    crossoverSmoother_.prepare(spec.sampleRate, 20.0);
    amountSmoother_.prepare(spec.sampleRate, 20.0);
    driveSmoother_.prepare(spec.sampleRate, 20.0);
    mixSmoother_.prepare(spec.sampleRate, 20.0);
    wetSmoother_.prepare(spec.sampleRate, 5.0);
    prepared_ = true;
    reset();
    return true;
}

void BassEnhancer::reset() noexcept {
    lowState_.fill(0.0F);
    harmonicLowState_.fill(0.0F);
    envelope_.fill(0.0F);
    crossoverSmoother_.reset(crossoverFrequency());
    amountSmoother_.reset(amount());
    driveSmoother_.reset(drive());
    mixSmoother_.reset(mix());
    wetSmoother_.reset(enabled() ? 1.0F : 0.0F);
}

void BassEnhancer::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_)) {
        return;
    }
    crossoverSmoother_.setTarget(crossoverFrequency());
    amountSmoother_.setTarget(amount());
    driveSmoother_.setTarget(drive());
    mixSmoother_.setTarget(mix());
    wetSmoother_.setTarget(enabled() ? 1.0F : 0.0F);
    const auto selectedMode = mode();
    const auto envelopeRelease = static_cast<float>(std::exp(-1.0 / (0.080 * spec_.sampleRate)));

    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        const auto crossover = crossoverSmoother_.next();
        const auto lowCoefficient = onePoleCoefficient(crossover, spec_.sampleRate);
        const auto harmonicCoefficient = onePoleCoefficient(crossover * 1.5F, spec_.sampleRate);
        const auto amountValue = amountSmoother_.next();
        const auto driveValue = driveSmoother_.next();
        const auto mixValue = mixSmoother_.next();
        const auto wet = wetSmoother_.next();
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            const auto dry = finiteOrZero(buffer.channels[channel][frame]);
            lowState_[channel] = finiteOrZero(
                lowState_[channel] + lowCoefficient * (dry - lowState_[channel]));
            const auto low = lowState_[channel];
            const auto magnitude = std::abs(low);
            envelope_[channel] = finiteOrZero(magnitude > envelope_[channel]
                ? magnitude
                : envelopeRelease * envelope_[channel] + (1.0F - envelopeRelease) * magnitude);
            const auto protection = 1.0F / (1.0F + 3.0F * envelope_[channel]);

            float enhancement = 0.0F;
            if (selectedMode == BassEnhancerMode::psychoacoustic) {
                const auto shaped = std::tanh(low * driveValue);
                harmonicLowState_[channel] = finiteOrZero(harmonicLowState_[channel]
                    + harmonicCoefficient * (shaped - harmonicLowState_[channel]));
                enhancement = (shaped - harmonicLowState_[channel]) * amountValue * protection;
            } else {
                enhancement = low * amountValue * 0.75F * protection;
            }
            const auto processed = dry + enhancement * mixValue;
            buffer.channels[channel][frame] = finiteOrZero(dry + (processed - dry) * wet);
        }
    }
}

Exciter::Exciter() noexcept = default;

void Exciter::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_relaxed);
}

void Exciter::setFrequency(float frequency) noexcept {
    const auto sampleRate = spec_.sampleRate > 0.0 ? spec_.sampleRate : 48'000.0;
    frequency_.store(std::clamp(clampFrequency(frequency, sampleRate), 1'000.0F,
        static_cast<float>(sampleRate * 0.45)), std::memory_order_relaxed);
}

void Exciter::setDrive(float drive) noexcept {
    drive_.store(std::clamp(finiteOrZero(drive), 1.0F, 8.0F), std::memory_order_relaxed);
}

void Exciter::setAmount(float amount) noexcept {
    amount_.store(std::clamp(finiteOrZero(amount), 0.0F, 1.0F), std::memory_order_relaxed);
}

void Exciter::setMix(float mix) noexcept {
    mix_.store(std::clamp(finiteOrZero(mix), 0.0F, 1.0F), std::memory_order_relaxed);
}

bool Exciter::enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }
float Exciter::frequency() const noexcept { return frequency_.load(std::memory_order_relaxed); }
float Exciter::drive() const noexcept { return drive_.load(std::memory_order_relaxed); }
float Exciter::amount() const noexcept { return amount_.load(std::memory_order_relaxed); }
float Exciter::mix() const noexcept { return mix_.load(std::memory_order_relaxed); }

bool Exciter::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    frequencySmoother_.prepare(spec.sampleRate, 20.0);
    driveSmoother_.prepare(spec.sampleRate, 20.0);
    amountSmoother_.prepare(spec.sampleRate, 20.0);
    mixSmoother_.prepare(spec.sampleRate, 20.0);
    wetSmoother_.prepare(spec.sampleRate, 5.0);
    prepared_ = true;
    reset();
    return true;
}

void Exciter::reset() noexcept {
    lowState_.fill(0.0F);
    generatedLowState_.fill(0.0F);
    frequencySmoother_.reset(frequency());
    driveSmoother_.reset(drive());
    amountSmoother_.reset(amount());
    mixSmoother_.reset(mix());
    wetSmoother_.reset(enabled() ? 1.0F : 0.0F);
}

void Exciter::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_)) {
        return;
    }
    frequencySmoother_.setTarget(frequency());
    driveSmoother_.setTarget(drive());
    amountSmoother_.setTarget(amount());
    mixSmoother_.setTarget(mix());
    wetSmoother_.setTarget(enabled() ? 1.0F : 0.0F);
    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        const auto cutoff = frequencySmoother_.next();
        const auto coefficient = onePoleCoefficient(cutoff, spec_.sampleRate);
        const auto driveValue = driveSmoother_.next();
        const auto amountValue = amountSmoother_.next();
        const auto mixValue = mixSmoother_.next();
        const auto wet = wetSmoother_.next();
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            const auto dry = finiteOrZero(buffer.channels[channel][frame]);
            lowState_[channel] = finiteOrZero(
                lowState_[channel] + coefficient * (dry - lowState_[channel]));
            const auto high = dry - lowState_[channel];
            const auto saturated = std::tanh(high * driveValue);
            const auto residual = saturated - high;
            generatedLowState_[channel] = finiteOrZero(generatedLowState_[channel]
                + coefficient * (residual - generatedLowState_[channel]));
            const auto generatedHigh = std::clamp(
                residual - generatedLowState_[channel], -0.5F, 0.5F);
            const auto processed = dry + generatedHigh * amountValue * mixValue;
            buffer.channels[channel][frame] = finiteOrZero(dry + (processed - dry) * wet);
        }
    }
}

Crystalizer::Crystalizer() noexcept = default;

void Crystalizer::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_relaxed);
}

void Crystalizer::setFrequency(float frequency) noexcept {
    const auto sampleRate = spec_.sampleRate > 0.0 ? spec_.sampleRate : 48'000.0;
    frequency_.store(std::clamp(clampFrequency(frequency, sampleRate), 500.0F,
        static_cast<float>(sampleRate * 0.4)), std::memory_order_relaxed);
}

void Crystalizer::setAmount(float amount) noexcept {
    amount_.store(std::clamp(finiteOrZero(amount), 0.0F, 1.5F), std::memory_order_relaxed);
}

void Crystalizer::setSensitivity(float sensitivity) noexcept {
    sensitivity_.store(std::clamp(finiteOrZero(sensitivity), 0.0F, 1.0F), std::memory_order_relaxed);
}

void Crystalizer::setMix(float mix) noexcept {
    mix_.store(std::clamp(finiteOrZero(mix), 0.0F, 1.0F), std::memory_order_relaxed);
}

bool Crystalizer::enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }
float Crystalizer::frequency() const noexcept { return frequency_.load(std::memory_order_relaxed); }
float Crystalizer::amount() const noexcept { return amount_.load(std::memory_order_relaxed); }
float Crystalizer::sensitivity() const noexcept { return sensitivity_.load(std::memory_order_relaxed); }
float Crystalizer::mix() const noexcept { return mix_.load(std::memory_order_relaxed); }

bool Crystalizer::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    frequencySmoother_.prepare(spec.sampleRate, 20.0);
    amountSmoother_.prepare(spec.sampleRate, 20.0);
    sensitivitySmoother_.prepare(spec.sampleRate, 20.0);
    mixSmoother_.prepare(spec.sampleRate, 20.0);
    wetSmoother_.prepare(spec.sampleRate, 5.0);
    prepared_ = true;
    reset();
    return true;
}

void Crystalizer::reset() noexcept {
    lowState_.fill(0.0F);
    fastEnvelope_.fill(0.0F);
    slowEnvelope_.fill(0.0F);
    frequencySmoother_.reset(frequency());
    amountSmoother_.reset(amount());
    sensitivitySmoother_.reset(sensitivity());
    mixSmoother_.reset(mix());
    wetSmoother_.reset(enabled() ? 1.0F : 0.0F);
}

void Crystalizer::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_)) {
        return;
    }
    frequencySmoother_.setTarget(frequency());
    amountSmoother_.setTarget(amount());
    sensitivitySmoother_.setTarget(sensitivity());
    mixSmoother_.setTarget(mix());
    wetSmoother_.setTarget(enabled() ? 1.0F : 0.0F);
    const auto fastAttack = static_cast<float>(std::exp(-1.0 / (0.001 * spec_.sampleRate)));
    const auto fastRelease = static_cast<float>(std::exp(-1.0 / (0.020 * spec_.sampleRate)));
    const auto slowCoefficient = static_cast<float>(std::exp(-1.0 / (0.080 * spec_.sampleRate)));

    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        const auto coefficient = onePoleCoefficient(frequencySmoother_.next(), spec_.sampleRate);
        const auto amountValue = amountSmoother_.next();
        const auto sensitivityValue = sensitivitySmoother_.next();
        const auto mixValue = mixSmoother_.next();
        const auto wet = wetSmoother_.next();
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            const auto dry = finiteOrZero(buffer.channels[channel][frame]);
            lowState_[channel] = finiteOrZero(
                lowState_[channel] + coefficient * (dry - lowState_[channel]));
            const auto detail = dry - lowState_[channel];
            const auto magnitude = std::abs(detail);
            const auto fastCoefficient = magnitude > fastEnvelope_[channel] ? fastAttack : fastRelease;
            fastEnvelope_[channel] = finiteOrZero(fastCoefficient * fastEnvelope_[channel]
                + (1.0F - fastCoefficient) * magnitude);
            slowEnvelope_[channel] = finiteOrZero(slowCoefficient * slowEnvelope_[channel]
                + (1.0F - slowCoefficient) * magnitude);
            const auto contrast = std::max(0.0F, fastEnvelope_[channel] - slowEnvelope_[channel]);
            const auto normalized = contrast / (slowEnvelope_[channel] + 0.001F);
            const auto gate = std::clamp(
                (normalized - (1.0F - sensitivityValue) * 0.5F) * 2.0F, 0.0F, 1.0F);
            const auto enhancementGain = amountValue * gate;
            const auto processed = dry + detail * enhancementGain * mixValue;
            buffer.channels[channel][frame] = finiteOrZero(dry + (processed - dry) * wet);
        }
    }
}

StereoTools::StereoTools() noexcept = default;

void StereoTools::setEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_relaxed); }
void StereoTools::setWidth(float width) noexcept {
    width_.store(std::clamp(finiteOrZero(width), 0.0F, 2.0F), std::memory_order_relaxed);
}
void StereoTools::setMidGainDB(float gainDB) noexcept { midGainDB_.store(clampGainDB(gainDB), std::memory_order_relaxed); }
void StereoTools::setSideGainDB(float gainDB) noexcept { sideGainDB_.store(clampGainDB(gainDB), std::memory_order_relaxed); }
void StereoTools::setBalance(float balance) noexcept {
    balance_.store(std::clamp(finiteOrZero(balance), -1.0F, 1.0F), std::memory_order_relaxed);
}
void StereoTools::setMonoBassFrequency(float frequency) noexcept {
    const auto sampleRate = spec_.sampleRate > 0.0 ? spec_.sampleRate : 48'000.0;
    monoBassFrequency_.store(std::clamp(clampFrequency(frequency, sampleRate), 40.0F, 400.0F), std::memory_order_relaxed);
}
void StereoTools::setMonoBassAmount(float amount) noexcept {
    monoBassAmount_.store(std::clamp(finiteOrZero(amount), 0.0F, 1.0F), std::memory_order_relaxed);
}
void StereoTools::setSwapChannels(bool swap) noexcept { swapChannels_.store(swap, std::memory_order_relaxed); }
void StereoTools::setMono(bool mono) noexcept { mono_.store(mono, std::memory_order_relaxed); }
void StereoTools::setInvertLeft(bool invert) noexcept { invertLeft_.store(invert, std::memory_order_relaxed); }
void StereoTools::setInvertRight(bool invert) noexcept { invertRight_.store(invert, std::memory_order_relaxed); }
bool StereoTools::enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }
float StereoTools::width() const noexcept { return width_.load(std::memory_order_relaxed); }
float StereoTools::correlation() const noexcept { return correlation_.load(std::memory_order_relaxed); }

bool StereoTools::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    widthSmoother_.prepare(spec.sampleRate, 20.0);
    midGainSmoother_.prepare(spec.sampleRate, 20.0);
    sideGainSmoother_.prepare(spec.sampleRate, 20.0);
    balanceSmoother_.prepare(spec.sampleRate, 20.0);
    monoBassFrequencySmoother_.prepare(spec.sampleRate, 20.0);
    monoBassSmoother_.prepare(spec.sampleRate, 20.0);
    wetSmoother_.prepare(spec.sampleRate, 5.0);
    prepared_ = true;
    reset();
    return true;
}

void StereoTools::reset() noexcept {
    lowLeft_ = 0.0F;
    lowRight_ = 0.0F;
    widthSmoother_.reset(width());
    midGainSmoother_.reset(decibelsToLinear(midGainDB_.load(std::memory_order_relaxed)));
    sideGainSmoother_.reset(decibelsToLinear(sideGainDB_.load(std::memory_order_relaxed)));
    balanceSmoother_.reset(balance_.load(std::memory_order_relaxed));
    monoBassFrequencySmoother_.reset(monoBassFrequency_.load(std::memory_order_relaxed));
    monoBassSmoother_.reset(monoBassAmount_.load(std::memory_order_relaxed));
    wetSmoother_.reset(enabled() ? 1.0F : 0.0F);
    correlation_.store(0.0F, std::memory_order_relaxed);
}

void StereoTools::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_) || buffer.channelCount < 2) {
        return;
    }
    widthSmoother_.setTarget(width());
    midGainSmoother_.setTarget(decibelsToLinear(midGainDB_.load(std::memory_order_relaxed)));
    sideGainSmoother_.setTarget(decibelsToLinear(sideGainDB_.load(std::memory_order_relaxed)));
    balanceSmoother_.setTarget(balance_.load(std::memory_order_relaxed));
    monoBassFrequencySmoother_.setTarget(monoBassFrequency_.load(std::memory_order_relaxed));
    monoBassSmoother_.setTarget(monoBassAmount_.load(std::memory_order_relaxed));
    wetSmoother_.setTarget(enabled() ? 1.0F : 0.0F);
    const auto swap = swapChannels_.load(std::memory_order_relaxed);
    const auto forceMono = mono_.load(std::memory_order_relaxed);
    const auto invertLeft = invertLeft_.load(std::memory_order_relaxed);
    const auto invertRight = invertRight_.load(std::memory_order_relaxed);
    double sumLR = 0.0;
    double sumL2 = 0.0;
    double sumR2 = 0.0;

    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        const auto dryLeft = finiteOrZero(buffer.channels[0][frame]);
        const auto dryRight = finiteOrZero(buffer.channels[1][frame]);
        auto left = swap ? dryRight : dryLeft;
        auto right = swap ? dryLeft : dryRight;
        if (invertLeft) { left = -left; }
        if (invertRight) { right = -right; }

        const auto mid = (left + right) * 0.5F * midGainSmoother_.next();
        auto side = (left - right) * 0.5F * sideGainSmoother_.next() * widthSmoother_.next();
        if (forceMono) { side = 0.0F; }
        left = mid + side;
        right = mid - side;

        const auto lowCoefficient = onePoleCoefficient(
            monoBassFrequencySmoother_.next(), spec_.sampleRate);
        lowLeft_ = finiteOrZero(lowLeft_ + lowCoefficient * (left - lowLeft_));
        lowRight_ = finiteOrZero(lowRight_ + lowCoefficient * (right - lowRight_));
        const auto monoLow = (lowLeft_ + lowRight_) * 0.5F;
        const auto monoAmount = monoBassSmoother_.next();
        left += (monoLow - lowLeft_) * monoAmount;
        right += (monoLow - lowRight_) * monoAmount;

        const auto balance = balanceSmoother_.next();
        left *= balance > 0.0F ? 1.0F - balance : 1.0F;
        right *= balance < 0.0F ? 1.0F + balance : 1.0F;
        const auto wet = wetSmoother_.next();
        left = dryLeft + (left - dryLeft) * wet;
        right = dryRight + (right - dryRight) * wet;
        buffer.channels[0][frame] = finiteOrZero(left);
        buffer.channels[1][frame] = finiteOrZero(right);

        sumLR += static_cast<double>(left) * static_cast<double>(right);
        sumL2 += static_cast<double>(left) * static_cast<double>(left);
        sumR2 += static_cast<double>(right) * static_cast<double>(right);
    }
    const auto denominator = std::sqrt(sumL2 * sumR2);
    const auto value = denominator > 1.0e-12 ? sumLR / denominator : 0.0;
    correlation_.store(static_cast<float>(std::clamp(value, -1.0, 1.0)), std::memory_order_relaxed);
}

TruePeakLimiter::TruePeakLimiter() noexcept = default;

void TruePeakLimiter::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_relaxed);
}

void TruePeakLimiter::setCeilingDB(float ceilingDB) noexcept {
    ceilingDB_.store(std::clamp(finiteOrZero(ceilingDB), -24.0F, 0.0F), std::memory_order_relaxed);
}

void TruePeakLimiter::setLookaheadMilliseconds(float milliseconds) noexcept {
    lookaheadMilliseconds_.store(
        std::clamp(finiteOrZero(milliseconds), 0.1F, kMaximumLookaheadMilliseconds),
        std::memory_order_relaxed);
}

void TruePeakLimiter::setReleaseMilliseconds(float milliseconds) noexcept {
    releaseMilliseconds_.store(
        std::clamp(finiteOrZero(milliseconds), 10.0F, 1'000.0F),
        std::memory_order_relaxed);
}

bool TruePeakLimiter::enabled() const noexcept {
    return enabled_.load(std::memory_order_relaxed);
}

float TruePeakLimiter::ceilingDB() const noexcept {
    return ceilingDB_.load(std::memory_order_relaxed);
}

float TruePeakLimiter::lookaheadMilliseconds() const noexcept {
    return lookaheadMilliseconds_.load(std::memory_order_relaxed);
}

float TruePeakLimiter::releaseMilliseconds() const noexcept {
    return releaseMilliseconds_.load(std::memory_order_relaxed);
}

float TruePeakLimiter::gainReductionDB() const noexcept {
    return gainReductionDB_.load(std::memory_order_relaxed);
}

bool TruePeakLimiter::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    delayStride_ = static_cast<std::size_t>(
        std::ceil(spec.sampleRate * static_cast<double>(kMaximumLookaheadMilliseconds) / 1'000.0))
        + kDetectorLatencyFrames + 1;
    delay_.assign(spec.maximumChannelCount * delayStride_, 0.0F);
    detectorHistory_.assign(spec.maximumChannelCount * kInterpolationTaps, 0.0F);
    lookaheadFrames_ = std::max<std::size_t>(1, static_cast<std::size_t>(
        std::llround(spec.sampleRate * static_cast<double>(lookaheadMilliseconds()) / 1'000.0)));
    delayFrames_ = lookaheadFrames_ + kDetectorLatencyFrames;
    delayFrames_ = std::min(delayFrames_, delayStride_ - 1);

    constexpr auto center = static_cast<double>(kInterpolationTaps - 1) * 0.5;
    for (std::size_t phase = 0; phase < kOversamplingFactor; ++phase) {
        const auto fraction = static_cast<double>(phase) / static_cast<double>(kOversamplingFactor);
        double sum = 0.0;
        for (std::size_t tap = 0; tap < kInterpolationTaps; ++tap) {
            const auto distance = static_cast<double>(tap) - center + fraction;
            const auto sinc = std::abs(distance) < 1.0e-12
                ? 1.0 : std::sin(kPi * distance) / (kPi * distance);
            const auto window = 0.5 - 0.5 * std::cos(
                2.0 * kPi * static_cast<double>(tap) / static_cast<double>(kInterpolationTaps - 1));
            const auto coefficient = sinc * window;
            detectorCoefficients_[phase][tap] = static_cast<float>(coefficient);
            sum += coefficient;
        }
        if (std::abs(sum) > 1.0e-12) {
            for (auto& coefficient : detectorCoefficients_[phase]) {
                coefficient = static_cast<float>(static_cast<double>(coefficient) / sum);
            }
        }
    }
    prepared_ = true;
    reset();
    return true;
}

void TruePeakLimiter::reset() noexcept {
    std::fill(delay_.begin(), delay_.end(), 0.0F);
    std::fill(detectorHistory_.begin(), detectorHistory_.end(), 0.0F);
    writeIndex_ = 0;
    detectorWriteIndex_ = 0;
    holdFramesRemaining_ = 0;
    envelopeGain_ = 1.0F;
    gainReductionDB_.store(0.0F, std::memory_order_relaxed);
}

void TruePeakLimiter::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_)) {
        return;
    }

    const auto ceiling = decibelsToLinear(ceilingDB());
    const auto limiterEnabled = enabled();
    const auto outputCeiling = limiterEnabled ? ceiling : std::numeric_limits<float>::max();
    const auto releaseSeconds = static_cast<double>(releaseMilliseconds()) / 1'000.0;
    const auto releaseCoefficient = static_cast<float>(std::exp(-1.0 / (releaseSeconds * spec_.sampleRate)));
    float minimumGain = 1.0F;

    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        float linkedPeak = 0.0F;
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            detectorHistory_[channel * kInterpolationTaps + detectorWriteIndex_]
                = finiteOrZero(buffer.channels[channel][frame]);
            for (std::size_t phase = 0; phase < kOversamplingFactor; ++phase) {
                double interpolated = 0.0;
                for (std::size_t tap = 0; tap < kInterpolationTaps; ++tap) {
                    const auto historyIndex = (detectorWriteIndex_ + kInterpolationTaps - tap)
                        % kInterpolationTaps;
                    interpolated += static_cast<double>(
                        detectorHistory_[channel * kInterpolationTaps + historyIndex])
                        * static_cast<double>(detectorCoefficients_[phase][tap]);
                }
                linkedPeak = std::max(linkedPeak, std::abs(static_cast<float>(interpolated)));
            }
        }

        const auto desiredGain = limiterEnabled && linkedPeak > ceiling && linkedPeak > 0.0F
            ? ceiling / linkedPeak
            : 1.0F;
        if (desiredGain < envelopeGain_) {
            envelopeGain_ = desiredGain;
            holdFramesRemaining_ = lookaheadFrames_;
        } else if (holdFramesRemaining_ != 0) {
            --holdFramesRemaining_;
        } else {
            envelopeGain_ = releaseCoefficient * envelopeGain_
                + (1.0F - releaseCoefficient) * desiredGain;
        }
        envelopeGain_ = std::clamp(finiteOrZero(envelopeGain_), 0.0F, 1.0F);
        minimumGain = std::min(minimumGain, envelopeGain_);

        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            const auto offset = channel * delayStride_ + writeIndex_;
            const auto delayed = delay_[offset];
            delay_[offset] = finiteOrZero(buffer.channels[channel][frame]);
            const auto limited = finiteOrZero(delayed * envelopeGain_);
            buffer.channels[channel][frame] = std::clamp(limited, -outputCeiling, outputCeiling);
        }

        ++writeIndex_;
        if (writeIndex_ >= delayFrames_) {
            writeIndex_ = 0;
        }
        ++detectorWriteIndex_;
        if (detectorWriteIndex_ >= kInterpolationTaps) {
            detectorWriteIndex_ = 0;
        }
    }

    gainReductionDB_.store(-linearToDecibels(std::max(minimumGain, 0.000001F)), std::memory_order_relaxed);
}

std::size_t TruePeakLimiter::latencyFrames() const noexcept {
    return prepared_ ? delayFrames_ : 0;
}

DSPChain::DSPChain() noexcept {
    filter_.setEnabled(false);
    equalizer_.setEnabled(false);
    bassEnhancer_.setEnabled(false);
    exciter_.setEnabled(false);
    crystalizer_.setEnabled(false);
    stereoTools_.setEnabled(false);
}

void DSPChain::setBypassed(bool bypassed) noexcept {
    bypassed_.store(bypassed, std::memory_order_relaxed);
}

bool DSPChain::bypassed() const noexcept {
    return bypassed_.load(std::memory_order_relaxed);
}

void DSPChain::setOutputGainDB(float gainDB) noexcept {
    outputGain_.setGainDB(std::min(finiteOrZero(gainDB), 0.0F));
}

bool DSPChain::prepare(const ProcessSpec& spec) {
    if (!spec.isValid()) {
        return false;
    }
    spec_ = spec;
    prepared_ = inputGain_.prepare(spec)
        && filter_.prepare(spec)
        && equalizer_.prepare(spec)
        && bassEnhancer_.prepare(spec)
        && exciter_.prepare(spec)
        && crystalizer_.prepare(spec)
        && stereoTools_.prepare(spec)
        && limiter_.prepare(spec)
        && outputGain_.prepare(spec);
    if (prepared_) {
        dryDelayFrames_ = limiter_.latencyFrames();
        dryDelay_.assign(spec.maximumChannelCount * std::max<std::size_t>(dryDelayFrames_, 1), 0.0F);
        dryBlock_.assign(spec.maximumChannelCount * spec.maximumFrameCount, 0.0F);
        processedMix_.prepare(spec.sampleRate, 5.0);
        reset();
    }
    return prepared_;
}

void DSPChain::reset() noexcept {
    inputGain_.reset();
    filter_.reset();
    equalizer_.reset();
    bassEnhancer_.reset();
    exciter_.reset();
    crystalizer_.reset();
    stereoTools_.reset();
    limiter_.reset();
    outputGain_.reset();
    meters_.reset();
    std::fill(dryDelay_.begin(), dryDelay_.end(), 0.0F);
    std::fill(dryBlock_.begin(), dryBlock_.end(), 0.0F);
    dryWriteIndex_ = 0;
    processedMix_.reset(bypassed() ? 0.0F : 1.0F);
}

void DSPChain::process(AudioBufferView buffer) noexcept {
    if (!prepared_ || !buffer.isValidFor(spec_)) {
        return;
    }
    const auto inputPeak = measurePeak(buffer);
    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            const auto dry = finiteOrZero(buffer.channels[channel][frame]);
            auto delayedDry = dry;
            if (dryDelayFrames_ != 0) {
                const auto offset = channel * dryDelayFrames_ + dryWriteIndex_;
                delayedDry = dryDelay_[offset];
                dryDelay_[offset] = dry;
            }
            dryBlock_[channel * spec_.maximumFrameCount + frame] = delayedDry;
        }
        if (dryDelayFrames_ != 0) {
            ++dryWriteIndex_;
            if (dryWriteIndex_ >= dryDelayFrames_) {
                dryWriteIndex_ = 0;
            }
        }
    }
    inputGain_.process(buffer);
    filter_.process(buffer);
    equalizer_.process(buffer);
    bassEnhancer_.process(buffer);
    exciter_.process(buffer);
    crystalizer_.process(buffer);
    stereoTools_.process(buffer);
    limiter_.process(buffer);
    outputGain_.process(buffer);
    processedMix_.setTarget(bypassed() ? 0.0F : 1.0F);
    for (std::size_t frame = 0; frame < buffer.frameCount; ++frame) {
        const auto processedMix = processedMix_.next();
        for (std::size_t channel = 0; channel < buffer.channelCount; ++channel) {
            const auto dry = dryBlock_[channel * spec_.maximumFrameCount + frame];
            const auto processed = finiteOrZero(buffer.channels[channel][frame]);
            buffer.channels[channel][frame] = dry + (processed - dry) * processedMix;
        }
    }
    const auto outputPeak = measurePeak(buffer);
    meters_.publish(inputPeak, outputPeak, limiter_.gainReductionDB(), outputPeak > 1.0F);
}

std::size_t DSPChain::latencyFrames() const noexcept {
    return limiter_.latencyFrames();
}

} // namespace phoenaux::dsp

struct PXEngine {
    phoenaux::dsp::DSPChain chain{};
    phoenaux::dsp::ProcessSpec spec{};
    bool prepared = false;
};

struct PXPCMSource {
    double sampleRate = 0.0;
    std::size_t channelCount = 0;
    std::vector<std::vector<float>> channels{};
    std::atomic<std::size_t> position{0};
    std::atomic<bool> sealed{false};
    std::atomic<bool> finished{false};
};

namespace {

[[nodiscard]] phoenaux::dsp::FilterType toCppFilterType(PXFilterType type) noexcept {
    using phoenaux::dsp::FilterType;
    switch (type) {
    case PXFilterTypeLowPass:
        return FilterType::lowPass;
    case PXFilterTypeHighPass:
        return FilterType::highPass;
    case PXFilterTypeLowShelf:
        return FilterType::lowShelf;
    case PXFilterTypeHighShelf:
        return FilterType::highShelf;
    case PXFilterTypeBell:
        return FilterType::bell;
    case PXFilterTypeBandPass:
        return FilterType::bandPass;
    case PXFilterTypeNotch:
        return FilterType::notch;
    }
    return FilterType::bell;
}

} // namespace

extern "C" {

PXEngine* PXEngineCreate(void) {
    return new (std::nothrow) PXEngine{};
}

void PXEngineDestroy(PXEngine* engine) {
    delete engine;
}

bool PXEnginePrepare(PXEngine* engine, PXProcessSpec spec) {
    if (engine == nullptr) {
        return false;
    }
    engine->spec = {spec.sampleRate, spec.maximumFrameCount, spec.maximumChannelCount};
    engine->prepared = engine->chain.prepare(engine->spec);
    return engine->prepared;
}

void PXEngineReset(PXEngine* engine) {
    if (engine != nullptr && engine->prepared) {
        engine->chain.reset();
    }
}

bool PXEngineProcess(
    PXEngine* engine,
    float* const* channels,
    size_t channelCount,
    size_t frameCount) {
    if (engine == nullptr || !engine->prepared) {
        return false;
    }
    phoenaux::dsp::AudioBufferView buffer{channels, channelCount, frameCount};
    if (!buffer.isValidFor(engine->spec)) {
        return false;
    }
    engine->chain.process(buffer);
    return true;
}

void PXEngineSetBypassed(PXEngine* engine, bool bypassed) {
    if (engine != nullptr) {
        engine->chain.setBypassed(bypassed);
    }
}

void PXEngineSetInputGainDB(PXEngine* engine, float gainDB) {
    if (engine != nullptr) {
        engine->chain.inputGain().setGainDB(gainDB);
    }
}

void PXEngineSetOutputGainDB(PXEngine* engine, float gainDB) {
    if (engine != nullptr) {
        engine->chain.setOutputGainDB(gainDB);
    }
}

void PXEngineSetFilterEnabled(PXEngine* engine, bool enabled) {
    if (engine != nullptr) {
        engine->chain.filter().setEnabled(enabled);
    }
}

void PXEngineSetFilter(
    PXEngine* engine,
    PXFilterType type,
    float frequency,
    float q,
    float gainDB) {
    if (engine == nullptr) {
        return;
    }
    auto& filter = engine->chain.filter();
    filter.setType(toCppFilterType(type));
    filter.setFrequency(frequency);
    filter.setQ(q);
    filter.setGainDB(gainDB);
}

void PXEngineSetEqualizerEnabled(PXEngine* engine, bool enabled) {
    if (engine != nullptr) {
        engine->chain.equalizer().setEnabled(enabled);
    }
}

bool PXEngineSetEqualizerBandCount(PXEngine* engine, size_t count) {
    if (engine == nullptr || count > phoenaux::dsp::kMaxEQBands) {
        return false;
    }
    engine->chain.equalizer().setBandCount(count);
    return true;
}

bool PXEngineSetEqualizerBand(
    PXEngine* engine,
    size_t index,
    bool enabled,
    PXFilterType type,
    float frequency,
    float q,
    float gainDB) {
    if (engine == nullptr || index >= phoenaux::dsp::kMaxEQBands) {
        return false;
    }
    auto& filterBand = engine->chain.equalizer().band(index);
    filterBand.setEnabled(enabled);
    filterBand.setType(toCppFilterType(type));
    filterBand.setFrequency(frequency);
    filterBand.setQ(q);
    filterBand.setGainDB(gainDB);
    return true;
}

void PXEngineSetBassEnhancerEnabled(PXEngine* engine, bool enabled) {
    if (engine != nullptr) {
        engine->chain.bassEnhancer().setEnabled(enabled);
    }
}

void PXEngineSetBassEnhancer(
    PXEngine* engine,
    PXBassEnhancerMode mode,
    float crossoverFrequency,
    float amount,
    float drive,
    float mix) {
    if (engine == nullptr) {
        return;
    }
    auto& bass = engine->chain.bassEnhancer();
    bass.setMode(mode == PXBassEnhancerModeExtension
        ? phoenaux::dsp::BassEnhancerMode::extension
        : phoenaux::dsp::BassEnhancerMode::psychoacoustic);
    bass.setCrossoverFrequency(crossoverFrequency);
    bass.setAmount(amount);
    bass.setDrive(drive);
    bass.setMix(mix);
}

void PXEngineSetExciterEnabled(PXEngine* engine, bool enabled) {
    if (engine != nullptr) {
        engine->chain.exciter().setEnabled(enabled);
    }
}

void PXEngineSetExciter(
    PXEngine* engine,
    float frequency,
    float drive,
    float amount,
    float mix) {
    if (engine == nullptr) {
        return;
    }
    auto& exciter = engine->chain.exciter();
    exciter.setFrequency(frequency);
    exciter.setDrive(drive);
    exciter.setAmount(amount);
    exciter.setMix(mix);
}

void PXEngineSetCrystalizerEnabled(PXEngine* engine, bool enabled) {
    if (engine != nullptr) {
        engine->chain.crystalizer().setEnabled(enabled);
    }
}

void PXEngineSetCrystalizer(
    PXEngine* engine,
    float frequency,
    float amount,
    float sensitivity,
    float mix) {
    if (engine == nullptr) {
        return;
    }
    auto& crystalizer = engine->chain.crystalizer();
    crystalizer.setFrequency(frequency);
    crystalizer.setAmount(amount);
    crystalizer.setSensitivity(sensitivity);
    crystalizer.setMix(mix);
}

void PXEngineSetStereoToolsEnabled(PXEngine* engine, bool enabled) {
    if (engine != nullptr) {
        engine->chain.stereoTools().setEnabled(enabled);
    }
}

void PXEngineSetStereoTools(
    PXEngine* engine,
    float width,
    float midGainDB,
    float sideGainDB,
    float balance,
    float monoBassFrequency,
    float monoBassAmount) {
    if (engine == nullptr) {
        return;
    }
    auto& stereo = engine->chain.stereoTools();
    stereo.setWidth(width);
    stereo.setMidGainDB(midGainDB);
    stereo.setSideGainDB(sideGainDB);
    stereo.setBalance(balance);
    stereo.setMonoBassFrequency(monoBassFrequency);
    stereo.setMonoBassAmount(monoBassAmount);
}

void PXEngineSetStereoSwitches(
    PXEngine* engine,
    bool swapChannels,
    bool mono,
    bool invertLeft,
    bool invertRight) {
    if (engine == nullptr) {
        return;
    }
    auto& stereo = engine->chain.stereoTools();
    stereo.setSwapChannels(swapChannels);
    stereo.setMono(mono);
    stereo.setInvertLeft(invertLeft);
    stereo.setInvertRight(invertRight);
}

float PXEngineStereoCorrelation(const PXEngine* engine) {
    return engine == nullptr ? 0.0F : engine->chain.stereoTools().correlation();
}

void PXEngineSetLimiterEnabled(PXEngine* engine, bool enabled) {
    if (engine != nullptr) {
        engine->chain.limiter().setEnabled(enabled);
    }
}

void PXEngineSetLimiter(
    PXEngine* engine,
    float ceilingDB,
    float lookaheadMilliseconds,
    float releaseMilliseconds) {
    if (engine == nullptr) {
        return;
    }
    auto& limiter = engine->chain.limiter();
    limiter.setCeilingDB(ceilingDB);
    limiter.setLookaheadMilliseconds(lookaheadMilliseconds);
    limiter.setReleaseMilliseconds(releaseMilliseconds);
}

size_t PXEngineLatencyFrames(const PXEngine* engine) {
    return engine == nullptr ? 0 : engine->chain.latencyFrames();
}

PXMeterSnapshot PXEngineMeters(const PXEngine* engine) {
    if (engine == nullptr) {
        return {};
    }
    const auto snapshot = engine->chain.meters().snapshot();
    return {snapshot.inputPeak, snapshot.outputPeak, snapshot.gainReductionDB, snapshot.clipped};
}

PXPCMSource* PXPCMSourceCreate(double sampleRate, size_t channelCount) {
    if (!std::isfinite(sampleRate) || sampleRate < 8'000.0 || sampleRate > 768'000.0
        || channelCount == 0 || channelCount > phoenaux::dsp::kMaxChannels) {
        return nullptr;
    }
    auto* source = new (std::nothrow) PXPCMSource{};
    if (source == nullptr) {
        return nullptr;
    }
    try {
        source->sampleRate = sampleRate;
        source->channelCount = channelCount;
        source->channels.resize(channelCount);
    } catch (...) {
        delete source;
        return nullptr;
    }
    return source;
}

void PXPCMSourceDestroy(PXPCMSource* source) {
    delete source;
}

bool PXPCMSourceAppend(
    PXPCMSource* source,
    float* const* channels,
    size_t channelCount,
    size_t frameCount) {
    if (source == nullptr || channels == nullptr || frameCount == 0
        || channelCount != source->channelCount
        || source->sealed.load(std::memory_order_acquire)) {
        return false;
    }
    for (std::size_t channel = 0; channel < channelCount; ++channel) {
        const auto& destination = source->channels[channel];
        if (channels[channel] == nullptr
            || frameCount > destination.max_size() - destination.size()) {
            return false;
        }
    }
    try {
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            auto& destination = source->channels[channel];
            destination.reserve(destination.size() + frameCount);
        }
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            auto& destination = source->channels[channel];
            for (std::size_t frame = 0; frame < frameCount; ++frame) {
                destination.push_back(phoenaux::dsp::finiteOrZero(channels[channel][frame]));
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool PXPCMSourceSeal(PXPCMSource* source) {
    if (source == nullptr || source->sealed.load(std::memory_order_acquire)
        || source->channels.empty() || source->channels.front().empty()) {
        return false;
    }
    const auto frameCount = source->channels.front().size();
    if (!std::all_of(source->channels.begin(), source->channels.end(),
            [frameCount](const auto& channel) { return channel.size() == frameCount; })) {
        return false;
    }
    source->position.store(0, std::memory_order_relaxed);
    source->finished.store(false, std::memory_order_relaxed);
    source->sealed.store(true, std::memory_order_release);
    return true;
}

void PXPCMSourceReset(PXPCMSource* source) {
    if (source == nullptr) {
        return;
    }
    source->position.store(0, std::memory_order_release);
    source->finished.store(false, std::memory_order_release);
}

bool PXPCMSourceRender(
    PXPCMSource* source,
    float* const* outputChannels,
    size_t outputChannelCount,
    size_t frameCount,
    bool loop) {
    if (source == nullptr || outputChannels == nullptr || frameCount == 0
        || outputChannelCount != source->channelCount
        || !source->sealed.load(std::memory_order_acquire)) {
        return false;
    }
    for (std::size_t channel = 0; channel < outputChannelCount; ++channel) {
        if (outputChannels[channel] == nullptr) {
            return false;
        }
    }

    const auto sourceFrames = source->channels.front().size();
    auto position = source->position.load(std::memory_order_relaxed);
    auto reachedEnd = false;
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        if (position >= sourceFrames) {
            if (loop) {
                position = 0;
            } else {
                reachedEnd = true;
                for (std::size_t channel = 0; channel < outputChannelCount; ++channel) {
                    outputChannels[channel][frame] = 0.0F;
                }
                continue;
            }
        }
        for (std::size_t channel = 0; channel < outputChannelCount; ++channel) {
            outputChannels[channel][frame] = source->channels[channel][position];
        }
        ++position;
    }
    source->position.store(position, std::memory_order_release);
    source->finished.store(reachedEnd, std::memory_order_release);
    return true;
}

double PXPCMSourceSampleRate(const PXPCMSource* source) {
    return source == nullptr ? 0.0 : source->sampleRate;
}

size_t PXPCMSourceChannelCount(const PXPCMSource* source) {
    return source == nullptr ? 0 : source->channelCount;
}

size_t PXPCMSourceFrameCount(const PXPCMSource* source) {
    return source == nullptr || source->channels.empty() ? 0 : source->channels.front().size();
}

size_t PXPCMSourcePosition(const PXPCMSource* source) {
    return source == nullptr ? 0 : source->position.load(std::memory_order_acquire);
}

bool PXPCMSourceFinished(const PXPCMSource* source) {
    return source == nullptr || source->finished.load(std::memory_order_acquire);
}

} // extern "C"
