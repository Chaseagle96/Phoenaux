import AudioToolbox

enum PhoenauxAUParameterAddress: AUParameterAddress, CaseIterable {
    case bypass = 0
    case inputGainDB = 1
    case outputGainDB = 2
    case filterEnabled = 10
    case highPassFrequency = 11
    case equalizerEnabled = 20
    case lowGainDB = 21
    case presenceGainDB = 22
    case airGainDB = 23
    case bassEnabled = 30
    case bassAmount = 31
    case exciterEnabled = 40
    case exciterAmount = 41
    case crystalizerEnabled = 50
    case crystalizerAmount = 51
    case stereoEnabled = 60
    case stereoWidth = 61
    case limiterEnabled = 70
    case limiterCeilingDB = 71
}

enum PhoenauxAUParameters {
    static func makeTree() -> AUParameterTree {
        AUParameterTree.createTree(withChildren: [
            parameter("bypass", "Bypass", .bypass, 0, 1, .boolean, 0),
            parameter("inputGain", "Input Gain", .inputGainDB, -24, 6, .decibels, -4),
            parameter("outputGain", "Output Gain", .outputGainDB, -24, 0, .decibels, 0),
            parameter("filterEnabled", "Filter", .filterEnabled, 0, 1, .boolean, 1),
            parameter("highPass", "High-Pass", .highPassFrequency, 20, 200, .hertz, 28),
            parameter("equalizerEnabled", "Equalizer", .equalizerEnabled, 0, 1, .boolean, 1),
            parameter("lowGain", "Low", .lowGainDB, -12, 12, .decibels, 2),
            parameter("presenceGain", "Presence", .presenceGainDB, -12, 12, .decibels, 1.5),
            parameter("airGain", "Air", .airGainDB, -12, 12, .decibels, 1),
            parameter("bassEnabled", "Bass Enhancer", .bassEnabled, 0, 1, .boolean, 1),
            parameter("bassAmount", "Bass Amount", .bassAmount, 0, 1, .generic, 0.36),
            parameter("exciterEnabled", "Exciter", .exciterEnabled, 0, 1, .boolean, 1),
            parameter("exciterAmount", "Exciter Amount", .exciterAmount, 0, 1, .generic, 0.18),
            parameter("crystalizerEnabled", "Crystalizer", .crystalizerEnabled, 0, 1, .boolean, 1),
            parameter("crystalizerAmount", "Crystalizer Amount", .crystalizerAmount, 0, 1.5, .generic, 0.24),
            parameter("stereoEnabled", "Stereo Tools", .stereoEnabled, 0, 1, .boolean, 1),
            parameter("stereoWidth", "Stereo Width", .stereoWidth, 0, 2, .generic, 1.12),
            parameter("limiterEnabled", "Limiter", .limiterEnabled, 0, 1, .boolean, 1),
            parameter("limiterCeiling", "Limiter Ceiling", .limiterCeilingDB, -24, 0, .decibels, -1),
        ])
    }

    private static func parameter(
        _ identifier: String,
        _ name: String,
        _ address: PhoenauxAUParameterAddress,
        _ minimum: AUValue,
        _ maximum: AUValue,
        _ unit: AudioUnitParameterUnit,
        _ initialValue: AUValue
    ) -> AUParameter {
        let parameter = AUParameterTree.createParameter(
            withIdentifier: identifier,
            name: name,
            address: address.rawValue,
            min: minimum,
            max: maximum,
            unit: unit,
            unitName: nil,
            flags: [.flag_IsReadable, .flag_IsWritable],
            valueStrings: nil,
            dependentParameters: nil
        )
        parameter.value = initialValue
        return parameter
    }
}
