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
                LabeledContent(NSLocalizedString("版本", comment: ""), value: appUpdates.currentVersion)
                if appUpdates.updateAvailable {
                    Link(destination: URL(string: "https://crossdesk.cn")!) {
                        HStack {
                            UpdateDot()
                            Text(String(format: NSLocalizedString("新版本可用：v%1$@", comment: ""), String(describing: appUpdates.availableVersion)))
                            Spacer()
                            Image(systemName: "arrow.up.right")
                        }
                    }
                }
                if !session.hasNetworkConsent {
                    Text(NSLocalizedString("完成隐私授权后可检查更新", comment: "")).foregroundStyle(.secondary)
                } else if appUpdates.status == .checking {
                    Text(NSLocalizedString("正在检查更新…", comment: "")).foregroundStyle(.secondary)
                } else if appUpdates.status == .upToDate {
                    Text(NSLocalizedString("当前已是最新版本", comment: "")).foregroundStyle(.secondary)
                } else if appUpdates.status == .failed {
                    Text(NSLocalizedString("检查失败，请稍后重试", comment: "")).foregroundStyle(.red)
                }
                Button(NSLocalizedString("检查更新", comment: "")) {
                    if session.hasNetworkConsent { appUpdates.checkNow() }
                    else { session.showPrivacyNotice() }
                }
                .disabled(appUpdates.status == .checking)
            }
            switch LicenseCatalog.bundled {
            case .success(let catalog):
                if let application = catalog.components.first(where: { $0.id == "crossdesk" }) {
                    Section {
                        NavigationLink(NSLocalizedString("软件许可", comment: "")) {
                            ComponentLicenseView(component: application)
                        }
                        NavigationLink(NSLocalizedString("开源组件", comment: "")) {
                            OpenSourceComponentsView(catalog: catalog)
                        }
                        NavigationLink(NSLocalizedString("源码与构建说明", comment: "")) {
                            SourceCodeView(application: application)
                        }
                    } footer: {
                        Text(NSLocalizedString("许可与版权声明可离线查阅。", comment: ""))
                    }
                }
            case .failure:
                VStack(spacing: 12) {
                    Image(systemName: "doc.text.magnifyingglass")
                        .font(.largeTitle)
                    Text(NSLocalizedString("无法读取开源许可", comment: ""))
                        .font(.headline)
                    Text(NSLocalizedString("应用中的许可文件缺失或损坏，请重新安装应用。", comment: ""))
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                }
                .padding()
            }
        }
        .readableContentWidth()
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
        .navigationTitle(NSLocalizedString("关于 CrossDesk", comment: ""))
        .navigationBarTitleDisplayMode(.inline)
        .onValueChange(of: session.hasNetworkConsent) { allowed in
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
                Section(NSLocalizedString("OpenFEC 告知", comment: "")) {
                    Text(NSLocalizedString("本应用使用 OpenFEC 提供前向纠错功能，遵循 CeCILL-C 1.0 许可。该许可规定了有限保证及责任限制，详情见许可全文。", comment: ""))
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                    Text("(c) Copyright 2009 - 2012 INRIA - All rights reserved")
                        .font(.footnote)
                    NavigationLink(NSLocalizedString("OpenFEC 许可与源码", comment: "")) {
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
                Text(NSLocalizedString("各组件遵循所列开源许可证，其授予的权利不受本应用普通使用条款限制。查看源码需要网络连接。", comment: ""))
            }
        }
        .readableContentWidth()
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
        .navigationTitle(NSLocalizedString("开源组件", comment: ""))
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct SourceCodeView: View {
    let application: LicensedComponent

    var body: some View {
        List {
            Section(NSLocalizedString("本版本源码与构建说明", comment: "")) {
                if let source = SourceMetadata.bundled {
                    LabeledContent(NSLocalizedString("源码版本", comment: ""), value: source.tag ?? String(source.revision.prefix(12)))
                        .textSelection(.enabled)
                    if source.isModified {
                        Text(NSLocalizedString("开发版本包含尚未发布的修改，以下链接为基础版本源码。", comment: ""))
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                    Link(NSLocalizedString("查看源码", comment: ""), destination: source.sourceURL)
                    Link(NSLocalizedString("构建说明", comment: ""), destination: source.buildInstructionsURL)
                } else {
                    Text(NSLocalizedString("本版本源码信息暂不可用。", comment: ""))
                        .foregroundStyle(.secondary)
                    Link(NSLocalizedString("项目主页", comment: ""), destination: application.sourceURL)
                }
            }
        }
        .readableContentWidth()
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
        .navigationTitle(NSLocalizedString("源码与构建说明", comment: ""))
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct ComponentLicenseView: View {
    let component: LicensedComponent

    var body: some View {
        List {
            Section {
                LabeledContent(NSLocalizedString("版本", comment: ""), value: component.displayVersion)
                    .textSelection(.enabled)
                LabeledContent(NSLocalizedString("许可", comment: ""), value: component.license)
                Link(NSLocalizedString("查看源码", comment: ""), destination: component.resolvedSourceURL)
                if let buildSourceURL = component.resolvedBuildSourceURL {
                    Link(NSLocalizedString("构建配方与项目补丁", comment: ""), destination: buildSourceURL)
                }
            }
            Section(NSLocalizedString("许可与版权声明", comment: "")) {
                ForEach(component.documents) { document in
                    NavigationLink {
                        LicenseTextView(text: document.text)
                            .readableContentWidth()
                            .navigationTitle(documentTitle(document.title))
                            .navigationBarTitleDisplayMode(.inline)
                    } label: {
                        Text(documentTitle(document.title))
                    }
                }
            }
        }
        .readableContentWidth()
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
        .navigationTitle(component.name)
        .navigationBarTitleDisplayMode(.inline)
    }

    private func documentTitle(_ title: String) -> String {
        switch title {
        case "Source copyright and license notices": return NSLocalizedString("版权与许可声明", comment: "")
        case "apps/ios/licenses/OPEN_SOURCE_NOTICE.txt": return NSLocalizedString("开源软件权利说明", comment: "")
        case "LICENSE" where component.id == "crossdesk": return NSLocalizedString("GNU GPL 第 3 版", comment: "")
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
