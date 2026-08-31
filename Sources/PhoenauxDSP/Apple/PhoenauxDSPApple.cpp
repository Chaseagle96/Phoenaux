#include <PhoenauxDSP/PhoenauxDSPApple.h>

#if defined(__APPLE__)

#include <cstddef>

extern "C" bool PXEngineProcessAudioBufferList(
    PXEngine* engine,
    AudioBufferList* audioBufferList,
    uint32_t frameCount) {
    if (engine == nullptr || audioBufferList == nullptr
        || audioBufferList->mNumberBuffers == 0
        || audioBufferList->mNumberBuffers > 8) {
        return false;
    }

    float* channels[8]{};
    for (uint32_t index = 0; index < audioBufferList->mNumberBuffers; ++index) {
        auto& buffer = audioBufferList->mBuffers[index];
        const auto requiredBytes = static_cast<std::size_t>(frameCount) * sizeof(float);
        if (buffer.mNumberChannels != 1 || buffer.mData == nullptr
            || static_cast<std::size_t>(buffer.mDataByteSize) < requiredBytes) {
            return false;
        }
        channels[index] = static_cast<float*>(buffer.mData);
    }

    return PXEngineProcess(
        engine,
        channels,
        static_cast<std::size_t>(audioBufferList->mNumberBuffers),
        static_cast<std::size_t>(frameCount));
}

extern "C" bool PXPCMSourceRenderAudioBufferList(
    PXPCMSource* source,
    AudioBufferList* audioBufferList,
    uint32_t frameCount,
    bool loop) {
    if (source == nullptr || audioBufferList == nullptr
        || audioBufferList->mNumberBuffers == 0
        || audioBufferList->mNumberBuffers > 8) {
        return false;
    }

    float* channels[8]{};
    for (uint32_t index = 0; index < audioBufferList->mNumberBuffers; ++index) {
        auto& buffer = audioBufferList->mBuffers[index];
        const auto requiredBytes = static_cast<std::size_t>(frameCount) * sizeof(float);
        if (buffer.mNumberChannels != 1 || buffer.mData == nullptr
            || static_cast<std::size_t>(buffer.mDataByteSize) < requiredBytes) {
            return false;
        }
        channels[index] = static_cast<float*>(buffer.mData);
    }

    return PXPCMSourceRender(
        source,
        channels,
        static_cast<std::size_t>(audioBufferList->mNumberBuffers),
        static_cast<std::size_t>(frameCount),
        loop);
}

#endif // defined(__APPLE__)
