#ifndef PHOENAUX_DSP_APPLE_H
#define PHOENAUX_DSP_APPLE_H

#include "PhoenauxDSP.h"

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PXEngineProcessAudioBufferList(
    PXEngine* engine,
    AudioBufferList* audioBufferList,
    uint32_t frameCount);

bool PXPCMSourceRenderAudioBufferList(
    PXPCMSource* source,
    AudioBufferList* audioBufferList,
    uint32_t frameCount,
    bool loop);

#ifdef __cplusplus
} // extern "C"
#endif
#endif // defined(__APPLE__)

#endif // PHOENAUX_DSP_APPLE_H
