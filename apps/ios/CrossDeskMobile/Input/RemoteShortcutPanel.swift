import SwiftUI

struct RemoteShortcutPanel: View {
    @ObservedObject var store: RemoteShortcutStore
    @ObservedObject var session: RemoteSessionModel
    let systemKeyboard: () -> Void
    let dismiss: () -> Void
    let manage: () -> Void
    @State private var category = "common"
    @State private var page = 0
    private let groups = ["common", "keys", "system", "mine"]
    private let labels = ["常用", "按键", "系统", "我的"]
    private var platform: String { session.remotePlatform.rawValue }
    private var items: [RemoteShortcut] { store.items(platform: platform).filter { $0.group == category } }

    var body: some View {
        VStack(spacing: 0) {
            RemoteKeyboardToolbar(
                switchKeyboard: systemKeyboard,
                more: systemKeyboard,
                moreAccessibilityLabel: NSLocalizedString("系统键盘", comment: ""),
                dismiss: dismiss
            ) {
                Text(NSLocalizedString("快捷键", comment: ""))
                    .font(.subheadline).foregroundStyle(.secondary)
            }
            VStack(spacing: 4) {
                Picker(NSLocalizedString("快捷键", comment: ""), selection: $category) {
                    ForEach(Array(groups.enumerated()), id: \.element) { index, group in
                        Text(NSLocalizedString(labels[index], comment: "")).tag(group)
                    }
                }.pickerStyle(.segmented)
                if category == "system" && platform == "linux" {
                    Picker(NSLocalizedString("选择桌面方案", comment: ""), selection: Binding(get: { store.desktop }, set: { store.desktop = $0 })) {
                        Text(NSLocalizedString("选择桌面方案", comment: "")).tag("")
                        Text("GNOME").tag("gnome")
                        Text("KDE Plasma").tag("kde")
                    }.frame(maxWidth: .infinity, alignment: .leading)
                }
                GeometryReader { geometry in
                  ScrollView {
                    if items.isEmpty {
                        Text(NSLocalizedString(!RemoteShortcut.platforms.contains(platform) ? "等待远端平台信息" : category == "system" ? "选择桌面方案以加载系统预设" : "还没有自定义快捷键", comment: ""))
                            .font(.footnote).foregroundStyle(.secondary).frame(maxWidth: .infinity).padding(.vertical, 16)
                    } else {
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 3), spacing: 6) {
                            ForEach(Array(items.dropFirst(page * 6).prefix(6))) { item in
                                Button {
                                    UIImpactFeedbackGenerator(style: .light).impactOccurred()
                                    session.sendShortcut(item)
                                } label: {
                                    VStack(spacing: 3) {
                                        Text(item.title).font(.subheadline)
                                        Text(item.chord).font(.caption2).foregroundStyle(.secondary)
                                    }
                                    .multilineTextAlignment(.center)
                                    .frame(maxWidth: .infinity, minHeight: max(44, (geometry.size.height - (session.keyboardInputStatus.isEmpty ? 6 : 34)) / 2))
                                    .background(Color(uiColor: .secondarySystemBackground), in: RoundedRectangle(cornerRadius: 7))
                                }.disabled(!session.isConnected)
                                .accessibilityLabel(item.title + ", " + item.chord)
                            }
                        }
                    }
                    if !session.keyboardInputStatus.isEmpty {
                        Text(session.keyboardInputStatus).font(.caption).foregroundStyle(.secondary)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
                }
                HStack(spacing: 6) {
                    if items.count > 6 {
                        Button { page = max(0, page - 1) } label: { Image(systemName: "chevron.left").frame(width: 44, height: 44) }
                            .disabled(page == 0).accessibilityLabel(NSLocalizedString("上一页", comment: ""))
                        Text("\(page + 1) / \((items.count + 5) / 6)").font(.caption)
                        Button { page += 1 } label: { Image(systemName: "chevron.right").frame(width: 44, height: 44) }
                            .disabled((page + 1) * 6 >= items.count).accessibilityLabel(NSLocalizedString("下一页", comment: ""))
                    } else {
                        Text(RemoteShortcut.platformName(platform)).font(.caption).foregroundStyle(.secondary)
                    }
                    Spacer(minLength: 0)
                    Button(NSLocalizedString("管理与自定义", comment: ""), action: manage)
                        .font(.subheadline).frame(minHeight: 44)
                        .disabled(!RemoteShortcut.platforms.contains(platform))
                }.frame(height: 44)
            }
            .padding(.horizontal, 6)
            .padding(.top, 4)
        }
        .buttonStyle(.plain)
        .background(Color(uiColor: RemoteKeyboardInputBar.keyboardBackground))
        .onValueChange(of: category) { _ in page = 0 }
        .onValueChange(of: platform) { _ in page = 0 }
        .onValueChange(of: store.revision) { _ in page = min(page, max(0, (items.count - 1) / 6)) }
    }
}

struct RemoteShortcutManager: View {
    @ObservedObject var store: RemoteShortcutStore
    let platform: String
    @Environment(\.dismiss) private var dismiss
    @State private var editor: RemoteShortcut?
    @State private var error = ""

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text(NSLocalizedString("本地编辑，输入不会发送到远端", comment: "")).font(.footnote).foregroundStyle(.secondary)
                }
                Section(NSLocalizedString("键盘上方常用栏", comment: "")) {
                    ForEach(store.pinned(platform: platform)) { item in
                        HStack {
                            label(item)
                            Spacer(minLength: 2)
                            Button {
                                var values = store.pins(platform: platform)
                                if let index = values.firstIndex(of: item.id), index > 0 { values.swapAt(index - 1, index); store.setPins(values, platform: platform) }
                            } label: { Image(systemName: "arrow.up").frame(width: 44, height: 44) }
                                .disabled(store.pins(platform: platform).first == item.id)
                                .accessibilityLabel(NSLocalizedString("向上移动", comment: ""))
                            Button(NSLocalizedString("移除", comment: "")) { store.setPins(store.pins(platform: platform).filter { $0 != item.id }, platform: platform) }
                                .frame(minHeight: 44)
                        }.buttonStyle(.borderless)
                    }
                }
                Section(NSLocalizedString("添加到常用栏", comment: "")) {
                    ForEach(store.items(platform: platform)) { item in
                        HStack {
                            label(item)
                            Spacer(minLength: 2)
                            if !store.pins(platform: platform).contains(item.id) {
                                Button {
                                    var values = store.pins(platform: platform)
                                    guard values.count < 8 else { error = NSLocalizedString("最多固定 8 个快捷键", comment: ""); return }
                                    values.append(item.id); store.setPins(values, platform: platform)
                                } label: { Image(systemName: "plus").frame(width: 44, height: 44) }
                                    .accessibilityLabel(NSLocalizedString("添加到常用栏", comment: ""))
                            }
                            if item.custom {
                                Button(NSLocalizedString("编辑", comment: "")) { editor = item }.frame(minHeight: 44)
                                Button(role: .destructive) { store.remove(item.id) } label: { Image(systemName: "trash").frame(width: 44, height: 44) }
                                    .accessibilityLabel(NSLocalizedString("删除", comment: ""))
                            }
                        }.buttonStyle(.borderless)
                    }
                }
                if !error.isEmpty { Text(error).foregroundStyle(.red) }
            }
            .navigationTitle(NSLocalizedString("管理快捷键", comment: ""))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button(NSLocalizedString("完成", comment: "")) { dismiss() } }
                ToolbarItem(placement: .primaryAction) {
                    Button(NSLocalizedString("新建", comment: "")) {
                        editor = RemoteShortcut(id: UUID().uuidString, name: "", platform: platform, group: "mine", key: 83, modifiers: [platform == "macos" ? 91 : 17])
                    }
                }
            }
            .sheet(item: $editor) { item in RemoteShortcutEditor(store: store, item: item) }
        }
    }
    private func label(_ item: RemoteShortcut) -> some View {
        VStack(alignment: .leading, spacing: 3) { Text(item.title); Text(item.chord).font(.caption).foregroundStyle(.secondary) }
    }
}

private struct RemoteShortcutEditor: View {
    @ObservedObject var store: RemoteShortcutStore
    let item: RemoteShortcut
    @Environment(\.dismiss) private var dismiss
    @State private var name: String
    @State private var platform: String
    @State private var key: UInt
    @State private var modifiers: Set<UInt>
    @State private var pin: Bool
    @State private var error = ""
    init(store: RemoteShortcutStore, item: RemoteShortcut) {
        self.store = store; self.item = item
        _name = State(initialValue: item.name); _platform = State(initialValue: item.platform)
        _key = State(initialValue: item.key); _modifiers = State(initialValue: Set(item.modifiers))
        _pin = State(initialValue: item.custom ? store.pins(platform: item.platform).contains(item.id) : store.pins(platform: item.platform).count < 8)
    }
    private var preview: RemoteShortcut { RemoteShortcut(id: "", name: name, platform: platform, group: "mine", key: key, modifiers: RemoteShortcut.modifierKeys.filter(modifiers.contains)) }
    var body: some View {
        NavigationStack {
            Form {
                Section { Text(NSLocalizedString("本地编辑，输入不会发送到远端", comment: "")).font(.footnote).foregroundStyle(.secondary) }
                TextField(NSLocalizedString("名称", comment: ""), text: $name)
                Picker(NSLocalizedString("适用远端", comment: ""), selection: $platform) {
                    ForEach(RemoteShortcut.platforms, id: \.self) { Text(RemoteShortcut.platformName($0)).tag($0) }
                }
                Picker(NSLocalizedString("主按键", comment: ""), selection: $key) {
                    ForEach(RemoteShortcut.keys, id: \.self) { Text(RemoteShortcut.keyName($0, platform: platform)).tag($0) }
                }
                Section(NSLocalizedString("修饰键", comment: "")) {
                    HStack(spacing: 6) {
                        ForEach(RemoteShortcut.modifierKeys, id: \.self) { code in
                            Button {
                                if modifiers.contains(code) { modifiers.remove(code) } else { modifiers.insert(code) }
                            } label: {
                                Text(RemoteShortcut.modifierName(code, platform: platform)).font(.subheadline)
                                    .frame(maxWidth: .infinity, minHeight: 44)
                                    .background(modifiers.contains(code) ? Color.accentColor.opacity(0.18) : Color(uiColor: .tertiarySystemFill), in: RoundedRectangle(cornerRadius: 7))
                            }.buttonStyle(.borderless)
                                .accessibilityAddTraits(modifiers.contains(code) ? .isSelected : [])
                        }
                    }
                }
                Section(NSLocalizedString("发送组合", comment: "")) { Text(preview.chord) }
                Toggle(NSLocalizedString("固定到键盘上方", comment: ""), isOn: $pin)
                if !error.isEmpty { Text(error).foregroundStyle(.red) }
            }
            .navigationTitle(NSLocalizedString(item.custom ? "编辑快捷键" : "新建快捷键", comment: ""))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button(NSLocalizedString("取消", comment: "")) { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button(NSLocalizedString("保存", comment: "")) {
                        if let message = store.save(id: item.custom ? item.id : nil, name: name, platform: platform, key: key, modifiers: Array(modifiers), pin: pin) { error = message } else { dismiss() }
                    }
                }
            }
        }
    }
}
