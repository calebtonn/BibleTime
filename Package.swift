// swift-tools-version:5.9
// This package exposes the platform-independent core (verse data, selection, preferences) so it can be
// unit-tested with `swift run CoreTests Resources` and browsed in any Swift editor.
// The screen saver bundle itself is built by the Makefile (or BibleTime.xcodeproj), not by SwiftPM,
// because SwiftPM cannot produce a .saver bundle.
import PackageDescription

let package = Package(
    name: "BibleTime",
    platforms: [.macOS(.v12)],
    products: [
        .library(name: "BibleTimeCore", targets: ["BibleTimeCore"]),
    ],
    targets: [
        .target(name: "BibleTimeCore", path: "Sources/BibleTimeCore"),
        .executableTarget(name: "CoreTests", dependencies: ["BibleTimeCore"], path: "Tests/CoreTests"),
        // Prints the core's choices for comparison with the Windows C++ port; see `make crosscheck`.
        .executableTarget(name: "CrossCheck", dependencies: ["BibleTimeCore"], path: "Tests/CrossCheck"),
    ]
)
