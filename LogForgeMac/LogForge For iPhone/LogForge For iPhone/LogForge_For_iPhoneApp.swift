import SwiftUI

@main
struct LogForgeForiPhoneApp: App {
    // All scenes observe one queue and one conversion task. Closing a window must
    // not create a second pipeline or let two queues write the same journal.
    @StateObject private var queue = ConversionQueue()
    var body: some Scene {
        WindowGroup {
            ContentView().environmentObject(queue)
                .onContinueUserActivity(WorkspaceWindowMenu.activityType) { _ in }
        }
    }
}
