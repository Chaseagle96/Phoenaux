import AudioToolbox
import Foundation

@objc(PhoenauxAudioUnitFactory)
final class PhoenauxAudioUnitFactory: NSObject, AUAudioUnitFactory {
    func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        try PhoenauxAudioUnit(componentDescription: componentDescription, options: [])
    }

    func beginRequest(with context: NSExtensionContext) {
        context.completeRequest(returningItems: nil)
    }
}
