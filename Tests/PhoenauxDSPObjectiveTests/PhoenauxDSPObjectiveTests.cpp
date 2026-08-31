#include <PhoenauxDSP/PhoenauxDSP.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using namespace phoenaux::dsp;

constexpr double pi = 3.14159265358979323846;
constexpr ProcessSpec spec{48'000.0, 8'192, 2};
constexpr std::size_t analysisStart = 4'096;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct TestBuffer {
    TestBuffer(std::size_t channelCount, std::size_t frameCount)
        : storage(channelCount, std::vector<float>(frameCount)), pointers(channelCount) {
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

void fillTone(TestBuffer& buffer, double frequency, float amplitude = 0.1F) {
    for (std::size_t frame = 0; frame < buffer.storage.front().size(); ++frame) {
        const auto sample = amplitude * static_cast<float>(std::sin(
            2.0 * pi * frequency * static_cast<double>(frame) / spec.sampleRate));
        for (auto& channel : buffer.storage) {
            channel[frame] = sample;
        }
    }
}

double rms(const std::vector<float>& samples, std::size_t start) {
    double energy = 0.0;
    for (std::size_t frame = start; frame < samples.size(); ++frame) {
        const auto sample = static_cast<double>(samples[frame]);
        energy += sample * sample;
    }
    return std::sqrt(energy / static_cast<double>(samples.size() - start));
}

double toneAmplitude(
    const std::vector<float>& samples,
    double frequency,
    std::size_t start
) {
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t frame = start; frame < samples.size(); ++frame) {
        const auto phase = 2.0 * pi * frequency * static_cast<double>(frame) / spec.sampleRate;
        const auto sample = static_cast<double>(samples[frame]);
        real += sample * std::cos(phase);
        imaginary -= sample * std::sin(phase);
    }
    const auto count = static_cast<double>(samples.size() - start);
    return 2.0 * std::hypot(real, imaginary) / count;
}

double highPassGain(double frequency) {
    FilterNode filter;
    filter.setEnabled(true);
    filter.setType(FilterType::highPass);
    filter.setFrequency(200.0F);
    filter.setQ(0.70710678F);
    check(filter.prepare(spec), "objective high-pass prepares");

    TestBuffer buffer(2, spec.maximumFrameCount);
    fillTone(buffer, frequency);
    const auto inputRMS = rms(buffer.storage.front(), analysisStart);
    filter.process(buffer.view());
    return rms(buffer.storage.front(), analysisStart) / inputRMS;
}

void testHighPassResponse() {
    const auto stopbandGain = highPassGain(50.0);
    const auto passbandGain = highPassGain(2'000.0);
    check(stopbandGain < 0.07, "200 Hz high-pass attenuates 50 Hz by at least 23 dB");
    check(passbandGain > 0.99 && passbandGain < 1.01,
        "200 Hz high-pass remains within about 0.1 dB at 2 kHz");
}

void testBellCenterGain() {
    ParametricEQ equalizer;
    equalizer.setEnabled(true);
    equalizer.setBandCount(1);
    auto& band = equalizer.band(0);
    band.setEnabled(true);
    band.setType(FilterType::bell);
    band.setFrequency(1'000.0F);
    band.setQ(1.0F);
    band.setGainDB(6.0F);
    check(equalizer.prepare(spec), "objective bell EQ prepares");

    TestBuffer buffer(2, spec.maximumFrameCount);
    fillTone(buffer, 1'000.0);
    const auto inputRMS = rms(buffer.storage.front(), analysisStart);
    equalizer.process(buffer.view());
    const auto gain = rms(buffer.storage.front(), analysisStart) / inputRMS;
    check(gain > 1.97 && gain < 2.02, "+6 dB bell reaches its center-frequency target");
}

void testExciterHarmonicGeneration() {
    Exciter exciter;
    exciter.setEnabled(true);
    exciter.setFrequency(3'000.0F);
    exciter.setDrive(6.0F);
    exciter.setAmount(1.0F);
    exciter.setMix(1.0F);
    check(exciter.prepare(spec), "objective exciter prepares");

    TestBuffer buffer(2, spec.maximumFrameCount);
    fillTone(buffer, 6'000.0, 0.2F);
    exciter.process(buffer.view());
    const auto fundamental = toneAmplitude(buffer.storage.front(), 6'000.0, analysisStart);
    const auto thirdHarmonic = toneAmplitude(buffer.storage.front(), 18'000.0, analysisStart);
    const auto harmonicRatio = thirdHarmonic / std::max(fundamental, 0.000001);
    check(harmonicRatio > 0.005, "exciter creates a measurable high-band third harmonic");
    check(harmonicRatio < 0.5, "exciter harmonic contribution remains bounded");
}

void testWidthPreservesMonoDownmix() {
    StereoTools stereo;
    stereo.setEnabled(true);
    stereo.setWidth(1.5F);
    stereo.setMonoBassAmount(0.0F);
    check(stereo.prepare(spec), "objective stereo tools prepare");

    TestBuffer buffer(2, spec.maximumFrameCount);
    std::vector<float> monoBefore(spec.maximumFrameCount);
    for (std::size_t frame = 0; frame < spec.maximumFrameCount; ++frame) {
        const auto left = 0.15F * static_cast<float>(std::sin(
            2.0 * pi * 1'000.0 * static_cast<double>(frame) / spec.sampleRate));
        const auto right = 0.11F * static_cast<float>(std::sin(
            2.0 * pi * 2'000.0 * static_cast<double>(frame) / spec.sampleRate + 0.4));
        buffer.storage[0][frame] = left;
        buffer.storage[1][frame] = right;
        monoBefore[frame] = (left + right) * 0.5F;
    }
    stereo.process(buffer.view());

    double maximumError = 0.0;
    for (std::size_t frame = analysisStart; frame < spec.maximumFrameCount; ++frame) {
        const auto monoAfter = static_cast<double>(
            (buffer.storage[0][frame] + buffer.storage[1][frame]) * 0.5F);
        maximumError = std::max(
            maximumError,
            std::abs(monoAfter - static_cast<double>(monoBefore[frame])));
    }
    check(maximumError < 0.00001,
        "width processing preserves the mono downmix when mid gain and balance are neutral");
}

} // namespace

int main() {
    testHighPassResponse();
    testBellCenterGain();
    testExciterHarmonicGeneration();
    testWidthPreservesMonoDownmix();

    if (failures != 0) {
        std::cerr << failures << " objective assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PhoenauxDSP objective tests passed\n";
    return EXIT_SUCCESS;
}
