import SwiftUI

struct PrivacyConsentView: View {
    @ObservedObject var session: RemoteSessionModel
    @State private var hasReadPolicy = false

    var body: some View {
        NavigationStack {
            ScrollView {
                PrivacyPolicyDocument()
                    .padding(.horizontal, 20)
                    .padding(.vertical, 20)
            }
            .background(Color(.systemBackground))
            .safeAreaInset(edge: .bottom, spacing: 0) {
                consentActions
            }
            .navigationTitle("隐私政策")
            .navigationBarTitleDisplayMode(.inline)
        }
    }

    private var consentActions: some View {
        VStack(spacing: 0) {
            Divider()

            VStack(spacing: 12) {
                Toggle(isOn: $hasReadPolicy) {
                    Text("我已阅读并同意《隐私政策》")
                        .font(.subheadline)
                }
                .toggleStyle(ConsentCheckboxStyle())

                Button {
                    guard hasReadPolicy else { return }
                    session.acceptNetworkConsent()
                } label: {
                    Text("同意并继续")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity, minHeight: 24)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .tint(.blue)
                .disabled(!hasReadPolicy || !PrivacyPolicyDocument.isAvailable)

                Button {
                    session.declineNetworkConsent()
                } label: {
                    Text("暂不同意")
                        .font(.body)
                        .frame(maxWidth: .infinity, minHeight: 44)
                        .contentShape(Rectangle())
                }
                .foregroundStyle(.secondary)
                .buttonStyle(.plain)
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 12)
        }
        .background(Color(.systemBackground))
    }
}

private struct ConsentCheckboxStyle: ToggleStyle {
    func makeBody(configuration: Configuration) -> some View {
        Button {
            configuration.isOn.toggle()
        } label: {
            HStack(alignment: .center, spacing: 9) {
                Image(systemName: configuration.isOn ? "checkmark.square.fill" : "square")
                    .font(.title3)
                    .foregroundStyle(configuration.isOn ? Color.accentColor : .secondary)
                configuration.label
                    .foregroundStyle(.primary)
                    .multilineTextAlignment(.leading)
            }
            .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel("我已阅读并同意隐私政策")
        .accessibilityValue(configuration.isOn ? "已勾选" : "未勾选")
    }
}

struct PrivacySettingsSection: View {
    @ObservedObject var session: RemoteSessionModel
    @State private var confirmThumbnails = false
    @State private var confirmClear = false

    var body: some View {
        Section {
            NavigationLink {
                PrivacyAuthorizationView(session: session)
            } label: {
                HStack {
                    Text("隐私与授权")
                    Spacer()
                    Text(session.hasNetworkConsent ? "已授权" : "未授权")
                        .foregroundStyle(.secondary)
                }
            }
            Toggle("保存远程画面预览", isOn: Binding(
                get: { session.savesConnectionThumbnails },
                set: { enabled in
                    if enabled { confirmThumbnails = true }
                    else { session.setSavesConnectionThumbnails(false) }
                }
            ))
            .disabled(!session.hasNetworkConsent || session.thumbnailCleanupInProgress)
            Button(session.thumbnailCleanupInProgress ? "正在清除…" : "清除预览图", role: .destructive) {
                confirmClear = true
            }
            .disabled(session.thumbnailCleanupInProgress)
            if let error = session.thumbnailCleanupError {
                Text(error).font(.footnote).foregroundStyle(.secondary)
            }
        } header: {
            Text("隐私")
        }
        .alert("保存远程画面预览？", isPresented: $confirmThumbnails) {
            Button("开启保存") { session.setSavesConnectionThumbnails(true) }
            Button("取消", role: .cancel) { }
        } message: {
            Text("预览可能包含远程电脑上的个人或工作信息。开启后，每次连接会自动将一张画面保存在本机并显示在最近连接中。你可以随时关闭并清除。")
        }
        .alert("清除预览图？", isPresented: $confirmClear) {
            Button("清除", role: .destructive) { session.clearConnectionThumbnails() }
            Button("取消", role: .cancel) { }
        } message: {
            Text(session.savesConnectionThumbnails
                 ? "将删除本机保存的所有远程预览图，保留连接记录、密码和接收的文件。后续连接仍会保存新的预览图。"
                 : "将删除本机保存的所有远程预览图，保留连接记录、密码和接收的文件。")
        }
    }
}

private struct PrivacyAuthorizationView: View {
    @ObservedObject var session: RemoteSessionModel
    @State private var confirmWithdrawal = false

    var body: some View {
        Form {
            Section {
                LabeledContent("授权状态", value: session.hasNetworkConsent ? "已授权" : "未授权")
                NavigationLink("隐私政策") { PrivacyPolicyView() }
            } footer: {
                if !session.hasNetworkConsent {
                    Text("授权后可使用远程连接。")
                }
            }

            Section {
                if session.hasNetworkConsent {
                    Button("撤回隐私授权", role: .destructive) {
                        confirmWithdrawal = true
                    }
                } else {
                    Button("阅读并授权") { session.showPrivacyNotice() }
                }
            }
        }
        .navigationTitle("隐私与授权")
        .navigationBarTitleDisplayMode(.inline)
        .alert("是否撤回隐私授权？", isPresented: $confirmWithdrawal) {
            Button("撤回授权", role: .destructive) { session.revokeNetworkConsent() }
            Button("取消", role: .cancel) { }
        } message: {
            Text("将停止远程连接、关闭画面预览保存并清除已有预览。连接记录和接收文件会保留，已有服务端数据不会删除。您可随时重新授权。")
        }
    }
}

/// Bundled policy is readable before any network consent or external request.
private struct PrivacyPolicyDocument: View {
    private static let text: String? = {
        guard let url = Bundle.main.url(forResource: "PRIVACY", withExtension: "md"),
              let text = try? String(contentsOf: url, encoding: .utf8),
              !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        // The app's interface is Chinese; show the complete Chinese translation.
        if let start = text.range(of: "## 中文\n"),
           let end = text.range(of: "## English", range: start.upperBound..<text.endIndex) {
            return String(text[start.upperBound..<end.lowerBound])
        }
        return text
    }()
    static var isAvailable: Bool { text != nil }
    private static let paragraphs = (text ?? "隐私政策暂时无法读取，请拒绝联网并重新安装应用。")
        .components(separatedBy: "\n\n")
        .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
        .filter { !$0.isEmpty && $0 != "---" }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            ForEach(Self.paragraphs.indices, id: \.self) { index in
                let paragraph = Self.paragraphs[index]
                if paragraph.hasPrefix("#") {
                    Text(paragraph.trimmingCharacters(in: CharacterSet(charactersIn: "# ")))
                        .font(.headline)
                        .padding(.top, 4)
                } else if paragraph.hasPrefix("更新日期：") {
                    Text(paragraph)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                } else {
                    Text((try? AttributedString(markdown: paragraph,
                        options: .init(interpretedSyntax: .inlineOnlyPreservingWhitespace)))
                         ?? AttributedString(paragraph))
                        .font(.body)
                        .lineSpacing(4)
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .textSelection(.enabled)
    }
}

struct PrivacyPolicyView: View {
    var body: some View {
        ScrollView {
            PrivacyPolicyDocument().padding()
        }
        .navigationTitle("隐私政策")
        .navigationBarTitleDisplayMode(.inline)
    }
}
