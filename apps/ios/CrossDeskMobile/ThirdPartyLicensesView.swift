import SwiftUI
import UIKit

private struct LicenseDocument: Decodable, Identifiable {
    let title: String
    let text: String
    var id: String { title }
}

private struct LicensedComponent: Decodable, Identifiable {
    let id: String
    let name: String
    let version: String
    let license: String
    let sourceURL: URL
    let buildSourceURL: URL?
    let documents: [LicenseDocument]

    var displayVersion: String {
        if id == "crossdesk" {
            return Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        }
        let currentVersion = SourceMetadata.bundled?.componentSources[id]?.version ?? version
        return currentVersion.count == 40 ? String(currentVersion.prefix(12)) : currentVersion
    }

    var resolvedSourceURL: URL {
        if id == "crossdesk" {
            return SourceMetadata.bundled?.sourceURL ?? sourceURL
        }
        return SourceMetadata.bundled?.componentSources[id]?.sourceURL ?? sourceURL
    }

    var resolvedBuildSourceURL: URL? {
        SourceMetadata.bundled?.componentSources[id]?.buildSourceURL ?? buildSourceURL
    }
}

private struct LicenseCatalog: Decodable {
    let schemaVersion: Int
    let components: [LicensedComponent]

    static let bundled: Result<LicenseCatalog, Error> = Result {
        guard let url = Bundle.main.url(forResource: "ThirdPartyLicenses", withExtension: "json") else {
            throw CocoaError(.fileNoSuchFile)
        }
        let catalog = try JSONDecoder().decode(LicenseCatalog.self, from: Data(contentsOf: url))
        let requiredComponents: Set<String> = ["crossdesk", "openfec"]
        guard catalog.schemaVersion == 1,
              requiredComponents.isSubset(of: Set(catalog.components.map(\.id))) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return catalog
    }
}

struct ComponentSourceMetadata: Decodable {
    let version: String?
    let sourceURL: URL?
    let buildSourceURL: URL?
}

struct SourceMetadata: Decodable {
    let revision: String
    let tag: String?
    let isModified: Bool
    let sourceURL: URL
    let buildInstructionsURL: URL
    let privacyPolicyURL: URL
    let componentSources: [String: ComponentSourceMetadata]

    static let bundled: SourceMetadata? = {
        guard let url = Bundle.main.url(forResource: "SourceMetadata", withExtension: "json"),
              let data = try? Data(contentsOf: url) else { return nil }
        return try? JSONDecoder().decode(SourceMetadata.self, from: data)
    }()
}

struct ThirdPartyLicensesView: View {
    @ObservedObject var session: RemoteSessionModel
    @EnvironmentObject private var appUpdates: AppUpdateChecker
    var body: some View {
        List {
            Section {
                LabeledContent("版本", value: appUpdates.currentVersion)
                if appUpdates.updateAvailable {
                    Link(destination: URL(string: "https://crossdesk.cn")!) {
                        HStack {
                            UpdateDot()
                            Text("新版本可用：v\(appUpdates.availableVersion)")
                            Spacer()
                            Image(systemName: "arrow.up.right")
                        }
                    }
                }
                if !session.hasNetworkConsent {
                    Text("完成隐私授权后可检查更新").foregroundStyle(.secondary)
                } else if appUpdates.status == .checking {
                    Text("正在检查更新…").foregroundStyle(.secondary)
                } else if appUpdates.status == .upToDate {
                    Text("当前已是最新版本").foregroundStyle(.secondary)
                } else if appUpdates.status == .failed {
                    Text("检查失败，请稍后重试").foregroundStyle(.red)
                }
                Button("检查更新") {
                    if session.hasNetworkConsent { appUpdates.checkNow() }
                    else { session.showPrivacyNotice() }
                }
                .disabled(appUpdates.status == .checking)
            }
            switch LicenseCatalog.bundled {
            case .success(let catalog):
                if let application = catalog.components.first(where: { $0.id == "crossdesk" }) {
                    Section {
                        NavigationLink("软件许可") {
                            ComponentLicenseView(component: application)
                        }
                        NavigationLink("开源组件") {
                            OpenSourceComponentsView(catalog: catalog)
                        }
                        NavigationLink("源码与构建说明") {
                            SourceCodeView(application: application)
                        }
                    } footer: {
                        Text("许可与版权声明可离线查阅。")
                    }
                }
            case .failure:
                VStack(spacing: 12) {
                    Image(systemName: "doc.text.magnifyingglass")
                        .font(.largeTitle)
                    Text("无法读取开源许可")
                        .font(.headline)
                    Text("应用中的许可文件缺失或损坏，请重新安装应用。")
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                }
                .padding()
            }
        }
        .navigationTitle("关于 CrossDesk")
        .navigationBarTitleDisplayMode(.inline)
        .onChange(of: session.hasNetworkConsent) { allowed in
            if allowed { appUpdates.checkNow() }
        }
    }
}

struct UpdateDot: View {
    var body: some View {
        Circle()
            .fill(.red)
            .frame(width: 8, height: 8)
            .accessibilityHidden(true)
    }
}

private struct OpenSourceComponentsView: View {
    let catalog: LicenseCatalog

    var body: some View {
        List {
            if let openFEC = catalog.components.first(where: { $0.id == "openfec" }) {
                Section("OpenFEC 告知") {
                    Text("本应用使用 OpenFEC 提供前向纠错功能，遵循 CeCILL-C 1.0 许可。该许可规定了有限保证及责任限制，详情见许可全文。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                    Text("(c) Copyright 2009 - 2012 INRIA - All rights reserved")
                        .font(.footnote)
                    NavigationLink("OpenFEC 许可与源码") {
                        ComponentLicenseView(component: openFEC)
                    }
                }
            }
            Section {
                ForEach(catalog.components.filter { $0.id != "crossdesk" && $0.id != "openfec" }) { component in
                    NavigationLink {
                        ComponentLicenseView(component: component)
                    } label: {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(component.name)
                            Text(component.license)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            } footer: {
                Text("各组件遵循所列开源许可证，其授予的权利不受本应用普通使用条款限制。查看源码需要网络连接。")
            }
        }
        .navigationTitle("开源组件")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct SourceCodeView: View {
    let application: LicensedComponent

    var body: some View {
        List {
            Section("本版本源码与构建说明") {
                if let source = SourceMetadata.bundled {
                    LabeledContent("源码版本", value: source.tag ?? String(source.revision.prefix(12)))
                        .textSelection(.enabled)
                    if source.isModified {
                        Text("开发版本包含尚未发布的修改，以下链接为基础版本源码。")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                    Link("查看源码", destination: source.sourceURL)
                    Link("构建说明", destination: source.buildInstructionsURL)
                } else {
                    Text("本版本源码信息暂不可用。")
                        .foregroundStyle(.secondary)
                    Link("项目主页", destination: application.sourceURL)
                }
            }
        }
        .navigationTitle("源码与构建说明")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct ComponentLicenseView: View {
    let component: LicensedComponent

    var body: some View {
        List {
            Section {
                LabeledContent("版本", value: component.displayVersion)
                    .textSelection(.enabled)
                LabeledContent("许可", value: component.license)
                Link("查看源码", destination: component.resolvedSourceURL)
                if let buildSourceURL = component.resolvedBuildSourceURL {
                    Link("构建配方与项目补丁", destination: buildSourceURL)
                }
            }
            Section("许可与版权声明") {
                ForEach(component.documents) { document in
                    NavigationLink {
                        LicenseTextView(text: document.text)
                            .navigationTitle(documentTitle(document.title))
                            .navigationBarTitleDisplayMode(.inline)
                    } label: {
                        Text(documentTitle(document.title))
                    }
                }
            }
        }
        .navigationTitle(component.name)
        .navigationBarTitleDisplayMode(.inline)
    }

    private func documentTitle(_ title: String) -> String {
        switch title {
        case "Source copyright and license notices": return "版权与许可声明"
        case "apps/ios/licenses/OPEN_SOURCE_NOTICE.txt": return "开源软件权利说明"
        case "LICENSE" where component.id == "crossdesk": return "GNU GPL 第 3 版"
        default: return title
        }
    }
}

// UITextView keeps long license documents selectable and efficient to scroll.
private struct LicenseTextView: UIViewRepresentable {
    let text: String

    func makeUIView(context: Context) -> UITextView {
        let view = UITextView()
        view.isEditable = false
        view.isSelectable = true
        view.dataDetectorTypes = [.link]
        view.adjustsFontForContentSizeCategory = true
        view.font = .preferredFont(forTextStyle: .body)
        view.textColor = .label
        view.backgroundColor = .systemBackground
        view.textContainerInset = UIEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        return view
    }

    func updateUIView(_ view: UITextView, context: Context) {
        if view.text != text { view.text = text }
    }
}
