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
        return version.count == 40 ? String(version.prefix(12)) : version
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
        guard catalog.schemaVersion == 1, catalog.components.contains(where: { $0.id == "openfec" }) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return catalog
    }
}

struct SourceMetadata: Decodable {
    let revision: String
    let tag: String?
    let isModified: Bool
    let sourceURL: URL
    let buildInstructionsURL: URL
    let privacyPolicyURL: URL

    static let bundled: SourceMetadata? = {
        guard let url = Bundle.main.url(forResource: "SourceMetadata", withExtension: "json"),
              let data = try? Data(contentsOf: url) else { return nil }
        return try? JSONDecoder().decode(SourceMetadata.self, from: data)
    }()
}

struct ThirdPartyLicensesView: View {
    var body: some View {
        Group {
            switch LicenseCatalog.bundled {
            case .success(let catalog):
                List {
                    Section("本版本源码与构建说明") {
                        if let source = SourceMetadata.bundled {
                            Text(source.tag ?? String(source.revision.prefix(12)))
                                .textSelection(.enabled)
                            if source.isModified {
                                Text("开发版本包含尚未发布的修改，以下链接为基础版本源码。")
                                    .font(.footnote)
                                    .foregroundStyle(.secondary)
                            }
                            Link("CrossDesk 源码", destination: source.sourceURL)
                            Link("获取源码、修改与重新构建", destination: source.buildInstructionsURL)
                            Link("隐私政策", destination: source.privacyPolicyURL)
                        }
                        Text("各组件的使用、复制、修改和再分发权利以相应开源许可证为准。普通使用条款不限制这些许可证授予的权利。")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                    Section("OpenFEC 告知") {
                        Text("本应用使用 OpenFEC \(catalog.components.first(where: { $0.id == "openfec" })?.displayVersion ?? "") 提供前向纠错功能，遵循 CeCILL-C 1.0 许可。")
                        Text("(c) Copyright 2009 - 2012 INRIA - All rights reserved")
                            .font(.footnote)
                        Text("OpenFEC 按其许可提供有限保证，作者、权利人和后续许可人的责任亦受限制。完整声明、许可条款及源码见下方。")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                        if let openFEC = catalog.components.first(where: { $0.id == "openfec" }) {
                            NavigationLink("OpenFEC 许可与源码") {
                                ComponentLicenseView(component: openFEC)
                            }
                        }
                    }
                    Section {
                        ForEach(catalog.components) { component in
                            NavigationLink {
                                ComponentLicenseView(component: component)
                            } label: {
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(component.name)
                                    Text(component.displayVersion + " · " + component.license)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }
                        }
                    } header: {
                        Text("开源组件")
                    } footer: {
                        Text("许可全文和版权声明可离线阅读；查看源码需要网络连接。")
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
        .navigationTitle("关于与开源许可")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct ComponentLicenseView: View {
    let component: LicensedComponent

    var body: some View {
        List {
            Section {
                Text(component.name).font(.headline)
                Text(component.displayVersion).textSelection(.enabled)
                Text(component.license).foregroundStyle(.secondary)
                Link("查看源码", destination: component.id == "crossdesk"
                     ? SourceMetadata.bundled?.sourceURL ?? component.sourceURL
                     : component.sourceURL)
                if let buildSourceURL = component.buildSourceURL {
                    Link("构建配方与项目补丁", destination: buildSourceURL)
                }
            }
            Section("许可与版权声明") {
                ForEach(component.documents) { document in
                    NavigationLink {
                        LicenseTextView(text: document.text)
                            .navigationTitle(document.title)
                            .navigationBarTitleDisplayMode(.inline)
                    } label: {
                        Text(document.title == "Source copyright and license notices"
                             ? "源码版权与许可声明" : document.title)
                    }
                }
            }
        }
        .navigationTitle(component.name)
        .navigationBarTitleDisplayMode(.inline)
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
