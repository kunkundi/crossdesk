import SwiftUI
import UIKit

enum MobileLayout {
    static let twoColumnBreakpoint: CGFloat = 900
    static let homeMaximumWidth: CGFloat = 1200
    static let readableMaximumWidth: CGFloat = 720
}

extension View {
    // Keep change callbacks available on iOS 16 without using the deprecated
    // overload on newer systems. Neither branch fires on initial appearance.
    @ViewBuilder
    func onValueChange<Value: Equatable>(of value: Value,
                                         perform action: @escaping (Value) -> Void) -> some View {
        if #available(iOS 17.0, *) {
            onChange(of: value) { _, newValue in action(newValue) }
        } else {
            onChange(of: value, perform: action)
        }
    }

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
        .alert(NSLocalizedString("连接提示", comment: ""), isPresented: Binding(
            get: { session.connectionFailureMessage != nil },
            set: { if !$0 { session.dismissConnectionFailure() } }
        )) {
            if let change = session.hostKeyChange {
                Button(NSLocalizedString("连接新设备", comment: "")) {
                    session.requestedPasswordPrompt = .init(target: change.target, expectedKey: change.observed)
                }
                Button(NSLocalizedString("复制指纹", comment: "")) { UIPasteboard.general.string = change.expected + "\n" + change.observed }
            }
            if let retry = session.rejectedPasswordPrompt {
                Button(NSLocalizedString("重试", comment: "")) { session.requestedPasswordPrompt = retry }
            }
            Button(NSLocalizedString("确定", comment: ""), role: .cancel) { session.dismissConnectionFailure() }
        } message: {
            Text(session.connectionFailureMessage ?? "")
        }
        .onAppear {
            if !session.sessionVisible {
                AppOrientation.endSession()
            }
            if scenePhase == .active {
                session.applicationDidBecomeActive()
            }
        }
        .onValueChange(of: scenePhase) { phase in
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
    @State private var replacementKey: String?
    @State private var promptPassword = ""
    @State private var promptRememberPassword = false
    @State private var groupFilter: String?
    @State private var groupDevice: RecentConnection?
    @State private var groupToDelete: (name: String, history: ConnectionHistory)?

    private let connectionAccent = Color(red: 0.18, green: 0.48, blue: 0.86)
    private let connectionAccentEnd = Color(red: 0.12, green: 0.38, blue: 0.78)

    private var trimmedRemoteID: String {
        session.remoteID.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private var groupedRemoteID: Binding<String> {
        Binding(
            get: { formatRemoteID(session.remoteID) },
            set: { value in
                session.remoteID = value.allSatisfy { $0.isNumber || $0 == " " } ? value.replacingOccurrences(of: " ", with: "") : value
            }
        )
    }

    private var signalIsConnected: Bool { session.signalConnected }
    private var signalHasError: Bool { session.signalHasError }

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

    private var recentGroups: [String] {
        Set(session.recentConnections.flatMap(\.groups)).sorted()
    }

    private var orderedRecentConnections: [RecentConnection] {
        session.recentConnections.enumerated()
            .filter { entry in
                guard let groupFilter else { return true }
                return groupFilter.isEmpty ? entry.element.groups.isEmpty : entry.element.groups.contains(groupFilter)
            }
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
                            .id(session.history.key)
                            .alert(NSLocalizedString("删除分组", comment: ""),
                                   isPresented: Binding(get: { groupToDelete != nil },
                                                        set: { if !$0 { groupToDelete = nil } }),
                                   presenting: groupToDelete) { group in
                                Button(NSLocalizedString("删除", comment: ""), role: .destructive) {
                                    session.deleteGroup(group.name, in: group.history)
                                }
                                Button(NSLocalizedString("取消", comment: ""), role: .cancel) {}
                            } message: { group in
                                Text(verbatim: group.name + "\n" + NSLocalizedString("组内设备和连接记录将保留。", comment: ""))
                            }
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
                guard passwordPromptVisible, session.canStartConnection else { return }
                passwordPromptVisible = false
                session.remoteID = promptRemoteID
                session.connect(password: promptPassword,
                                rememberPassword: promptRememberPassword, replacement: replacementKey)
            }
            .presentationDetents([.height(340)])
            .presentationDragIndicator(.visible)
        }
        .onValueChange(of: session.requestedPasswordPrompt) { prompt in
            guard let prompt else { return }
            session.requestedPasswordPrompt = nil
            presentPasswordPrompt(for: prompt.target, replacement: prompt.expectedKey)
        }
        .sheet(item: $groupDevice) { device in
            RecentGroupEditor(device: device, groups: recentGroups) { groups in
                session.editRecentConnection(device.id, groups: groups)
            }
            .presentationDetents([.medium, .large])
        }
        .onValueChange(of: session.history) { _ in
            passwordPromptVisible = false
            promptRemoteID = ""
            promptPassword = ""
            promptRememberPassword = false
            groupFilter = nil
            groupDevice = nil
            groupToDelete = nil
        }
        .onValueChange(of: recentGroups) { groups in
            if let groupFilter, !groupFilter.isEmpty, !groups.contains(groupFilter) {
                self.groupFilter = nil
            }
        }
        .alert(NSLocalizedString("设备离线", comment: ""), isPresented: Binding(
            get: { session.deviceOfflineAlertVisible },
            set: { visible in
                if !visible { session.dismissDeviceOfflineAlert() }
            }
        )) {
            Button(NSLocalizedString("确定", comment: "")) {
                session.dismissDeviceOfflineAlert()
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
                .accessibilityHint(NSLocalizedString("打开隐私政策", comment: ""))
            }
        }
        ToolbarItem(placement: announcementPlacement) {
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
            .accessibilityLabel(appUpdates.updateAvailable ? NSLocalizedString("设置，有新版本", comment: "") : NSLocalizedString("设置", comment: ""))
        }
    }

    private var announcementPlacement: ToolbarItemPlacement {
        if #available(iOS 27.0, *) { return .topBarPinnedTrailing }
        return .navigationBarTrailing
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
                Text(NSLocalizedString("远程桌面", comment: ""))
                    .font(.title3.weight(.bold))
                Spacer()
            }

            HStack(spacing: 10) {
                TextField(NSLocalizedString("设备 ID、IP 或域名", comment: ""), text: groupedRemoteID)
                    .keyboardType(.asciiCapable)
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
                    Text(NSLocalizedString("连接", comment: ""))
                        .font(.system(size: 16, weight: .bold))
                        .lineLimit(1)
                        .minimumScaleFactor(0.65)
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
                .accessibilityLabel(NSLocalizedString("连接", comment: ""))
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

    private var recentGroupPicker: some View {
        let all = NSLocalizedString("全部设备", comment: "")
        let ungrouped = NSLocalizedString("未分组", comment: "")
        let selected = groupFilter.map { $0.isEmpty ? ungrouped : $0 } ?? all
        return Menu {
            Picker(NSLocalizedString("分组", comment: ""), selection: $groupFilter) {
                Text(all).tag(nil as String?)
                Text(ungrouped).tag(Optional(""))
                ForEach(recentGroups, id: \.self) { group in
                    Text(verbatim: group).tag(Optional(group))
                }
            }
            .pickerStyle(.inline)
            if !recentGroups.isEmpty {
                Divider()
                Menu {
                    ForEach(recentGroups, id: \.self) { name in
                        Button { groupToDelete = (name, session.history) } label: { Text(verbatim: name) }
                    }
                } label: {
                    Label(NSLocalizedString("删除分组", comment: ""), systemImage: "trash")
                }
            }
        } label: {
            HStack(spacing: 4) {
                Text(verbatim: selected).lineLimit(1)
                Image(systemName: "chevron.up.chevron.down")
                    .font(.caption2.weight(.semibold))
            }
            .font(.subheadline)
            .foregroundStyle(.tint)
            .frame(minHeight: 44)
            .contentShape(Rectangle())
        }
        .accessibilityLabel(NSLocalizedString("分组", comment: ""))
        .accessibilityValue(Text(verbatim: selected))
    }

    @ViewBuilder
    private func recentConnectionsPanel(windowWidth: CGFloat) -> some View {
        let connections = orderedRecentConnections
        let title = Text(NSLocalizedString("最近连接", comment: "")).font(.title3.bold())
        VStack(alignment: .leading, spacing: 14) {
            if session.recentConnections.isEmpty {
                title
            } else {
                let count = Text(String(format: NSLocalizedString("%1$@ 台设备", comment: ""), String(connections.count)))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                ViewThatFits(in: .horizontal) {
                    HStack(alignment: .firstTextBaseline) {
                        title
                        Spacer(minLength: 8)
                        count
                        recentGroupPicker
                    }
                    VStack(alignment: .trailing, spacing: 4) {
                        HStack(alignment: .firstTextBaseline) {
                            title
                            Spacer(minLength: 8)
                            count
                        }
                        recentGroupPicker
                    }
                }
            }

            if session.recentConnections.isEmpty {
                VStack(spacing: 10) {
                    Image(systemName: "clock.arrow.circlepath")
                        .font(.system(size: 30, weight: .medium))
                        .foregroundStyle(.tertiary)
                    Text(NSLocalizedString("还没有连接记录", comment: ""))
                        .font(.headline)
                    Text(NSLocalizedString("成功连接后，这里会显示远端桌面的缩略图。", comment: ""))
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if connections.isEmpty {
                Text(NSLocalizedString("该分组暂无近期连接", comment: ""))
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView(.vertical) {
                    LazyVGrid(columns: recentConnectionColumns(windowWidth: windowWidth), spacing: 12) {
                        ForEach(connections) { connection in
                            RecentConnectionCard(
                                connection: connection,
                                thumbnail: session.thumbnailImage(for: connection),
                                online: session.isRecentConnectionOnline(connection),
                                connect: {
                                    connectRecent(connection)
                                },
                                rename: { session.editRecentConnection(connection.id, alias: $0) },
                                editGroups: { groupDevice = connection },
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
                        Text(NSLocalizedString("正在连接", comment: ""))
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
                    Text(NSLocalizedString("取消连接", comment: ""))
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

    private func presentPasswordPrompt(for identifier: String, replacement: String? = nil) {
        guard !passwordPromptVisible, session.canStartConnection else { return }
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
        guard let target = session.connectionTarget(trimmed) else { return }
        if replacement == nil, CrossDeskRTCBridge.isDomainEndpoint(target),
           let saved = session.savedCredential(for: target) {
            session.remoteID = target
            session.connect(password: saved, rememberPassword: true, saved: true)
            return
        }
        replacementKey = replacement
        promptRemoteID = target
        promptPassword = CrossDeskRTCBridge.isDomainEndpoint(target) ? "" : session.savedPassword(for: target)
        promptRememberPassword = session.remembersPassword(for: target)
        passwordPromptVisible = true
    }

    private func formatRemoteID(_ value: String) -> String {
        guard value.count <= 9, value.allSatisfy({ $0.isNumber }) else { return value }
        let digits = Array(value)
        return stride(from: 0, to: digits.count, by: 3)
            .map { start in
                String(digits[start..<min(start + 3, digits.count)])
            }
            .joined(separator: " ")
    }

    private func connectRecent(_ connection: RecentConnection) {
        guard !passwordPromptVisible, session.canStartConnection else { return }
        session.remoteID = connection.remoteID
        remoteIDFocused = false

        if connection.remembersPassword,
           let savedPassword = session.savedCredential(for: connection.remoteID) {
            session.connect(password: savedPassword, rememberPassword: true, saved: true)
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
    let rename: (String) -> Void
    let editGroups: () -> Void
    let delete: () -> Void
    @State private var renaming = false
    @State private var nameDraft = ""

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
                            .accessibilityLabel(String(format: NSLocalizedString("被控端平台：%1$@", comment: ""), String(describing: connection.platform.displayName)))
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
                    if !connection.groups.isEmpty {
                        Text(connection.groups.joined(separator: " · "))
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                    HStack(spacing: 5) {
                        Text(connection.remoteID.contains(":") ? connection.remoteID : "ID \(connection.remoteID)")
                            .font(.caption2.monospacedDigit())
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                        Spacer(minLength: 4)
                        if connection.remembersPassword {
                            Image(systemName: "key.fill")
                                .font(.system(size: 9, weight: .semibold))
                                .foregroundStyle(.secondary)
                                .accessibilityLabel(NSLocalizedString("已保存密码", comment: ""))
                        }
                        Text(connection.remoteID.contains(":") ? "IP" : online ? NSLocalizedString("在线", comment: "") : NSLocalizedString("离线", comment: ""))
                            .font(.caption2.weight(.semibold))
                            .foregroundStyle(online ? Color.green : Color.secondary)
                            .accessibilityLabel(connection.remoteID.contains(":") ? "IP" : online ? NSLocalizedString("在线", comment: "") : NSLocalizedString("离线", comment: ""))
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
        .contextMenu { actions }
        .overlay(alignment: .topTrailing) {
            Menu { actions } label: {
                Image(systemName: "ellipsis")
                    .font(.footnote.weight(.semibold))
                    .foregroundStyle(Color(uiColor: .label))
                    .padding(.horizontal, 6)
                    .frame(minWidth: 26, minHeight: 17)
                    .background(Color(uiColor: .secondarySystemBackground), in: Capsule())
                    .overlay {
                        Capsule().strokeBorder(Color(uiColor: .separator), lineWidth: 0.75)
                    }
                    .frame(width: 44, height: 44)
                    .contentShape(Rectangle())
            }
            .accessibilityLabel(NSLocalizedString("更多操作", comment: ""))
        }
        .alert(NSLocalizedString("修改名称", comment: ""), isPresented: $renaming) {
            TextField(connection.hostName, text: $nameDraft)
            Button(NSLocalizedString("取消", comment: ""), role: .cancel) {}
            Button(NSLocalizedString("保存", comment: "")) { rename(nameDraft) }
        } message: {
            Text(connection.remoteID.contains(":") ? connection.remoteID : "ID \(connection.remoteID)")
        }
    }

    @ViewBuilder private var actions: some View {
        Button(action: connect) { Label(NSLocalizedString("连接", comment: ""), systemImage: "arrow.right") }
        Button {
            nameDraft = connection.displayName
            renaming = true
        } label: { Label(NSLocalizedString("修改名称", comment: ""), systemImage: "pencil") }
        Button(action: editGroups) { Label(NSLocalizedString("修改分组", comment: ""), systemImage: "folder") }
        Button(role: .destructive, action: delete) {
            Label(NSLocalizedString("删除记录", comment: ""), systemImage: "trash")
        }
    }
}

private struct RecentGroupEditor: View {
    let device: RecentConnection
    let groups: [String]
    let save: ([String]) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var selected: Set<String>
    @State private var newGroup = ""

    init(device: RecentConnection, groups: [String], save: @escaping ([String]) -> Void) {
        self.device = device
        self.groups = groups
        self.save = save
        _selected = State(initialValue: Set(device.groups))
    }

    private var trimmedGroup: String { newGroup.trimmingCharacters(in: .whitespacesAndNewlines) }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    ForEach([""] + groups, id: \.self) { group in
                        let checked = group.isEmpty ? selected.isEmpty && trimmedGroup.isEmpty : selected.contains(group)
                        Button {
                            if group.isEmpty { selected.removeAll(); newGroup = "" }
                            else if selected.contains(group) { selected.remove(group) }
                            else { selected.insert(group) }
                        } label: {
                            HStack {
                                Text(group.isEmpty ? NSLocalizedString("未分组", comment: "") : group)
                                    .foregroundStyle(.primary)
                                Spacer()
                                if checked { Image(systemName: "checkmark") }
                            }
                        }
                        .accessibilityAddTraits(checked ? .isSelected : [])
                    }
                    TextField(NSLocalizedString("新建分组（可选）", comment: ""), text: $newGroup)
                } header: {
                    Text("\(device.displayName) · ID \(device.id)").textCase(nil)
                }
            }
            .navigationTitle(NSLocalizedString("修改分组", comment: ""))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(NSLocalizedString("取消", comment: "")) { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(NSLocalizedString("保存", comment: "")) {
                        if !trimmedGroup.isEmpty { selected.insert(trimmedGroup) }
                        save(Array(selected))
                        dismiss()
                    }
                }
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
                    Text(NSLocalizedString("连接远程桌面", comment: ""))
                        .font(.title2.bold())
                    Text(remoteID.contains(":") ? remoteID : String(format: NSLocalizedString("对端 ID  %1$@", comment: ""), remoteID))
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
                .accessibilityLabel(NSLocalizedString("关闭", comment: ""))
            }

            HStack(spacing: 10) {
                Group {
                    if passwordVisible {
                        TextField(NSLocalizedString(remoteID.contains(":") ? "IP 直连密码" : "访问密码", comment: ""), text: $password)
                    } else {
                        SecureField(NSLocalizedString(remoteID.contains(":") ? "IP 直连密码" : "访问密码", comment: ""), text: $password)
                    }
                }
                .keyboardType(.asciiCapable)
                .textContentType(.password)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .focused($passwordFocused)

                Button {
                    passwordVisible.toggle()
                } label: {
                    Image(systemName: passwordVisible ? "eye.slash" : "eye")
                        .foregroundStyle(.secondary)
                }
                .accessibilityLabel(passwordVisible ? NSLocalizedString("隐藏密码", comment: "") : NSLocalizedString("显示密码", comment: ""))
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
                    Text(NSLocalizedString("保存密码", comment: ""))
                        .font(.subheadline.weight(.semibold))
                    Text(NSLocalizedString("密码将安全保存在本机 Keychain 中", comment: ""))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Button(action: connect) {
                Text(NSLocalizedString("连接", comment: ""))
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .frame(height: 48)
            }
            .disabled(remoteID.contains(":") && password.range(of: "^[A-Za-z0-9]{6}$", options: .regularExpression) == nil)
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

private struct DevicePairingView: View {
    let deviceID: String
    @State private var info: [String: String]?
    @State private var copied = false

    var body: some View {
        Form {
            Section {
                Text(NSLocalizedString("将配对信息添加到被控端的可信设备列表，并核对指纹。", comment: ""))
                if let info, let fingerprint = info["fingerprint"], let pairing = info["pairing"] {
                    Text(fingerprint.enumerated().map { index, character in
                        (index > 0 && index % 4 == 0 ? " " : "") + String(character)
                    }.joined()).font(.footnote.monospaced()).textSelection(.enabled)
                    Button(NSLocalizedString("复制配对信息", comment: "")) {
                        UIPasteboard.general.setItems([["public.utf8-plain-text": pairing]], options: [.localOnly: true])
                        copied = true
                    }
                    if copied { Text(NSLocalizedString("配对信息已复制。", comment: "")).foregroundStyle(.secondary) }
                } else {
                    Text(NSLocalizedString("安全存储不可用，请解锁设备后重试", comment: "")).foregroundStyle(.orange)
                    Button(NSLocalizedString("重试", comment: "")) { load() }
                }
            }
        }
        .navigationTitle(NSLocalizedString("本机配对信息", comment: ""))
        .navigationBarTitleDisplayMode(.inline)
        .task { load() }
    }
    private func load() { info = CrossDeskRTCBridge.deviceTrustInfo(forDeviceID: deviceID); copied = false }
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
        let server = session.customServer
        _usesCustomServer = State(initialValue: server != nil)
        _customServerHost = State(initialValue: server?.host ?? "")
        _customServerPort = State(initialValue: server.map { String($0.signalPort) } ?? "")
    }

    var body: some View {
        Form {
            Section(NSLocalizedString("鼠标控制", comment: "")) {
                Picker(NSLocalizedString("控制模式", comment: ""), selection: $session.mouseControlMode) {
                    ForEach(MouseControlMode.allCases) { mode in
                        Text(mode.title).tag(mode)
                    }
                }
                .pickerStyle(.segmented)
            }
            Section(NSLocalizedString("画面偏好", comment: "")) {
                Picker(NSLocalizedString("偏好模式", comment: ""), selection: $session.videoAdaptationPolicy) {
                    ForEach(VideoAdaptationPolicy.allCases) { policy in
                        Text(policy.title).tag(policy)
                    }
                }
                .pickerStyle(.segmented)
            }
            Section {
                Toggle(NSLocalizedString("连接建立后锁定横屏", comment: ""), isOn: $session.autoLandscape)
            } header: {
                Text(NSLocalizedString("屏幕方向", comment: ""))
            } footer: {
                Text(NSLocalizedString("默认关闭。开启后，iPhone 连接时进入并保持横屏；关闭时跟随设备方向。iPad 不受影响。", comment: ""))
            }
            Section {
                DisclosureGroup(isExpanded: $serverConfigurationExpanded) {
                    Toggle(NSLocalizedString("使用自定义服务器", comment: ""), isOn: $usesCustomServer)
                    if usesCustomServer {
                        LabeledContent(NSLocalizedString("地址", comment: "")) {
                            TextField(NSLocalizedString("填写服务器地址", comment: ""), text: $customServerHost)
                                .textInputAutocapitalization(.never)
                                .autocorrectionDisabled()
                                .keyboardType(.URL)
                                .multilineTextAlignment(.trailing)
                                .focused($focusedServerField, equals: .host)
                                .submitLabel(.next)
                                .onSubmit { focusedServerField = .port }
                                .accessibilityLabel(NSLocalizedString("服务器地址", comment: ""))
                        }
                        LabeledContent(NSLocalizedString("端口", comment: "")) {
                            TextField(NSLocalizedString("填写端口", comment: ""), text: $customServerPort)
                                .keyboardType(.numberPad)
                                .multilineTextAlignment(.trailing)
                                .focused($focusedServerField, equals: .port)
                                .accessibilityLabel(NSLocalizedString("服务器端口", comment: ""))
                        }
                    }
                } label: {
                    HStack {
                        Text(NSLocalizedString("服务器", comment: ""))
                        Spacer()
                        Text(usesCustomServer ? NSLocalizedString("自定义", comment: "") : NSLocalizedString("默认", comment: ""))
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
                Text(!session.hasNetworkConsent ? NSLocalizedString("完成隐私授权后登记本机身份", comment: "") : session.localIdentity.isEmpty
                     ? NSLocalizedString("正在获取本机 ID…", comment: "")
                     : String(format: NSLocalizedString("本机 ID  %1$@", comment: ""), String(describing: session.localIdentity)))
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .center)
                    .padding(.top, 16)
                    .textSelection(.enabled)
            }
            PrivacySettingsSection(session: session)
            Section {
                NavigationLink(NSLocalizedString("本机配对信息", comment: "")) {
                    DevicePairingView(deviceID: session.localIdentity)
                }
            }
            Section {
                NavigationLink {
                    ThirdPartyLicensesView(session: session)
                } label: {
                    HStack {
                        Text(NSLocalizedString("关于", comment: ""))
                        Spacer()
                        if appUpdates.updateAvailable {
                            Text(NSLocalizedString("新版本", comment: "")).foregroundStyle(.secondary)
                            UpdateDot()
                        }
                    }
                }
                .accessibilityLabel(appUpdates.updateAvailable ? NSLocalizedString("关于，有新版本", comment: "") : NSLocalizedString("关于", comment: ""))
            }
        }
        .readableContentWidth()
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
        .navigationTitle(NSLocalizedString("设置", comment: ""))
        .navigationBarTitleDisplayMode(.inline)
        .scrollDismissesKeyboard(.interactively)
        .background {
            KeyboardDismissTapView {
                focusedServerField = nil
            }
            .allowsHitTesting(false)
        }
        .onValueChange(of: serverConfigurationExpanded) { expanded in
            if !expanded {
                focusedServerField = nil
                validateAndApplyServerConfiguration()
            }
        }
        .onValueChange(of: usesCustomServer) { isCustom in
            if !isCustom { useDefaultServer() }
        }
        .onValueChange(of: focusedServerField) { field in
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
                Button(NSLocalizedString("完成", comment: "")) {
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
        session.setCustomServer(nil)
    }

    private func validateAndApplyServerConfiguration() {
        guard usesCustomServer else { return }
        let host = customServerHost.trimmingCharacters(in: .whitespacesAndNewlines)
        let port = customServerPort.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !host.isEmpty else {
            serverConfigurationError = NSLocalizedString("请输入服务器地址，或关闭“使用自定义服务器”。", comment: "")
            return
        }
        guard let portNumber = Int(port), (1...65535).contains(portNumber) else {
            serverConfigurationError = NSLocalizedString("请输入 1–65535 范围内的端口，或关闭“使用自定义服务器”。", comment: "")
            return
        }
        serverConfigurationError = nil
        customServerHost = host
        customServerPort = String(portNumber)
        session.setCustomServer(SignalServerConfiguration(host: host, signalPort: portNumber))
    }
}
