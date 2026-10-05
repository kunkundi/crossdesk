import SwiftUI
import UIKit

final class CrossDeskAppDelegate: NSObject, UIApplicationDelegate {
    static var supportedOrientations: UIInterfaceOrientationMask =
        UIDevice.current.userInterfaceIdiom == .pad ? .all : .portrait

    func application(_ application: UIApplication,
                     supportedInterfaceOrientationsFor window: UIWindow?)
        -> UIInterfaceOrientationMask {
        Self.supportedOrientations
    }
}

enum AppOrientation {
    static func update(to orientations: UIInterfaceOrientationMask) {
        DispatchQueue.main.async {
            // iPad windows can rotate and resize independently of the session.
            // Keep all orientations available and let the window manager choose
            // the geometry; the phone still locks the home screen to portrait.
            guard UIDevice.current.userInterfaceIdiom != .pad else { return }
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
