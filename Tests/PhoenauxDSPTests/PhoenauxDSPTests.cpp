#include <PhoenauxDSP/PhoenauxDSP.hpp>
#include <PhoenauxDSP/PhoenauxDSP.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using namespace phoenaux::dsp;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool approximately(float lhs, float rhs, float tolerance = 0.0001F) {
    return std::abs(lhs - rhs) <= tolerance;
}

float maximumAbsolute(const std::vector<float>& samples) {
    float result = 0.0F;
    for (const auto sample : samples) {
        result = std::max(result, std::abs(sample));
    }
    return result;
}

double differenceEnergy(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    double result = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto difference = static_cast<double>(lhs[index]) - static_cast<double>(rhs[index]);
        result += difference * difference;
    }
    return result;
}

struct TestBuffer {
    TestBuffer(std::size_t channelCount, std::size_t frameCount)
        : storage(channelCount, std::vector<float>(frameCount, 0.0F)), pointers(channelCount) {
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            pointers[channel] = storage[channel].data();
        }
    }

    AudioBufferView view() {
        return {pointers.data(), storage.size(), storage.front().size()};
    }

    std::vector<std::vector<float>> storage;
    std::vector<float*> pointers;
};

constexpr ProcessSpec testSpec{48'000.0, 2'048, 2};

void testConversionsAndSmoothing() {
    check(approximately(decibelsToLinear(0.0F), 1.0F), "0 dB converts to unity");
    check(approximately(decibelsToLinear(-6.0206F), 0.5F, 0.0002F), "-6.0206 dB converts to one half");
    check(approximately(linearToDecibels(1.0F), 0.0F), "unity converts to 0 dB");

    LinearSmoother smoother;
    smoother.prepare(48'000.0, 1.0);
    smoother.reset(0.0F);
    smoother.setTarget(1.0F);
    for (int index = 0; index < 48; ++index) {
        static_cast<void>(smoother.next());
    }
    check(approximately(smoother.current(), 1.0F), "linear smoother reaches its target");
    check(!smoother.isSmoothing(), "linear smoother finishes after the prepared ramp");
}

void testGain() {
    GainNode gain;
    gain.setGainDB(6.0206F);
    check(gain.prepare(testSpec), "gain prepares with a valid process spec");

    TestBuffer buffer(2, 32);
    for (auto& channel : buffer.storage) {
        std::fill(channel.begin(), channel.end(), 0.5F);
    }
    buffer.storage[0][3] = std::numeric_limits<float>::infinity();
    gain.process(buffer.view());
    check(approximately(buffer.storage[1][0], 1.0F, 0.0003F), "gain applies its prepared value");
    check(buffer.storage[0][3] == 0.0F, "gain sanitizes non-finite samples");
}

void testAllFilterShapesRemainFinite() {
    constexpr std::array types{
        FilterType::lowPass,
        FilterType::highPass,
        FilterType::lowShelf,
        FilterType::highShelf,
        FilterType::bell,
        FilterType::bandPass,
        FilterType::notch,
    };

    for (const auto type : types) {
        FilterNode filter;
        filter.setType(type);
        filter.setFrequency(1'000.0F);
        filter.setQ(0.70710678F);
        filter.setGainDB(6.0F);
        check(filter.prepare(testSpec), "filter prepares");
        TestBuffer impulse(2, 2'048);
        impulse.storage[0][0] = 1.0F;
        impulse.storage[1][0] = 1.0F;
        filter.process(impulse.view());
        for (const auto& channel : impulse.storage) {
            for (const auto sample : channel) {
                check(std::isfinite(sample), "filter impulse response remains finite");
                check(std::abs(sample) < 16.0F, "filter impulse response remains bounded");
            }
        }
    }
}

void testEqualizerBandAndBypass() {
    ParametricEQ equalizer;
    equalizer.setBandCount(1);
    equalizer.band(0).setEnabled(true);
    equalizer.band(0).setType(FilterType::bell);
    equalizer.band(0).setFrequency(1'000.0F);
    equalizer.band(0).setQ(1.0F);
    equalizer.band(0).setGainDB(6.0F);
    check(equalizer.prepare(testSpec), "equalizer prepares all fixed-capacity bands");

    TestBuffer signal(2, 2'048);
    for (std::size_t frame = 0; frame < signal.storage.front().size(); ++frame) {
        const auto sample = std::sin(2.0 * 3.141592653589793 * 1'000.0
            * static_cast<double>(frame) / testSpec.sampleRate);
        signal.storage[0][frame] = static_cast<float>(sample * 0.1);
        signal.storage[1][frame] = static_cast<float>(sample * 0.1);
    }
    equalizer.process(signal.view());
    const auto processedPeak = *std::max_element(
        signal.storage[0].begin(), signal.storage[0].end(),
        [](float lhs, float rhs) { return std::abs(lhs) < std::abs(rhs); });
    check(std::abs(processedPeak) > 0.15F, "bell EQ boosts a tone at its center frequency");

    equalizer.setEnabled(false);
    equalizer.reset();
    TestBuffer bypassed(2, 64);
    std::fill(bypassed.storage[0].begin(), bypassed.storage[0].end(), 0.25F);
    std::fill(bypassed.storage[1].begin(), bypassed.storage[1].end(), -0.25F);
    equalizer.process(bypassed.view());
    check(approximately(bypassed.storage[0][40], 0.25F), "disabled EQ is transparent");
    check(approximately(bypassed.storage[1][40], -0.25F), "disabled EQ preserves channel polarity");
}

void testBassEnhancerModesAndBypass() {
    BassEnhancer bass;
    bass.setEnabled(true);
    bass.setMode(BassEnhancerMode::psychoacoustic);
    bass.setCrossoverFrequency(120.0F);
    bass.setAmount(0.8F);
    bass.setDrive(4.0F);
    bass.setMix(0.8F);
    check(bass.prepare(testSpec), "bass enhancer prepares");

    TestBuffer signal(2, 2'048);
    for (std::size_t frame = 0; frame < signal.storage.front().size(); ++frame) {
        const auto sample = static_cast<float>(0.35 * std::sin(
            2.0 * 3.141592653589793 * 60.0 * static_cast<double>(frame) / testSpec.sampleRate));
        signal.storage[0][frame] = sample;
        signal.storage[1][frame] = sample;
    }
    const auto original = signal.storage[0];
    bass.process(signal.view());
    check(differenceEnergy(signal.storage[0], original) > 0.001,
        "psychoacoustic bass produces a parallel harmonic contribution");
    check(maximumAbsolute(signal.storage[0]) < 2.0F, "bass protection keeps output bounded");

    bass.setMode(BassEnhancerMode::extension);
    bass.reset();
    TestBuffer extension(2, 2'048);
    extension.storage[0] = original;
    extension.storage[1] = original;
    bass.process(extension.view());
    check(differenceEnergy(extension.storage[0], original) > 0.001,
        "extension mode reinforces reproducible low fundamentals");

    bass.setEnabled(false);
    bass.reset();
    TestBuffer bypassed(2, 128);
    std::fill(bypassed.storage[0].begin(), bypassed.storage[0].end(), 0.2F);
    std::fill(bypassed.storage[1].begin(), bypassed.storage[1].end(), -0.2F);
    bass.process(bypassed.view());
    check(approximately(bypassed.storage[0][80], 0.2F), "disabled bass enhancer is transparent");
    check(approximately(bypassed.storage[1][80], -0.2F), "bass bypass preserves polarity");
}

void testExciterAddsProtectedHighBandHarmonics() {
    Exciter exciter;
    exciter.setEnabled(true);
    exciter.setFrequency(3'000.0F);
    exciter.setDrive(5.0F);
    exciter.setAmount(0.8F);
    exciter.setMix(0.7F);
    check(exciter.prepare(testSpec), "exciter prepares");

    TestBuffer signal(2, 2'048);
    for (std::size_t frame = 0; frame < signal.storage.front().size(); ++frame) {
        const auto sample = static_cast<float>(0.25 * std::sin(
            2.0 * 3.141592653589793 * 6'000.0 * static_cast<double>(frame) / testSpec.sampleRate));
        signal.storage[0][frame] = sample;
        signal.storage[1][frame] = sample;
    }
    const auto original = signal.storage[0];
    exciter.process(signal.view());
    check(differenceEnergy(signal.storage[0], original) > 0.001,
        "exciter changes high-band harmonic content");
    check(maximumAbsolute(signal.storage[0]) < 1.0F, "exciter parallel signal remains protected");
    check(std::all_of(signal.storage[0].begin(), signal.storage[0].end(),
        [](float sample) { return std::isfinite(sample); }), "exciter output remains finite");
}

void testCrystalizerRespondsToTransientContrast() {
    Crystalizer crystalizer;
    crystalizer.setEnabled(true);
    crystalizer.setFrequency(2'000.0F);
    crystalizer.setAmount(1.0F);
    crystalizer.setSensitivity(1.0F);
    crystalizer.setMix(0.8F);
    check(crystalizer.prepare(testSpec), "crystalizer prepares");

    TestBuffer transient(2, 512);
    transient.storage[0][0] = 0.5F;
    transient.storage[1][0] = 0.5F;
    crystalizer.process(transient.view());
    check(transient.storage[0][0] > 0.5F,
        "crystalizer emphasizes extracted transient detail rather than applying static EQ");
    check(maximumAbsolute(transient.storage[0]) < 2.0F, "crystalizer transient remains bounded");

    crystalizer.setEnabled(false);
    crystalizer.reset();
    TestBuffer bypassed(2, 64);
    bypassed.storage[0][0] = 0.5F;
    bypassed.storage[1][0] = -0.5F;
    crystalizer.process(bypassed.view());
    check(approximately(bypassed.storage[0][0], 0.5F), "disabled crystalizer is transparent");
    check(approximately(bypassed.storage[1][0], -0.5F), "crystalizer bypass preserves channels");
}

void testStereoToolsWidthMonoBassAndCorrelation() {
    StereoTools stereo;
    stereo.setEnabled(true);
    stereo.setWidth(0.0F);
    stereo.setMonoBassAmount(0.0F);
    check(stereo.prepare(testSpec), "stereo tools prepare");

    TestBuffer antiPhase(2, 256);
    std::fill(antiPhase.storage[0].begin(), antiPhase.storage[0].end(), 0.25F);
    std::fill(antiPhase.storage[1].begin(), antiPhase.storage[1].end(), -0.25F);
    stereo.process(antiPhase.view());
    check(maximumAbsolute(antiPhase.storage[0]) < 0.0001F,
        "zero width removes a purely side signal");
    check(maximumAbsolute(antiPhase.storage[1]) < 0.0001F,
        "zero width produces matched mono channels");

    stereo.setWidth(1.0F);
    stereo.setMonoBassAmount(1.0F);
    stereo.reset();
    TestBuffer correlated(2, 512);
    for (std::size_t frame = 0; frame < correlated.storage.front().size(); ++frame) {
        const auto sample = static_cast<float>(0.2 * std::sin(
            2.0 * 3.141592653589793 * 1'000.0 * static_cast<double>(frame) / testSpec.sampleRate));
        correlated.storage[0][frame] = sample;
        correlated.storage[1][frame] = sample;
    }
    stereo.process(correlated.view());
    check(stereo.correlation() > 0.99F, "stereo tools publish positive correlation for identical channels");

    stereo.setMono(true);
    stereo.reset();
    TestBuffer split(2, 64);
    std::fill(split.storage[0].begin(), split.storage[0].end(), 0.4F);
    std::fill(split.storage[1].begin(), split.storage[1].end(), 0.0F);
    stereo.process(split.view());
    check(approximately(split.storage[0][40], split.storage[1][40]), "mono switch produces equal channels");
}

void testLimiterCeilingLatencyAndLinking() {
    TruePeakLimiter limiter;
    limiter.setCeilingDB(0.0F);
    limiter.setLookaheadMilliseconds(1.0F);
    limiter.setReleaseMilliseconds(100.0F);
    check(limiter.prepare(testSpec), "limiter prepares its delay line");
    check(limiter.latencyFrames() == 56,
        "limiter reports lookahead plus true-peak detector latency");

    TestBuffer signal(2, 256);
    signal.storage[0][0] = 2.0F;
    signal.storage[1][0] = 0.5F;
    limiter.process(signal.view());
    for (const auto& channel : signal.storage) {
        for (const auto sample : channel) {
            check(std::isfinite(sample), "limiter output remains finite");
            check(std::abs(sample) <= 1.00001F, "limiter respects its sample ceiling");
        }
    }
    check(approximately(signal.storage[0][56], 1.0F, 0.0001F), "limiter delays and contains the hot channel");
    check(signal.storage[1][56] < 0.5F, "linked limiter applies the same gain to both channels");
    check(limiter.gainReductionDB() > 5.9F, "limiter publishes gain reduction");

    limiter.reset();
    TestBuffer silence(2, 64);
    limiter.process(silence.view());
    check(std::all_of(silence.storage[0].begin(), silence.storage[0].end(),
        [](float sample) { return sample == 0.0F; }), "limiter reset clears delayed audio");

    limiter.setEnabled(false);
    limiter.setCeilingDB(-24.0F);
    limiter.reset();
    TestBuffer bypassed(2, 128);
    bypassed.storage[0][0] = 0.8F;
    bypassed.storage[1][0] = -0.8F;
    limiter.process(bypassed.view());
    check(approximately(bypassed.storage[0][56], 0.8F),
        "disabled limiter preserves samples while retaining declared latency");
    check(approximately(bypassed.storage[1][56], -0.8F),
        "disabled limiter does not apply its ceiling clamp");
}

void testLimiterDetectsIntersamplePeaks() {
    constexpr auto amplitude = 1.2;
    constexpr auto phase = 0.7853981633974483;
    constexpr std::array sampleRates{44'100.0, 48'000.0, 96'000.0};
    for (const auto sampleRate : sampleRates) {
        const ProcessSpec vectorSpec{sampleRate, testSpec.maximumFrameCount, 2};
        TruePeakLimiter limiter;
        limiter.setCeilingDB(-1.0F);
        limiter.setLookaheadMilliseconds(1.0F);
        limiter.setReleaseMilliseconds(100.0F);
        check(limiter.prepare(vectorSpec), "true-peak limiter prepares across sample rates");

        const auto frequency = sampleRate / 4.0;
        TestBuffer signal(2, vectorSpec.maximumFrameCount);
        for (std::size_t frame = 0; frame < signal.storage.front().size(); ++frame) {
            const auto sample = static_cast<float>(amplitude * std::sin(
                2.0 * 3.141592653589793 * frequency * static_cast<double>(frame)
                    / vectorSpec.sampleRate + phase));
            signal.storage[0][frame] = sample;
            signal.storage[1][frame] = sample;
        }
        check(maximumAbsolute(signal.storage[0]) < decibelsToLinear(-1.0F),
            "conformance tone hides its analog peak between samples");

        limiter.process(signal.view());
        const auto steadyBegin = signal.storage[0].begin() + 512;
        const auto steadySamplePeak = *std::max_element(steadyBegin, signal.storage[0].end(),
            [](float lhs, float rhs) { return std::abs(lhs) < std::abs(rhs); });
        const auto estimatedTruePeak = std::abs(steadySamplePeak)
            / static_cast<float>(std::sin(phase));
        check(estimatedTruePeak <= decibelsToLinear(-1.0F) + 0.002F,
            "true-peak detector contains a quarter-rate intersample over");
        check(limiter.gainReductionDB() > 2.0F,
            "intersample-only over drives linked gain reduction");
    }
}

void testChainAndMeters() {
    DSPChain chain;
    chain.inputGain().setGainDB(0.0F);
    chain.limiter().setCeilingDB(-1.0F);
    chain.limiter().setLookaheadMilliseconds(1.0F);
    check(chain.prepare(testSpec), "DSP chain prepares all nodes");

    TestBuffer signal(2, 256);
    signal.storage[0][0] = 1.5F;
    signal.storage[1][0] = -1.5F;
    chain.process(signal.view());
    const auto meters = chain.meters().snapshot();
    check(approximately(meters.inputPeak, 1.5F), "chain reports input peak");
    check(meters.outputPeak <= decibelsToLinear(-1.0F) + 0.0001F, "chain output remains below limiter ceiling");
    check(meters.gainReductionDB > 0.0F, "chain reports limiter activity");
    check(!meters.clipped, "limited chain does not report digital clipping");
}

void testCompleteSevenStageGraph() {
    DSPChain chain;
    chain.filter().setEnabled(true);
    chain.filter().setType(FilterType::highPass);
    chain.filter().setFrequency(25.0F);
    chain.equalizer().setEnabled(true);
    chain.equalizer().setBandCount(1);
    chain.equalizer().band(0).setEnabled(true);
    chain.equalizer().band(0).setType(FilterType::bell);
    chain.equalizer().band(0).setFrequency(2'500.0F);
    chain.equalizer().band(0).setGainDB(3.0F);
    chain.bassEnhancer().setEnabled(true);
    chain.bassEnhancer().setAmount(0.5F);
    chain.exciter().setEnabled(true);
    chain.exciter().setAmount(0.3F);
    chain.crystalizer().setEnabled(true);
    chain.crystalizer().setAmount(0.4F);
    chain.stereoTools().setEnabled(true);
    chain.stereoTools().setWidth(1.15F);
    chain.limiter().setCeilingDB(-1.0F);
    chain.limiter().setLookaheadMilliseconds(1.0F);
    check(chain.prepare(testSpec), "complete seven-stage graph prepares");

    TestBuffer signal(2, 2'048);
    for (std::size_t frame = 0; frame < signal.storage.front().size(); ++frame) {
        const auto low = 0.6 * std::sin(2.0 * 3.141592653589793 * 70.0
            * static_cast<double>(frame) / testSpec.sampleRate);
        const auto high = 0.4 * std::sin(2.0 * 3.141592653589793 * 5'000.0
            * static_cast<double>(frame) / testSpec.sampleRate);
        signal.storage[0][frame] = static_cast<float>(low + high);
        signal.storage[1][frame] = static_cast<float>(low - high * 0.5);
    }
    chain.process(signal.view());
    for (const auto& channel : signal.storage) {
        check(maximumAbsolute(channel) <= decibelsToLinear(-1.0F) + 0.0001F,
            "complete graph remains below the final limiter ceiling");
        check(std::all_of(channel.begin(), channel.end(),
            [](float sample) { return std::isfinite(sample); }),
            "complete graph remains finite");
    }
}

void testLatencyMatchedGlobalBypass() {
    DSPChain chain;
    chain.setBypassed(true);
    chain.inputGain().setGainDB(12.0F);
    chain.filter().setEnabled(true);
    chain.filter().setType(FilterType::lowPass);
    chain.filter().setFrequency(500.0F);
    chain.limiter().setLookaheadMilliseconds(1.0F);
    check(chain.prepare(testSpec), "globally bypassed chain prepares");

    TestBuffer impulse(2, 128);
    impulse.storage[0][0] = 0.5F;
    impulse.storage[1][0] = -0.5F;
    chain.process(impulse.view());
    check(approximately(impulse.storage[0][56], 0.5F),
        "global bypass returns latency-matched dry audio");
    check(approximately(impulse.storage[1][56], -0.5F),
        "global bypass preserves dry channel polarity");

    chain.setBypassed(false);
    TestBuffer transition(2, 512);
    std::fill(transition.storage[0].begin(), transition.storage[0].end(), 0.05F);
    std::fill(transition.storage[1].begin(), transition.storage[1].end(), 0.05F);
    chain.process(transition.view());
    float largestStep = 0.0F;
    for (std::size_t frame = 1; frame < transition.storage[0].size(); ++frame) {
        largestStep = std::max(largestStep,
            std::abs(transition.storage[0][frame] - transition.storage[0][frame - 1]));
    }
    check(largestStep < 0.1F, "global bypass transition is crossfaded without a large discontinuity");
}

void testCInterface() {
    auto* engine = PXEngineCreate();
    check(engine != nullptr, "C interface creates an engine");
    if (engine == nullptr) {
        return;
    }

    PXEngineSetInputGainDB(engine, 0.0F);
    PXEngineSetOutputGainDB(engine, 6.0F);
    PXEngineSetFilterEnabled(engine, true);
    PXEngineSetFilter(engine, PXFilterTypeHighPass, 30.0F, 0.70710678F, 0.0F);
    PXEngineSetEqualizerEnabled(engine, true);
    check(PXEngineSetEqualizerBandCount(engine, 1), "C interface accepts a valid EQ band count");
    check(PXEngineSetEqualizerBand(
        engine, 0, true, PXFilterTypeBell, 1'000.0F, 1.0F, 3.0F),
        "C interface configures a valid EQ band");
    PXEngineSetBassEnhancerEnabled(engine, true);
    PXEngineSetBassEnhancer(engine, PXBassEnhancerModePsychoacoustic, 120.0F, 0.4F, 2.5F, 0.4F);
    PXEngineSetExciterEnabled(engine, true);
    PXEngineSetExciter(engine, 4'000.0F, 2.5F, 0.2F, 0.2F);
    PXEngineSetCrystalizerEnabled(engine, true);
    PXEngineSetCrystalizer(engine, 2'500.0F, 0.3F, 0.6F, 0.3F);
    PXEngineSetStereoToolsEnabled(engine, true);
    PXEngineSetStereoTools(engine, 1.1F, 0.0F, 0.0F, 0.0F, 120.0F, 1.0F);
    PXEngineSetStereoSwitches(engine, false, false, false, false);
    PXEngineSetLimiter(engine, -1.0F, 1.0F, 100.0F);

    const PXProcessSpec spec{testSpec.sampleRate, testSpec.maximumFrameCount, testSpec.maximumChannelCount};
    check(PXEnginePrepare(engine, spec), "C interface prepares the shared engine");
    check(PXEngineLatencyFrames(engine) == 56, "C interface reports total DSP latency");

    TestBuffer signal(2, 256);
    signal.storage[0][0] = 1.5F;
    signal.storage[1][0] = 1.5F;
    check(PXEngineProcess(
        engine, signal.pointers.data(), signal.storage.size(), signal.storage.front().size()),
        "C interface processes valid non-interleaved audio");
    const auto meters = PXEngineMeters(engine);
    check(approximately(meters.inputPeak, 1.5F), "C interface exposes engine meters");
    check(meters.outputPeak <= decibelsToLinear(-1.0F) + 0.0001F,
        "positive final output trim cannot defeat limiter protection");
    check(PXEngineStereoCorrelation(engine) >= -1.0F && PXEngineStereoCorrelation(engine) <= 1.0F,
        "C interface exposes bounded stereo correlation");
    check(!PXEngineSetEqualizerBandCount(engine, kMaxEQBands + 1),
        "C interface rejects an excessive EQ band count");

    PXEngineDestroy(engine);
}

void testSealedPCMSource() {
    auto* source = PXPCMSourceCreate(48'000.0, 2);
    check(source != nullptr, "PCM source creates with a valid format");
    if (source == nullptr) {
        return;
    }

    std::array<float, 3> firstLeft{0.1F, 0.2F, 0.3F};
    std::array<float, 3> firstRight{-0.1F, -0.2F, -0.3F};
    std::array<float*, 2> firstPointers{firstLeft.data(), firstRight.data()};
    std::array<float, 2> secondLeft{0.4F, std::numeric_limits<float>::infinity()};
    std::array<float, 2> secondRight{-0.4F, -0.5F};
    std::array<float*, 2> secondPointers{secondLeft.data(), secondRight.data()};
    check(PXPCMSourceAppend(source, firstPointers.data(), 2, firstLeft.size()),
        "PCM source appends its first decoded chunk");
    check(PXPCMSourceAppend(source, secondPointers.data(), 2, secondLeft.size()),
        "PCM source appends another decoded chunk");
    check(PXPCMSourceSeal(source), "PCM source seals before rendering");
    check(!PXPCMSourceSeal(source), "sealed PCM source rejects a second seal");
    check(PXPCMSourceFrameCount(source) == 5, "PCM source reports its complete frame count");
    check(approximately(static_cast<float>(PXPCMSourceSampleRate(source)), 48'000.0F),
        "PCM source retains its sample rate");
    check(PXPCMSourceChannelCount(source) == 2, "PCM source retains its channel count");
    check(!PXPCMSourceAppend(source, firstPointers.data(), 2, firstLeft.size()),
        "sealed PCM source rejects mutation");

    TestBuffer output(2, 8);
    check(PXPCMSourceRender(source, output.pointers.data(), 2, 8, false),
        "PCM source renders a nonlooping block");
    check(approximately(output.storage[0][0], 0.1F)
        && approximately(output.storage[1][3], -0.4F),
        "PCM source preserves channel samples");
    check(output.storage[0][4] == 0.0F, "PCM source sanitizes non-finite decoded samples");
    check(output.storage[0][7] == 0.0F && output.storage[1][7] == 0.0F,
        "nonlooping PCM source pads its terminal block with silence");
    check(PXPCMSourceFinished(source), "nonlooping PCM source publishes end-of-file");

    PXPCMSourceReset(source);
    TestBuffer looped(2, 7);
    check(PXPCMSourceRender(source, looped.pointers.data(), 2, 7, true),
        "PCM source renders a looping block");
    check(approximately(looped.storage[0][5], 0.1F)
        && approximately(looped.storage[1][6], -0.2F),
        "looping PCM source wraps without allocation");
    check(!PXPCMSourceFinished(source), "looping PCM source does not publish end-of-file");
    check(PXPCMSourcePosition(source) == 2, "PCM source publishes its wrapped position");

    PXPCMSourceDestroy(source);
}

} // namespace

int main() {
    testConversionsAndSmoothing();
    testGain();
    testAllFilterShapesRemainFinite();
    testEqualizerBandAndBypass();
    testBassEnhancerModesAndBypass();
    testExciterAddsProtectedHighBandHarmonics();
    testCrystalizerRespondsToTransientContrast();
    testStereoToolsWidthMonoBassAndCorrelation();
    testLimiterCeilingLatencyAndLinking();
    testLimiterDetectsIntersamplePeaks();
    testChainAndMeters();
    testCompleteSevenStageGraph();
    testLatencyMatchedGlobalBypass();
    testCInterface();
    testSealedPCMSource();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PhoenauxDSP tests passed\n";
    return EXIT_SUCCESS;
}
