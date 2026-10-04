import SwiftUI

struct AnnouncementBadge: View {
    @ObservedObject var inbox: AnnouncementInbox

    var body: some View {
        Image(systemName: "envelope")
            .font(.system(size: 18, weight: .semibold))
            .frame(width: 40, height: 40)
            .background(Color(.secondarySystemGroupedBackground),
                        in: RoundedRectangle(cornerRadius: 10, style: .continuous))
            .overlay(alignment: .topTrailing) {
                if inbox.state.unread > 0 {
                    Text(inbox.state.unread > 99 ? "99+" : "\(inbox.state.unread)")
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(.white)
                        .padding(.horizontal, 4)
                        .frame(minWidth: 16, minHeight: 16)
                        .background(.red, in: Capsule())
                        .offset(x: 5, y: -3)
                }
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("通知公告")
            .accessibilityValue(inbox.state.unread > 0 ? "\(inbox.state.unread) 条未读" : "无未读公告")
    }
}

struct AnnouncementSelection: Hashable {
    let id: Int64
    let revision: Int64
}

private enum AnnouncementDate {
    static let formatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm"
        return formatter
    }()

    static func string(_ item: Announcement) -> String { formatter.string(from: item.date) }
}

struct AnnouncementInboxView: View {
    @ObservedObject var session: RemoteSessionModel
    @ObservedObject var inbox: AnnouncementInbox
    @State private var pendingDelete: Announcement?

    var body: some View {
        List {
            if inbox.state.deleteFailed {
                Text("无法删除这条公告，请重试。")
                    .foregroundStyle(.red)
            }
            if inbox.state.failed && session.announcementsConnected {
                HStack {
                    Text("公告加载失败，请重试。")
                    Spacer()
                    Button("重试") { session.refreshAnnouncements() }
                        .disabled(!session.announcementsConnected)
                }
            }
            Section {
                ForEach(inbox.state.items) { item in
                    NavigationLink(value: HomeDestination.announcement(
                        AnnouncementSelection(id: item.id, revision: item.revision))) {
                        HStack(alignment: .top, spacing: 10) {
                            Circle()
                                .fill(item.read ? Color.clear : Color.accentColor)
                                .frame(width: 7, height: 7)
                                .padding(.top, 7)
                                .accessibilityHidden(true)
                            VStack(alignment: .leading, spacing: 7) {
                                Text(verbatim: item.title)
                                    .fontWeight(item.read ? .regular : .semibold)
                                    .foregroundStyle(.primary)
                                    .lineLimit(2)
                                Text(AnnouncementDate.string(item))
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                        .padding(.vertical, 5)
                        .accessibilityElement(children: .combine)
                        .accessibilityValue(item.read ? "已读" : "未读")
                    }
                    .swipeActions {
                        Button("删除", role: .destructive) { pendingDelete = item }
                    }
                    .contextMenu {
                        Button("删除公告", role: .destructive) { pendingDelete = item }
                    }
                }
                if inbox.state.loading {
                    HStack { Spacer(); ProgressView("加载中…"); Spacer() }
                } else if inbox.state.items.count < inbox.state.total && !inbox.state.failed {
                    Button("加载更多") { inbox.loadMore() }
                        .frame(maxWidth: .infinity)
                        .disabled(!session.announcementsConnected)
                        .onAppear { inbox.loadMore() }
                } else if inbox.state.loaded && inbox.state.total == 0 &&
                            !inbox.state.failed && session.announcementsConnected {
                    Text("暂无公告")
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, minHeight: 120)
                }
            } header: {
                Text("共 \(inbox.state.total) 条 · \(inbox.state.unread) 条未读")
            }
        }
        .navigationTitle("通知公告")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .navigationBarTrailing) {
                Button { session.refreshAnnouncements() } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .accessibilityLabel("刷新公告")
                .disabled(inbox.state.loading || !session.announcementsConnected)
            }
        }
        .refreshable {
            session.refreshAnnouncements()
            while inbox.state.loading && !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 100_000_000)
            }
        }
        .onAppear { session.refreshAnnouncements() }
        .alert("删除公告", isPresented: Binding(
            get: { pendingDelete != nil },
            set: { if !$0 { pendingDelete = nil } }
        )) {
            Button("取消", role: .cancel) { pendingDelete = nil }
            Button("删除", role: .destructive) {
                if let item = pendingDelete { inbox.dismiss(item) }
                pendingDelete = nil
            }
        } message: {
            Text("删除这条公告？")
        }
    }
}

struct AnnouncementDetailView: View {
    @ObservedObject var inbox: AnnouncementInbox
    let selection: AnnouncementSelection
    @Environment(\.openURL) private var openURL

    private var item: Announcement? {
        inbox.state.items.first { $0.id == selection.id && $0.revision == selection.revision }
    }

    var body: some View {
        ScrollView {
            if let item {
                VStack(alignment: .leading, spacing: 16) {
                    Text(verbatim: item.title)
                        .font(.title2.weight(.semibold))
                    Text("\(AnnouncementDate.string(item)) · \(item.read ? "已读" : "未读")")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    if inbox.state.readSaveFailed {
                        Text("无法保存本地阅读状态，请重试。")
                            .foregroundStyle(.red)
                        Button("重试保存阅读状态") { inbox.markRead(item) }
                    }
                    Text(AnnouncementText.format(item.body))
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .environment(\.openURL, OpenURLAction { url in
                            guard AnnouncementText.isWebURL(url) else { return .discarded }
                            openURL(url)
                            return .handled
                        })
                }
                .padding(20)
            } else {
                Text("这条公告已更新或删除，请返回公告列表查看。")
                    .foregroundStyle(.secondary)
                    .padding(20)
            }
        }
        .navigationTitle("公告详情")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear {
            if let item { inbox.markRead(item) }
        }
    }
}
