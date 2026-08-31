// swift-tools-version: 6.2

import PackageDescription

let package = Package(
    name: "PhoenauxDSP",
    platforms: [
        .iOS(.v18),
        .macOS(.v15),
    ],
    products: [
        .library(name: "PhoenauxDSP", targets: ["PhoenauxDSP"]),
    ],
    targets: [
        .target(
            name: "PhoenauxDSP",
            path: "Sources/PhoenauxDSP",
            publicHeadersPath: "include"
        ),
    ],
    cxxLanguageStandard: .cxx20
)
