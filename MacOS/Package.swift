// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "IwanaProxy",
    platforms: [
        .macOS(.v14) // Sonoma+; runs fine on Intel and Apple Silicon.
        // (Needed for the two-parameter `.onChange` API used in
        // SettingsView.swift. Lower this back to .v13 and switch those
        // .onChange calls to the single-parameter { newValue in } form if
        // you need to support Ventura.)
    ],
    targets: [
        .executableTarget(
            name: "IwanaProxy",
            path: "Sources/IwanaProxy"
        )
    ]
)
