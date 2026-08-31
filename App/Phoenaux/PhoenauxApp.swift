import SwiftUI

@main
struct PhoenauxApp: App {
    @State private var model = PhoenauxAppModel()

    var body: some Scene {
        WindowGroup {
            HomeView(model: model)
        }
    }
}
