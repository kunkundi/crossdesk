import SwiftUI
import UIKit

enum MobileLayout {
    static let twoColumnBreakpoint: CGFloat = 900
    static let homeMaximumWidth: CGFloat = 1200
    static let readableMaximumWidth: CGFloat = 720
}

extension View {
    func readableContentWidth() -> some View {
        frame(maxWidth: MobileLayout.readableMaximumWidth)
            .frame(maxWidth: .infinity)
    }
}

enum HomeDestination: Hashable {
    case settings
    case announcements
    case announcement(AnnouncementSelection)
}

struct ContentView: View {
    @Environment(\.scenePhase) private var scenePhase
    @ObservedObject var session: RemoteSessionModel

    var body: some View {
        Group {
            if session.sessionVisible {
                RemoteSessionView(session: session)
                    .transition(.opacity)
            } else {
                NavigationStack {
                    ConnectionHomeView(session: session)
                        .navigationTitle("")
                        .navigationBarTitleDisplayMode(.inline)
                        // Keep inbox and detail in the same value-based stack.
                        // Mixing an isPresented inbox with a value-based detail
                        // can reinsert the inbox above the detail on updates.
                        .navigationDestination(for: HomeDestination.self) { destination in
                            switch destination {
                            case .settings:
                                ServerSettingsView(session: session)
                            case .announcements:
                                AnnouncementInboxView(session: session, inbox: session.announcements)
                            case .announcement(let selection):
                                AnnouncementDetailView(inbox: session.announcements, selection: selection)
                            }
                        }
                }
                .toolbarBackground(Color(.systemGroupedBackground), for: .navigationBar)
                .toolbarBackground(.visible, for: .navigationBar)
                .transition(.opacity)
            }
        }
        .animation(.easeInOut(duration: 0.2), value: session.sessionVisible)
        .environmentObject(session.appUpdates)
        .preferredColorScheme(session.sessionVisible ? .dark : .light)
        .sheet(isPresented: Binding(
            get: { session.privacyNoticeVisible },
            set: { if !$0 { session.dismissPrivacyNotice() } }
        )) {
            PrivacyConsentView(session: session)
                .presentationDetents([.large])
                .presentationDragIndicator(.hidden)
                .interactiveDismissDisabled()
        }
        .alert("连接提示", isPresented: Binding(
            get: { session.connectionFailureMessage != nil },
            set: { if !$0 { session.dismissConnectionFailure() } }
        )) {
            Button("确定", role: .cancel) { session.dismissConnectionFailure() }
        } message: {
            Text(session.connectionFailureMessage ?? "")
        }
        .onAppear {
            if !session.sessionVisible {
                AppOrientation.update(to: .portrait)
            }
            if scenePhase == .active {
                session.applicationDidBecomeActive()
            }
        }
        .onChange(of: scenePhase) { phase in
            if phase == .active {
                session.applicationDidBecomeActive()
            } else if phase == .background {
                session.applicationDidEnterBackground()
            }
        }
    }
}

private struct KeyboardDismissTapView: UIViewRepresentable {
    let onDismiss: () -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(onDismiss: onDismiss)
    }

    func makeUIView(context: Context) -> UIView {
        let view = UIView(frame: .zero)
        view.backgroundColor = .clear
        view.isUserInteractionEnabled = false
        installRecognizer(for: view, coordinator: context.coordinator)
        return view
    }

    func updateUIView(_ view: UIView, context: Context) {
        context.coordinator.onDismiss = onDismiss
        installRecognizer(for: view, coordinator: context.coordinator)
    }

    static func dismantleUIView(_ view: UIView, coordinator: Coordinator) {
        coordinator.uninstall()
    }

    private func installRecognizer(for view: UIView, coordinator: Coordinator) {
        DispatchQueue.main.async { [weak view, weak coordinator] in
            guard let window = view?.window else { return }
            coordinator?.install(in: window)
        }
    }

    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var onDismiss: () -> Void
        private weak var window: UIWindow?
        private var recognizer: UITapGestureRecognizer?

        init(onDismiss: @escaping () -> Void) {
            self.onDismiss = onDismiss
        }

        func install(in window: UIWindow) {
            guard self.window !== window || recognizer == nil else { return }
            uninstall()

            let recognizer = UITapGestureRecognizer(target: self,
                                                    action: #selector(didTapOutsideInput))
            recognizer.cancelsTouchesInView = false
            recognizer.delegate = self
            window.addGestureRecognizer(recognizer)
            self.window = window
            self.recognizer = recognizer
        }

        func uninstall() {
            if let recognizer {
                window?.removeGestureRecognizer(recognizer)
            }
            recognizer = nil
            window = nil
        }

        func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                               shouldReceive touch: UITouch) -> Bool {
            var touchedView = touch.view
            while let view = touchedView {
                if view is UITextField || view is UITextView {
                    return false
                }
                touchedView = view.superview
            }
            return true
        }

        @objc private func didTapOutsideInput() {
            onDismiss()
            window?.endEditing(true)
        }
    }
}

private struct ConnectionHomeView: View {
    @EnvironmentObject private var appUpdates: AppUpdateChecker
    @ObservedObject var session: RemoteSessionModel
    @FocusState private var remoteIDFocused: Bool
    @State private var passwordPromptVisible = false
    @State private var promptRemoteID = ""
    @State private var promptPassword = ""
    @State private var promptRememberPassword = false

    private let connectionAccent = Color(red: 0.18, green: 0.48, blue: 0.86)
    private let connectionAccentEnd = Color(red: 0.12, green: 0.38, blue: 0.78)

    private var trimmedRemoteID: String {
        session.remoteID.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private var groupedRemoteID: Binding<String> {
        Binding(
            get: { formatRemoteID(session.remoteID) },
            set: { value in
                session.remoteID = String(value.filter { $0.isNumber }.prefix(9))
            }
        )
    }

    private var signalIsConnected: Bool {
        session.signalStatus == "已连接服务器"
    }

    private var signalHasError: Bool {
        session.signalStatus.contains("失败") ||
            session.signalStatus.contains("无法") ||
            session.signalStatus.contains("无效") ||
            session.signalStatus.contains("关闭") ||
            session.signalStatus.contains("未知")
    }

    private var signalTint: Color {
        if signalIsConnected { return .green }
        if signalHasError { return .red }
        return .orange
    }

    private var signalSymbol: String {
        if signalIsConnected { return "checkmark.circle.fill" }
        if signalHasError { return "exclamationmark.triangle.fill" }
        return "arrow.triangle.2.circlepath"
    }

    private func recentConnectionColumns(windowWidth: CGFloat) -> [GridItem] {
        if windowWidth >= 600 {
            return [GridItem(.adaptive(minimum: 180), spacing: 12, alignment: .top)]
        }
        return [
            GridItem(.flexible(), spacing: 12, alignment: .top),
            GridItem(.flexible(), spacing: 12, alignment: .top)
        ]
    }

    private var orderedRecentConnections: [RecentConnection] {
        session.recentConnections.enumerated()
            .sorted { lhs, rhs in
                let lhsOnline = session.isRecentConnectionOnline(lhs.element)
                let rhsOnline = session.isRecentConnectionOnline(rhs.element)
                if lhsOnline != rhsOnline { return lhsOnline }
                return lhs.offset < rhs.offset
            }
            .map(\.element)
    }

    var body: some View {
        ZStack {
            Color(.systemGroupedBackground).ignoresSafeArea()

            GeometryReader { proxy in
                let isWide = proxy.size.width >= MobileLayout.twoColumnBreakpoint
                // AnyLayout keeps the fields, focus and scroll view alive when
                // rotation or iPad multitasking crosses the layout breakpoint.
                let layout = isWide
                    ? AnyLayout(HStackLayout(alignment: .top, spacing: 24))
                    : AnyLayout(VStackLayout(spacing: 20))

                ScrollView(.vertical) {
                    layout {
                        remoteConnectionPanel
                            .frame(width: isWide ? 360 : nil)
                        recentConnectionsPanel(windowWidth: proxy.size.width)
                            .frame(maxHeight: .infinity)
                    }
                    .frame(maxWidth: MobileLayout.homeMaximumWidth)
                    // Short multitasking windows and the onscreen keyboard
                    // must not squeeze the recent list below a usable height.
                    .frame(height: max(isWide ? 260 : 420, proxy.size.height - 36),
                           alignment: .top)
                    .frame(maxWidth: .infinity)
                    .padding(.horizontal, 20)
                    .padding(.top, 24)
                    .padding(.bottom, 12)
                }
                .scrollDismissesKeyboard(.interactively)
            }

            if session.isConnecting {
                connectionProgressOverlay
            }

        }
        .toolbar {
            if #available(iOS 26.0, *) {
                navigationToolbar.sharedBackgroundVisibility(.hidden)
            } else {
                navigationToolbar
            }
        }
        .background {
            KeyboardDismissTapView {
                remoteIDFocused = false
            }
            .allowsHitTesting(false)
        }
        .sheet(isPresented: $passwordPromptVisible) {
            PasswordPromptView(remoteID: promptRemoteID,
                               password: $promptPassword,
                               rememberPassword: $promptRememberPassword) {
                passwordPromptVisible = false
                session.remoteID = promptRemoteID
                session.connect(password: promptPassword,
                                rememberPassword: promptRememberPassword)
            }
            .presentationDetents([.height(340)])
            .presentationDragIndicator(.visible)
        }
        .alert("设备离线", isPresented: Binding(
            get: { session.deviceOfflineAlertVisible },
            set: { visible in
                if !visible { session.dismissDeviceOfflineAlert() }
            }
        )) {
            Button("确定") {
                session.dismissDeviceOfflineAlert()
            }
        }
        .onChange(of: session.remoteID) { value in
            let formatted = String(value.filter { $0.isNumber }.prefix(9))
            if formatted != value {
                session.remoteID = formatted
            }
        }
        .onDisappear { remoteIDFocused = false }
    }

    @ToolbarContentBuilder
    private var navigationToolbar: some ToolbarContent {
        ToolbarItem(placement: .navigationBarLeading) {
            if session.hasNetworkConsent {
                signalStatusBadge
                    .fixedSize(horizontal: true, vertical: false)
            } else {
                Button {
                    remoteIDFocused = false
                    session.showPrivacyNotice()
                } label: {
                    signalStatusBadge
                        .fixedSize(horizontal: true, vertical: false)
                }
                .buttonStyle(.plain)
                .accessibilityHint("打开隐私政策")
            }
        }
        ToolbarItem(placement: .navigationBarTrailing) {
            NavigationLink(value: HomeDestination.announcements) {
                AnnouncementBadge(inbox: session.announcements)
            }
            .foregroundStyle(.primary)
            .disabled(session.isConnecting)
        }
        ToolbarItem(placement: .navigationBarTrailing) {
            NavigationLink(value: HomeDestination.settings) {
                Image(systemName: "gearshape")
                    .font(.system(size: 18, weight: .semibold))
                    .frame(width: 40, height: 40)
                    .background(Color(.secondarySystemGroupedBackground),
                                in: RoundedRectangle(cornerRadius: 10,
                                                     style: .continuous))
                    .overlay(alignment: .topTrailing) {
                        if appUpdates.updateAvailable {
                            Text("!")
                                .font(.system(size: 10, weight: .bold))
                                .foregroundStyle(.white)
                                .frame(width: 16, height: 16)
                                .background(.red, in: Circle())
                                .offset(x: 5, y: -3)
                                .accessibilityHidden(true)
                        }
                    }
            }
            .foregroundStyle(.primary)
            .disabled(session.isConnecting)
            .accessibilityLabel(appUpdates.updateAvailable ? "设置，有新版本" : "设置")
        }
    }

    private var signalStatusBadge: some View {
        HStack(spacing: 6) {
            if session.hasNetworkConsent {
                Image(systemName: signalSymbol)
                    .font(.caption.weight(.bold))
            }
            Text(session.signalStatus)
                .font(.caption.weight(.semibold))
                .lineLimit(1)
        }
        .foregroundStyle(signalTint)
        .padding(.horizontal, 11)
        // Leave room for both toolbar actions even when the status is an error.
        .frame(maxWidth: 160, minHeight: 34, maxHeight: 34, alignment: .leading)
        .background(signalTint.opacity(0.11), in: Capsule())
        .overlay {
            Capsule()
                .stroke(signalTint.opacity(0.22), lineWidth: 1)
        }
        .contentShape(Capsule())
    }

    private var remoteConnectionPanel: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Text("远程桌面")
                    .font(.title3.weight(.bold))
                Spacer()
            }

            HStack(spacing: 10) {
                TextField("对端 ID", text: groupedRemoteID)
                    .keyboardType(.numberPad)
                    .textContentType(.username)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .font(.system(.title3, design: .rounded).weight(.semibold))
                    .monospacedDigit()
                    .focused($remoteIDFocused)
                    .padding(.horizontal, 15)
                    .frame(height: 54)
                    .background(Color(.systemGroupedBackground),
                                in: RoundedRectangle(cornerRadius: 12,
                                                     style: .continuous))
                    .overlay {
                        RoundedRectangle(cornerRadius: 12, style: .continuous)
                            .stroke(remoteIDFocused
                                    ? connectionAccent
                                    : Color(.separator).opacity(0.4),
                                    lineWidth: remoteIDFocused ? 2 : 1)
                    }
                    .shadow(color: remoteIDFocused
                            ? connectionAccent.opacity(0.12) : .clear,
                            radius: 8, y: 2)

                Button {
                    presentPasswordPrompt(for: trimmedRemoteID)
                } label: {
                    HStack(spacing: 6) {
                        Text("连接")
                        Image(systemName: "arrow.right")
                    }
                        .font(.system(size: 16, weight: .bold))
                        .frame(width: 96, height: 54)
                        .foregroundStyle(.white)
                        .background(LinearGradient(colors: [
                            connectionAccent,
                            connectionAccentEnd
                        ], startPoint: .topLeading, endPoint: .bottomTrailing))
                        .clipShape(RoundedRectangle(cornerRadius: 12,
                                                    style: .continuous))
                        .shadow(color: connectionAccent.opacity(0.24),
                                radius: 8, y: 4)
                }
                .buttonStyle(.plain)
                .disabled(trimmedRemoteID.isEmpty || session.isConnecting)
                .opacity(trimmedRemoteID.isEmpty || session.isConnecting ? 0.45 : 1)
                .accessibilityLabel("连接")
            }
        }
        .padding(18)
        .background(Color(.secondarySystemGroupedBackground),
                    in: RoundedRectangle(cornerRadius: 20, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 20, style: .continuous)
                .stroke(Color(.separator).opacity(0.28), lineWidth: 1)
        }
        .shadow(color: .black.opacity(0.055), radius: 14, y: 5)
    }

    @ViewBuilder
    private func recentConnectionsPanel(windowWidth: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .firstTextBaseline) {
                Text("最近连接")
                    .font(.title3.bold())
                Spacer()
                if !session.recentConnections.isEmpty {
                    Text("\(session.recentConnections.count) 台设备")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            if session.recentConnections.isEmpty {
                VStack(spacing: 10) {
                    Image(systemName: "clock.arrow.circlepath")
                        .font(.system(size: 30, weight: .medium))
                        .foregroundStyle(.tertiary)
                    Text("还没有连接记录")
                        .font(.headline)
                    Text("成功连接后，这里会显示远端桌面的缩略图。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView(.vertical) {
                    LazyVGrid(columns: recentConnectionColumns(windowWidth: windowWidth), spacing: 12) {
                        ForEach(orderedRecentConnections) { connection in
                            RecentConnectionCard(
                                connection: connection,
                                thumbnail: session.thumbnailImage(for: connection),
                                online: session.isRecentConnectionOnline(connection),
                                connect: {
                                    connectRecent(connection)
                                },
                                delete: {
                                    session.removeRecentConnection(connection)
                                }
                            )
                        }
                    }
                    .padding(.bottom, 2)
                }
            }
        }
        .padding(16)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(Color(.secondarySystemGroupedBackground),
                    in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .stroke(Color(.separator).opacity(0.35), lineWidth: 1)
        }
    }

    private var connectionProgressOverlay: some View {
        ZStack {
            Color.clear
                .ignoresSafeArea()
                .contentShape(Rectangle())
                .onTapGesture { }

            VStack(spacing: 0) {
                VStack(spacing: 14) {
                    ProgressView()
                        .controlSize(.large)
                        .tint(connectionAccent)

                    VStack(spacing: 6) {
                        Text("正在连接")
                            .font(.title3.weight(.semibold))
                            .foregroundStyle(.primary)
                        Text(session.connectionStatus)
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                            .lineLimit(2)
                            .frame(minHeight: 36)
                    }
                }
                .padding(.horizontal, 26)
                .padding(.top, 24)
                .padding(.bottom, 20)

                Divider()

                Button(role: .cancel, action: session.disconnect) {
                    Text("取消连接")
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(Color.red)
                        .frame(maxWidth: .infinity)
                        .frame(height: 46)
                }
                .buttonStyle(.plain)
            }
            .frame(width: 292)
            .background(Color.white,
                        in: RoundedRectangle(cornerRadius: 24,
                                             style: .continuous))
            .overlay {
                RoundedRectangle(cornerRadius: 24, style: .continuous)
                    .stroke(Color(.separator).opacity(0.24), lineWidth: 0.8)
            }
            .shadow(color: .black.opacity(0.22), radius: 28, y: 10)
        }
        .transition(.opacity.combined(with: .scale(scale: 0.96)))
    }

    private func presentPasswordPrompt(for identifier: String) {
        guard session.hasNetworkConsent else {
            remoteIDFocused = false
            session.showPrivacyNotice()
            return
        }
        let trimmed = identifier.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            remoteIDFocused = true
            return
        }
        remoteIDFocused = false
        promptRemoteID = trimmed
        promptPassword = session.savedPassword(for: trimmed)
        promptRememberPassword = session.remembersPassword(for: trimmed)
        passwordPromptVisible = true
    }

    private func formatRemoteID(_ value: String) -> String {
        let digits = Array(value.filter { $0.isNumber }.prefix(9))
        return stride(from: 0, to: digits.count, by: 3)
            .map { start in
                String(digits[start..<min(start + 3, digits.count)])
            }
            .joined(separator: " ")
    }

    private func connectRecent(_ connection: RecentConnection) {
        session.remoteID = connection.remoteID
        remoteIDFocused = false

        if connection.remembersPassword,
           let savedPassword = session.savedCredential(for: connection.remoteID) {
            session.connect(password: savedPassword, rememberPassword: true)
        } else {
            presentPasswordPrompt(for: connection.remoteID)
        }
    }
}

private struct RecentConnectionCard: View {
    let connection: RecentConnection
    let thumbnail: UIImage?
    let online: Bool
    let connect: () -> Void
    let delete: () -> Void

    private var platformColors: [Color] {
        switch connection.platform {
        case .windows:
            return [Color(red: 0.12, green: 0.38, blue: 0.70),
                    Color(red: 0.20, green: 0.48, blue: 0.77)]
        case .linux:
            return [Color(red: 0.63, green: 0.31, blue: 0.08),
                    Color(red: 0.73, green: 0.39, blue: 0.12)]
        case .macos:
            return [Color(red: 0.43, green: 0.29, blue: 0.68),
                    Color(red: 0.55, green: 0.39, blue: 0.77)]
        case .ios:
            return [Color(red: 0.08, green: 0.43, blue: 0.40),
                    Color(red: 0.12, green: 0.51, blue: 0.47)]
        case .unknown:
            return [Color(red: 0.35, green: 0.40, blue: 0.46),
                    Color(red: 0.45, green: 0.49, blue: 0.55)]
        }
    }

    var body: some View {
        Button(action: connect) {
            VStack(alignment: .leading, spacing: 0) {
                ZStack {
                    if let thumbnail {
                        Image(uiImage: thumbnail)
                            .resizable()
                            .scaledToFill()
                    } else {
                        LinearGradient(colors: platformColors,
                                       startPoint: .topLeading,
                                       endPoint: .bottomTrailing)
                        Text(connection.platform.displayName)
                            .font(.title3.weight(.semibold))
                            .lineLimit(1)
                            .minimumScaleFactor(0.8)
                            .foregroundStyle(.white)
                            .padding(8)
                            .accessibilityLabel("被控端平台：\(connection.platform.displayName)")
                    }
                }
                .frame(maxWidth: .infinity)
                .aspectRatio(16 / 9, contentMode: .fit)
                .clipped()

                VStack(alignment: .leading, spacing: 3) {
                    Text(connection.displayName)
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.primary)
                        .lineLimit(1)
                    HStack(spacing: 5) {
                        Text("ID \(connection.remoteID)")
                            .font(.caption2.monospacedDigit())
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                        Spacer(minLength: 4)
                        if connection.remembersPassword {
                            Image(systemName: "key.fill")
                                .font(.system(size: 9, weight: .semibold))
                                .foregroundStyle(.secondary)
                                .accessibilityLabel("已保存密码")
                        }
                        Text(online ? "在线" : "离线")
                            .font(.caption2.weight(.semibold))
                            .foregroundStyle(online ? Color.green : Color.secondary)
                            .accessibilityLabel(online ? "在线" : "离线")
                    }
                }
                .padding(9)
            }
            .background(Color(.secondarySystemGroupedBackground))
            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
            .overlay {
                RoundedRectangle(cornerRadius: 14, style: .continuous)
                    .stroke(Color(.separator).opacity(0.35), lineWidth: 1)
            }
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button(action: connect) {
                Label("连接", systemImage: "arrow.right.circle")
            }
            Button(role: .destructive, action: delete) {
                Label("删除记录", systemImage: "trash")
            }
        }
    }

}

private struct PasswordPromptView: View {
    let remoteID: String
    @Binding var password: String
    @Binding var rememberPassword: Bool
    let connect: () -> Void

    @Environment(\.dismiss) private var dismiss
    @FocusState private var passwordFocused: Bool
    @State private var passwordVisible = false

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("连接远程桌面")
                        .font(.title2.bold())
                    Text("对端 ID  \(remoteID)")
                        .font(.subheadline.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button {
                    dismiss()
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .font(.title2)
                        .foregroundStyle(.secondary)
                }
                .accessibilityLabel("关闭")
            }

            HStack(spacing: 10) {
                Group {
                    if passwordVisible {
                        TextField("访问密码", text: $password)
                    } else {
                        SecureField("访问密码", text: $password)
                    }
                }
                .keyboardType(.numberPad)
                .textContentType(.password)
                .focused($passwordFocused)

                Button {
                    passwordVisible.toggle()
                } label: {
                    Image(systemName: passwordVisible ? "eye.slash" : "eye")
                        .foregroundStyle(.secondary)
                }
                .accessibilityLabel(passwordVisible ? "隐藏密码" : "显示密码")
            }
            .padding(.horizontal, 13)
            .frame(height: 50)
            .background(Color(.secondarySystemGroupedBackground),
                        in: RoundedRectangle(cornerRadius: 10,
                                             style: .continuous))
            .overlay {
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .stroke(Color(.separator).opacity(0.5), lineWidth: 1)
            }

            Toggle(isOn: $rememberPassword) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("保存密码")
                        .font(.subheadline.weight(.semibold))
                    Text("密码将安全保存在本机 Keychain 中")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Button(action: connect) {
                Label("连接", systemImage: "arrow.right")
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .frame(height: 48)
            }
            .buttonStyle(.borderedProminent)
            .buttonBorderShape(.roundedRectangle(radius: 10))
        }
        .padding(22)
        .onAppear {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                passwordFocused = true
            }
        }
    }
}

private struct ServerSettingsView: View {
    @EnvironmentObject private var appUpdates: AppUpdateChecker
    @ObservedObject var session: RemoteSessionModel
    @State private var serverConfigurationExpanded = false
    @State private var usesCustomServer: Bool
    @State private var customServerHost: String
    @State private var customServerPort: String
    @State private var serverConfigurationError: String?
    @FocusState private var focusedServerField: ServerField?

    private enum ServerField: Hashable {
        case host, port
    }

    init(session: RemoteSessionModel) {
        self.session = session
        let isCustom = !session.usesOfficialServer
        _usesCustomServer = State(initialValue: isCustom)
        _customServerHost = State(initialValue: isCustom ? session.signalHost : "")
        _customServerPort = State(initialValue: isCustom ? session.signalPort : "")
    }

    var body: some View {
        Form {
            Section("鼠标控制") {
                Picker("控制模式", selection: $session.mouseControlMode) {
                    ForEach(MouseControlMode.allCases) { mode in
                        Text(mode.title).tag(mode)
                    }
                }
                .pickerStyle(.segmented)
            }
            Section("画面偏好") {
                Picker("偏好模式", selection: $session.videoAdaptationPolicy) {
                    ForEach(VideoAdaptationPolicy.allCases) { policy in
                        Text(policy.title).tag(policy)
                    }
                }
                .pickerStyle(.segmented)
            }
            Section {
                DisclosureGroup(isExpanded: $serverConfigurationExpanded) {
                    Toggle("使用自定义服务器", isOn: $usesCustomServer)
                    if usesCustomServer {
                        LabeledContent("地址") {
                            TextField("填写服务器地址", text: $customServerHost)
                                .textInputAutocapitalization(.never)
                                .autocorrectionDisabled()
                                .keyboardType(.URL)
                                .multilineTextAlignment(.trailing)
                                .focused($focusedServerField, equals: .host)
                                .submitLabel(.next)
                                .onSubmit { focusedServerField = .port }
                                .accessibilityLabel("服务器地址")
                        }
                        LabeledContent("端口") {
                            TextField("填写端口", text: $customServerPort)
                                .keyboardType(.numberPad)
                                .multilineTextAlignment(.trailing)
                                .focused($focusedServerField, equals: .port)
                                .accessibilityLabel("服务器端口")
                        }
                    }
                } label: {
                    HStack {
                        Text("服务器")
                        Spacer()
                        Text(usesCustomServer ? "自定义" : "默认")
                            .foregroundStyle(.secondary)
                    }
                }
            } footer: {
                if let serverConfigurationError {
                    Text(serverConfigurationError)
                        .foregroundStyle(.red)
                }
                if let warning = session.identityStorageWarning {
                    Text(warning)
                        .foregroundStyle(.orange)
                }
                Text(!session.hasNetworkConsent ? "完成隐私授权后登记本机身份" : session.localIdentity.isEmpty
                     ? "正在获取本机 ID…"
                     : "本机 ID  \(session.localIdentity)")
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .center)
                    .padding(.top, 16)
                    .textSelection(.enabled)
            }
            PrivacySettingsSection(session: session)
            Section {
                NavigationLink {
                    ThirdPartyLicensesView(session: session)
                } label: {
                    HStack {
                        Text("关于")
                        Spacer()
                        if appUpdates.updateAvailable {
                            Text("新版本").foregroundStyle(.secondary)
                            UpdateDot()
                        }
                    }
                }
                .accessibilityLabel(appUpdates.updateAvailable ? "关于，有新版本" : "关于")
            }
        }
        .readableContentWidth()
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
        .navigationTitle("设置")
        .navigationBarTitleDisplayMode(.inline)
        .scrollDismissesKeyboard(.interactively)
        .background {
            KeyboardDismissTapView {
                focusedServerField = nil
            }
            .allowsHitTesting(false)
        }
        .onChange(of: serverConfigurationExpanded) { expanded in
            if !expanded {
                focusedServerField = nil
                validateAndApplyServerConfiguration()
            }
        }
        .onChange(of: usesCustomServer) { isCustom in
            if !isCustom { useDefaultServer() }
        }
        .onChange(of: focusedServerField) { field in
            if field == nil { validateAndApplyServerConfiguration() }
        }
        .onDisappear {
            // Native back navigation can remove the view before focus updates.
            focusedServerField = nil
            validateAndApplyServerConfiguration()
        }
        .toolbar {
            ToolbarItemGroup(placement: .keyboard) {
                Spacer()
                Button("完成") {
                    focusedServerField = nil
                }
            }
        }
    }

    private func useDefaultServer() {
        serverConfigurationError = nil
        focusedServerField = nil
        customServerHost = ""
        customServerPort = ""
        guard !session.usesOfficialServer else { return }
        session.restoreOfficialServerConfiguration()
        session.configureBridge()
    }

    private func validateAndApplyServerConfiguration() {
        guard usesCustomServer else { return }
        let host = customServerHost.trimmingCharacters(in: .whitespacesAndNewlines)
        let port = customServerPort.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !host.isEmpty else {
            serverConfigurationError = "请输入服务器地址，或关闭“使用自定义服务器”。"
            return
        }
        guard let portNumber = Int(port), (1...65535).contains(portNumber) else {
            serverConfigurationError = "请输入 1–65535 范围内的端口，或关闭“使用自定义服务器”。"
            return
        }
        serverConfigurationError = nil
        customServerHost = host
        customServerPort = String(portNumber)
        if session.signalHost != host || session.signalPort != customServerPort {
            session.signalHost = host
            session.signalPort = customServerPort
            session.configureBridge()
        }
    }
}
