func checkPhoenauxCBridge(
    source: OpaquePointer,
    channels: UnsafePointer<UnsafeMutablePointer<Float>?>
) {
    let spec = PXProcessSpec(
        sampleRate: 48_000,
        maximumFrameCount: 4_096,
        maximumChannelCount: 2
    )
    _ = spec
    _ = PXPCMSourceAppend(source, channels, 2, 512)
    _ = PXPCMSourceFrameCount(source)
    _ = PXPCMSourcePosition(source)
    _ = PXPCMSourceFinished(source)
}
