import SwiftUI
import UIKit

final class CrossDeskAppDelegate: NSObject, UIApplicationDelegate {
    static var supportedOrientations: UIInterfaceOrientationMask = .portrait

    func application(_ application: UIApplication,
                     supportedInterfaceOrientationsFor window: UIWindow?)
        -> UIInterfaceOrientationMask {
        Self.supportedOrientations
    }
}

enum AppOrientation {
    static func update(to orientations: UIInterfaceOrientationMask) {
        DispatchQueue.main.async {
            CrossDeskAppDelegate.supportedOrientations = orientations

            for case let scene as UIWindowScene in UIApplication.shared.connectedScenes {
                scene.windows.forEach {
                    $0.rootViewController?.setNeedsUpdateOfSupportedInterfaceOrientations()
                }
                scene.requestGeometryUpdate(
                    .iOS(interfaceOrientations: orientations)
                ) { error in
                    NSLog("CrossDesk orientation update failed: %@", error.localizedDescription)
                }
            }
        }
    }
}

@main
struct CrossDeskMobileApp: App {
    @UIApplicationDelegateAdaptor(CrossDeskAppDelegate.self) private var appDelegate
    @StateObject private var session = RemoteSessionModel()

    var body: some Scene {
        WindowGroup {
            ContentView(session: session)
        }
    }
}
