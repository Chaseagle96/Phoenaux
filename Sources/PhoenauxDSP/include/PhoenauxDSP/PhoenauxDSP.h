#ifndef PHOENAUX_DSP_H
#define PHOENAUX_DSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PXEngine PXEngine;
typedef struct PXPCMSource PXPCMSource;

typedef enum PXFilterType {
    PXFilterTypeLowPass = 0,
    PXFilterTypeHighPass = 1,
    PXFilterTypeLowShelf = 2,
    PXFilterTypeHighShelf = 3,
    PXFilterTypeBell = 4,
    PXFilterTypeBandPass = 5,
    PXFilterTypeNotch = 6,
} PXFilterType;

typedef enum PXBassEnhancerMode {
    PXBassEnhancerModePsychoacoustic = 0,
    PXBassEnhancerModeExtension = 1,
} PXBassEnhancerMode;

typedef struct PXProcessSpec {
    double sampleRate;
    size_t maximumFrameCount;
    size_t maximumChannelCount;
} PXProcessSpec;

typedef struct PXMeterSnapshot {
    float inputPeak;
    float outputPeak;
    float gainReductionDB;
    bool clipped;
} PXMeterSnapshot;

PXEngine* PXEngineCreate(void);
void PXEngineDestroy(PXEngine* engine);

bool PXEnginePrepare(PXEngine* engine, PXProcessSpec spec);
void PXEngineReset(PXEngine* engine);
bool PXEngineProcess(
    PXEngine* engine,
    float* const* channels,
    size_t channelCount,
    size_t frameCount);

void PXEngineSetBypassed(PXEngine* engine, bool bypassed);

void PXEngineSetInputGainDB(PXEngine* engine, float gainDB);
void PXEngineSetOutputGainDB(PXEngine* engine, float gainDB);

void PXEngineSetFilterEnabled(PXEngine* engine, bool enabled);
void PXEngineSetFilter(
    PXEngine* engine,
    PXFilterType type,
    float frequency,
    float q,
    float gainDB);

void PXEngineSetEqualizerEnabled(PXEngine* engine, bool enabled);
bool PXEngineSetEqualizerBandCount(PXEngine* engine, size_t count);
bool PXEngineSetEqualizerBand(
    PXEngine* engine,
    size_t index,
    bool enabled,
    PXFilterType type,
    float frequency,
    float q,
    float gainDB);

void PXEngineSetBassEnhancerEnabled(PXEngine* engine, bool enabled);
void PXEngineSetBassEnhancer(
    PXEngine* engine,
    PXBassEnhancerMode mode,
    float crossoverFrequency,
    float amount,
    float drive,
    float mix);

void PXEngineSetExciterEnabled(PXEngine* engine, bool enabled);
void PXEngineSetExciter(
    PXEngine* engine,
    float frequency,
    float drive,
    float amount,
    float mix);

void PXEngineSetCrystalizerEnabled(PXEngine* engine, bool enabled);
void PXEngineSetCrystalizer(
    PXEngine* engine,
    float frequency,
    float amount,
    float sensitivity,
    float mix);

void PXEngineSetStereoToolsEnabled(PXEngine* engine, bool enabled);
void PXEngineSetStereoTools(
    PXEngine* engine,
    float width,
    float midGainDB,
    float sideGainDB,
    float balance,
    float monoBassFrequency,
    float monoBassAmount);
void PXEngineSetStereoSwitches(
    PXEngine* engine,
    bool swapChannels,
    bool mono,
    bool invertLeft,
    bool invertRight);
float PXEngineStereoCorrelation(const PXEngine* engine);

void PXEngineSetLimiterEnabled(PXEngine* engine, bool enabled);
void PXEngineSetLimiter(
    PXEngine* engine,
    float ceilingDB,
    float lookaheadMilliseconds,
    float releaseMilliseconds);
size_t PXEngineLatencyFrames(const PXEngine* engine);
PXMeterSnapshot PXEngineMeters(const PXEngine* engine);

PXPCMSource* PXPCMSourceCreate(double sampleRate, size_t channelCount);
void PXPCMSourceDestroy(PXPCMSource* source);
bool PXPCMSourceAppend(
    PXPCMSource* source,
    float* const* channels,
    size_t channelCount,
    size_t frameCount);
bool PXPCMSourceSeal(PXPCMSource* source);
void PXPCMSourceReset(PXPCMSource* source);
bool PXPCMSourceRender(
    PXPCMSource* source,
    float* const* outputChannels,
    size_t outputChannelCount,
    size_t frameCount,
    bool loop);
double PXPCMSourceSampleRate(const PXPCMSource* source);
size_t PXPCMSourceChannelCount(const PXPCMSource* source);
size_t PXPCMSourceFrameCount(const PXPCMSource* source);
size_t PXPCMSourcePosition(const PXPCMSource* source);
bool PXPCMSourceFinished(const PXPCMSource* source);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PHOENAUX_DSP_H
