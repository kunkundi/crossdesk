import SwiftUI
import UIKit

struct ContentView: View {
    @Environment(\.scenePhase) private var scenePhase
    @ObservedObject var session: RemoteSessionModel

    var body: some View {
        Group {
            if session.sessionVisible {
                RemoteSessionView(session: session)
                    .transition(.opacity)
            } else {
                ConnectionHomeView(session: session)
                    .transition(.opacity)
            }
        }
        .animation(.easeInOut(duration: 0.2), value: session.sessionVisible)
        .preferredColorScheme(session.sessionVisible ? .dark : .light)
        .onAppear {
            if !session.sessionVisible {
                AppOrientation.update(to: .portrait)
            }
            if scenePhase == .active {
                session.refreshRecentConnectionPresenceAfterForeground()
            }
        }
        .onChange(of: scenePhase) { phase in
            if phase == .active {
                session.refreshRecentConnectionPresenceAfterForeground()
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
    @ObservedObject var session: RemoteSessionModel
    @FocusState private var remoteIDFocused: Bool
    @State private var passwordPromptVisible = false
    @State private var settingsVisible = false
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

    private var recentConnectionColumns: [GridItem] {
        [
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

            VStack(spacing: 0) {
                header
                remoteConnectionPanel
                    .padding(.top, 24)
                recentConnectionsPanel
                    .padding(.top, 20)
                    .frame(maxHeight: .infinity)
            }
            .padding(.horizontal, 20)
            .padding(.bottom, 12)

            if session.isConnecting {
                connectionProgressOverlay
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
        .sheet(isPresented: $settingsVisible) {
            ServerSettingsView(session: session)
                .presentationDetents([.fraction(0.80)])
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
    }

    private var header: some View {
        HStack(spacing: 8) {
            Label {
                Text(session.signalStatus)
                    .font(.caption.weight(.semibold))
                    .lineLimit(1)
            } icon: {
                Image(systemName: signalSymbol)
                    .font(.caption.weight(.bold))
            }
            .foregroundStyle(signalTint)
            .padding(.horizontal, 11)
            .frame(height: 34)
            .background(signalTint.opacity(0.11), in: Capsule())
            .overlay {
                Capsule()
                    .stroke(signalTint.opacity(0.22), lineWidth: 1)
            }

            Spacer()

            Button {
                settingsVisible = true
            } label: {
                Image(systemName: "gearshape")
                    .font(.system(size: 18, weight: .semibold))
                    .frame(width: 40, height: 40)
                    .background(Color(.secondarySystemGroupedBackground),
                                in: RoundedRectangle(cornerRadius: 10,
                                                     style: .continuous))
            }
            .foregroundStyle(.primary)
            .accessibilityLabel("设置")
        }
        .padding(.top, 8)
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
    private var recentConnectionsPanel: some View {
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
                    LazyVGrid(columns: recentConnectionColumns, spacing: 12) {
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

    var body: some View {
        Button(action: connect) {
            VStack(alignment: .leading, spacing: 0) {
                ZStack {
                    if let thumbnail {
                        Image(uiImage: thumbnail)
                            .resizable()
                            .scaledToFill()
                    } else {
                        LinearGradient(colors: [
                            Color(red: 0.18, green: 0.48, blue: 0.86),
                            Color(red: 0.38, green: 0.68, blue: 0.96)
                        ], startPoint: .topLeading, endPoint: .bottomTrailing)
                        Image(systemName: "display")
                            .font(.system(size: 34, weight: .medium))
                            .foregroundStyle(.white.opacity(0.9))
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
    @ObservedObject var session: RemoteSessionModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                Section("鼠标控制") {
                    Picker("控制模式", selection: $session.mouseControlMode) {
                        ForEach(MouseControlMode.allCases) { mode in
                            Text(mode.title).tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)

                    Text(session.mouseControlMode.detail)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
                Section("视频编解码") {
                    Picker("处理方式", selection: $session.videoCodecMode) {
                        ForEach(VideoCodecMode.allCases) { mode in
                            Text(mode.title).tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)

                    Text(session.videoCodecMode.detail)
                        .font(.footnote)
                        .foregroundStyle(.secondary)

                    Text("修改后从下一次连接开始生效。")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
                Section("服务器") {
                    TextField("信令服务器", text: $session.signalHost)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    TextField("信令端口", text: $session.signalPort)
                        .keyboardType(.numberPad)
                    TextField("STUN/TURN 端口", text: $session.turnPort)
                        .keyboardType(.numberPad)
                }
                Section {
                    Toggle("启用 SRTP", isOn: $session.enableSRTP)
                } header: {
                    Text("传输")
                } footer: {
                    Text(session.localIdentity.isEmpty
                         ? "正在获取本机 ID…"
                         : "本机 ID  \(session.localIdentity)")
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .center)
                        .padding(.top, 16)
                        .textSelection(.enabled)
                }
            }
            .navigationTitle("设置")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("应用") {
                        session.configureBridge()
                        dismiss()
                    }
                }
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
            }
        }
    }
}
