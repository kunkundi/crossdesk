import SwiftUI
import UIKit
import UniformTypeIdentifiers

private enum RemoteKeyboardPalette {
    static let background = Color(red: 0.82, green: 0.84, blue: 0.87)
    static let characterKey = Color.white
    static let specialKey = Color(red: 0.68, green: 0.71, blue: 0.75)
    static let accentKey = Color(red: 0.04, green: 0.48, blue: 1.00)
    static let keyText = Color.black.opacity(0.88)
}

private enum RemoteKeyboardMetrics {
    static let rowHeight: CGFloat = 31
    static let keySpacing: CGFloat = 3
    static let rowSpacing: CGFloat = 3
    static let cornerRadius: CGFloat = 5
    static let panelPadding: CGFloat = 5
    static let dragHandleHeight: CGFloat = 10
}

private enum RemoteKeyboardMode: String {
    case system
    case computer
}

private struct RemoteViewportState: Equatable {
    var scale: CGFloat = 1
    var offset = CGSize.zero
}

struct RemoteSessionView: View {
    @ObservedObject var session: RemoteSessionModel
    @State private var keyboardInputVisible = false
    @State private var keyboardMode: RemoteKeyboardMode = .system
    @State private var viewport = RemoteViewportState()
    @State private var showingFileImporter = false
    @State private var disconnectConfirmationVisible = false
    @State private var cursorPosition: CGPoint?
    @State private var statusMenuVisible = false
    @State private var statusOrbCenter: CGPoint?
    @State private var keyboardPanelCenter: CGPoint?
    @GestureState private var statusOrbDragTranslation = CGSize.zero
    @GestureState private var keyboardDragTranslation = CGSize.zero

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            GeometryReader { proxy in
                ZStack(alignment: .topLeading) {
                    if let videoRect = RemoteVideoGeometry.aspectFitRect(
                        containerSize: proxy.size,
                        videoSize: session.displayGeometrySize
                    ) {
                        ZStack(alignment: .topLeading) {
                            NativeVideoView(pixelBuffer: session.pixelBuffer)

                            if let cursorPosition,
                               session.pixelBuffer != nil {
                                RemoteCursorOverlay(
                                    normalizedPosition: cursorPosition,
                                    normalizedVisualOffset:
                                        session.remoteCursorVisualOffset,
                                    contentSize: videoRect.size,
                                    viewportScale: viewport.scale,
                                    shape: visibleCursorShape
                                )
                                .allowsHitTesting(false)
                            }
                        }
                            .frame(width: videoRect.width,
                                   height: videoRect.height)
                            .position(x: videoRect.midX, y: videoRect.midY)
                            .scaleEffect(viewport.scale, anchor: .center)
                            .offset(viewport.offset)
                    }

                    RemoteTouchInputView(
                        videoSize: session.displayGeometrySize,
                        controlMode: session.mouseControlMode,
                        remoteCursorPosition: session.remoteCursorPosition,
                        viewportScale: viewport.scale,
                        viewportOffset: viewport.offset,
                        viewportChanged: { scale, offset in
                            // Scale and offset describe one affine transform.
                            // Publish them atomically so all layers observe the
                            // same viewport.
                            viewport = RemoteViewportState(scale: scale,
                                                           offset: offset)
                        },
                        move: { x, y in
                            cursorPosition = CGPoint(x: CGFloat(x), y: CGFloat(y))
                            session.bridge.sendPointer(x: x, y: y, action: .move)
                        },
                        leftDown: { x, y in
                            session.bridge.sendPointer(x: x, y: y, action: .leftDown)
                        },
                        leftUp: { x, y in
                            session.bridge.sendPointer(x: x, y: y, action: .leftUp)
                        },
                        rightClick: { x, y in
                            session.bridge.sendPointer(x: x, y: y, action: .rightDown)
                            session.bridge.sendPointer(x: x, y: y, action: .rightUp)
                        },
                        scroll: { x, y, dx, dy in
                            cursorPosition = CGPoint(x: CGFloat(x), y: CGFloat(y))
                            session.bridge.sendScrollX(x, y: y,
                                                       deltaX: dx, deltaY: dy)
                        }
                    )
                    .frame(width: proxy.size.width, height: proxy.size.height)
                    .allowsHitTesting(session.pixelBuffer != nil)

                }
                .frame(width: proxy.size.width, height: proxy.size.height)
            }
            .ignoresSafeArea()
            .transaction { transaction in
                transaction.animation = nil
                transaction.disablesAnimations = true
            }

            if session.pixelBuffer == nil {
                VStack(spacing: 12) {
                    ProgressView()
                        .tint(.white)
                    Text(session.videoStatus)
                        .font(.callout.monospacedDigit())
                        .foregroundStyle(.white)
                }
                .padding(20)
                .background(.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 14))
                .allowsHitTesting(true)
            }

            if keyboardInputVisible {
                draggableKeyboard
                .zIndex(20)
            }

            floatingStatusControls
                .zIndex(30)

            if disconnectConfirmationVisible {
                disconnectConfirmationOverlay
                    .zIndex(50)
            }
        }
        .onDisappear {
            keyboardInputVisible = false
            viewport = RemoteViewportState()
            cursorPosition = nil
            statusMenuVisible = false
            statusOrbCenter = nil
            keyboardPanelCenter = nil
        }
        .onChange(of: session.selectedDisplay) { _ in
            cursorPosition = nil
            viewport = RemoteViewportState()
        }
        .onChange(of: session.mouseControlMode) { _ in
            cursorPosition = session.remoteCursorPosition
        }
        .onChange(of: session.remoteCursorPosition) { position in
            guard let position else { return }
            cursorPosition = position
        }
        .fileImporter(isPresented: $showingFileImporter,
                      allowedContentTypes: [.item],
                      allowsMultipleSelection: false) { result in
            if case let .success(urls) = result, let url = urls.first {
                session.sendFile(url)
            }
        }
    }

    private var disconnectConfirmationOverlay: some View {
        GeometryReader { proxy in
            ZStack {
                Color.black.opacity(0.28)
                    .ignoresSafeArea()
                    .contentShape(Rectangle())
                    .onTapGesture {
                        withAnimation(.easeOut(duration: 0.16)) {
                            disconnectConfirmationVisible = false
                        }
                    }

                VStack(spacing: 0) {
                    VStack(spacing: 4) {
                        Text("断开远程连接？")
                            .font(.headline)
                            .foregroundStyle(Color.black.opacity(0.9))

                        Text("断开后将返回首页")
                            .font(.subheadline)
                            .foregroundStyle(Color.black.opacity(0.55))
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)

                    Divider()

                    Button {
                        disconnectConfirmationVisible = false
                        session.disconnect()
                    } label: {
                        Text("断开连接")
                            .font(.body.weight(.semibold))
                            .foregroundStyle(Color.red)
                            .frame(maxWidth: .infinity)
                            .frame(height: 45)
                    }

                    Divider()

                    Button {
                        withAnimation(.easeOut(duration: 0.16)) {
                            disconnectConfirmationVisible = false
                        }
                    } label: {
                        Text("取消")
                            .font(.body.weight(.semibold))
                            .foregroundStyle(Color.blue)
                            .frame(maxWidth: .infinity)
                            .frame(height: 45)
                    }
                }
                .background(
                    Color.white,
                    in: RoundedRectangle(cornerRadius: 16,
                                         style: .continuous)
                )
                .clipShape(RoundedRectangle(cornerRadius: 16,
                                            style: .continuous))
                .shadow(color: .black.opacity(0.2), radius: 18, y: 7)
                .frame(width: min(250, max(220, proxy.size.width - 32)))
                .position(x: proxy.size.width / 2,
                          y: proxy.size.height / 2)
                .transition(.scale(scale: 0.96).combined(with: .opacity))
            }
            .frame(width: proxy.size.width, height: proxy.size.height)
        }
        .ignoresSafeArea()
    }

    private var floatingStatusControls: some View {
        GeometryReader { proxy in
            let containerSize = proxy.size
            let restingOrbCenter = boundedOrbCenter(in: containerSize)
            let orbCenter = constrainedOrbCenter(
                CGPoint(x: restingOrbCenter.x + statusOrbDragTranslation.width,
                        y: restingOrbCenter.y + statusOrbDragTranslation.height),
                in: containerSize
            )
            let panelSize = CGSize(
                width: min(340, max(280, containerSize.width - 160)),
                height: min(250, max(220, containerSize.height - 48))
            )
            let panelCenter = floatingPanelCenter(
                orbCenter: orbCenter,
                panelSize: panelSize,
                containerSize: containerSize
            )

            ZStack {
                if statusMenuVisible {
                    Color.clear
                        .contentShape(Rectangle())
                        .onTapGesture {
                            withAnimation(.easeOut(duration: 0.14)) {
                                statusMenuVisible = false
                            }
                        }

                    FloatingSessionMenu(
                        session: session,
                        showKeyboard: {
                            statusMenuVisible = false
                            keyboardInputVisible.toggle()
                        },
                        chooseFile: {
                            statusMenuVisible = false
                            showingFileImporter = true
                        },
                        close: {
                            withAnimation(.easeOut(duration: 0.14)) {
                                statusMenuVisible = false
                            }
                        },
                        disconnect: {
                            statusMenuVisible = false
                            keyboardInputVisible = false
                            withAnimation(.easeOut(duration: 0.18)) {
                                disconnectConfirmationVisible = true
                            }
                        }
                    )
                    .frame(width: panelSize.width, height: panelSize.height)
                    .position(panelCenter)
                    .transition(.scale(scale: 0.94, anchor: .topTrailing)
                        .combined(with: .opacity))
                }

                CrossDeskStatusOrb(isExpanded: statusMenuVisible)
                    .frame(width: 54, height: 54)
                    .contentShape(Circle())
                    .position(orbCenter)
                    .onTapGesture {
                        withAnimation(.spring(response: 0.24,
                                              dampingFraction: 0.84)) {
                            statusMenuVisible.toggle()
                        }
                    }
                    .gesture(
                        DragGesture(minimumDistance: 1)
                            .updating($statusOrbDragTranslation) { value, state, _ in
                                state = value.translation
                            }
                            .onChanged { _ in
                                if statusMenuVisible {
                                    statusMenuVisible = false
                                }
                            }
                            .onEnded { value in
                                statusOrbCenter = constrainedOrbCenter(
                                    CGPoint(x: restingOrbCenter.x + value.translation.width,
                                            y: restingOrbCenter.y + value.translation.height),
                                    in: containerSize
                                )
                            }
                    )
            }
        }
        .ignoresSafeArea()
    }

    private var draggableKeyboard: some View {
        GeometryReader { proxy in
            let containerSize = proxy.size
            let panelSize = keyboardPanelSize(in: containerSize)
            let restingCenter = constrainedKeyboardCenter(
                keyboardPanelCenter ?? defaultKeyboardCenter(
                    in: containerSize,
                    panelSize: panelSize
                ),
                in: containerSize,
                panelSize: panelSize
            )
            let visibleCenter = constrainedKeyboardCenter(
                CGPoint(
                    x: restingCenter.x + keyboardDragTranslation.width,
                    y: restingCenter.y + keyboardDragTranslation.height
                ),
                in: containerSize,
                panelSize: panelSize
            )

            remoteKeyboardPanel(containerSize: containerSize,
                                panelSize: panelSize)
                .frame(width: panelSize.width, height: panelSize.height)
                .position(restingCenter)
                .offset(x: visibleCenter.x - restingCenter.x,
                        y: visibleCenter.y - restingCenter.y)
                .transaction { transaction in
                    transaction.animation = nil
                }
        }
    }

    private func remoteKeyboardPanel(containerSize: CGSize,
                                     panelSize: CGSize) -> some View {
        VStack(spacing: RemoteKeyboardMetrics.rowSpacing) {
            Capsule()
                .fill(Color.black.opacity(0.28))
                .frame(width: 38, height: 4)
                .frame(maxWidth: .infinity)
                .frame(height: RemoteKeyboardMetrics.dragHandleHeight)
                .contentShape(Rectangle())
                .gesture(keyboardDragGesture(containerSize: containerSize,
                                             panelSize: panelSize))
                .accessibilityLabel("拖动键盘")

            Group {
                if keyboardMode == .system {
                    RemoteSystemKeyboard(
                        session: session,
                        switchKeyboard: { keyboardMode = .computer },
                        dismiss: { keyboardInputVisible = false }
                    )
                } else {
                    RemoteComputerKeyboard(
                        session: session,
                        switchKeyboard: { keyboardMode = .system },
                        dismiss: { keyboardInputVisible = false }
                    )
                }
            }
        }
        .padding(RemoteKeyboardMetrics.panelPadding)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(RemoteKeyboardPalette.background,
                    in: RoundedRectangle(cornerRadius: 10, style: .continuous))
        .shadow(color: .black.opacity(0.2), radius: 10, y: 4)
    }

    private func keyboardPanelSize(in containerSize: CGSize) -> CGSize {
        let rowCount: CGFloat = keyboardMode == .system ? 4 : 6
        let keyboardHeight = rowCount * RemoteKeyboardMetrics.rowHeight +
            (rowCount - 1) * RemoteKeyboardMetrics.rowSpacing
        let height = keyboardHeight +
            RemoteKeyboardMetrics.dragHandleHeight +
            RemoteKeyboardMetrics.rowSpacing +
            RemoteKeyboardMetrics.panelPadding * 2
        return CGSize(width: min(780, max(0, containerSize.width - 12)),
                      height: height)
    }

    private func defaultKeyboardCenter(in containerSize: CGSize,
                                       panelSize: CGSize) -> CGPoint {
        CGPoint(x: containerSize.width / 2,
                y: containerSize.height - panelSize.height / 2 - 6)
    }

    private func constrainedKeyboardCenter(_ point: CGPoint,
                                           in containerSize: CGSize,
                                           panelSize: CGSize) -> CGPoint {
        let margin: CGFloat = 6
        let minimumX = panelSize.width / 2 + margin
        let maximumX = max(minimumX,
                           containerSize.width - panelSize.width / 2 - margin)
        let minimumY = panelSize.height / 2 + margin
        let maximumY = max(minimumY,
                           containerSize.height - panelSize.height / 2 - margin)
        return CGPoint(x: min(max(point.x, minimumX), maximumX),
                       y: min(max(point.y, minimumY), maximumY))
    }

    private func keyboardDragGesture(containerSize: CGSize,
                                     panelSize: CGSize) -> some Gesture {
        DragGesture(minimumDistance: 1, coordinateSpace: .global)
            .updating($keyboardDragTranslation) { value, state, _ in
                state = value.translation
            }
            .onEnded { value in
                let restingCenter = constrainedKeyboardCenter(
                    keyboardPanelCenter ?? defaultKeyboardCenter(
                        in: containerSize,
                        panelSize: panelSize
                    ),
                    in: containerSize,
                    panelSize: panelSize
                )
                keyboardPanelCenter = constrainedKeyboardCenter(
                    CGPoint(x: restingCenter.x + value.translation.width,
                            y: restingCenter.y + value.translation.height),
                    in: containerSize,
                    panelSize: panelSize
                )
            }
    }

    private func boundedOrbCenter(in size: CGSize) -> CGPoint {
        let initial = statusOrbCenter ?? CGPoint(x: size.width - 39, y: 44)
        return constrainedOrbCenter(initial, in: size)
    }

    private func constrainedOrbCenter(_ point: CGPoint, in size: CGSize) -> CGPoint {
        let horizontalMargin: CGFloat = 39
        let verticalMargin: CGFloat = 39
        return CGPoint(
            x: min(max(point.x, horizontalMargin),
                   max(horizontalMargin, size.width - horizontalMargin)),
            y: min(max(point.y, verticalMargin),
                   max(verticalMargin, size.height - verticalMargin))
        )
    }

    private func floatingPanelCenter(orbCenter: CGPoint,
                                     panelSize: CGSize,
                                     containerSize: CGSize) -> CGPoint {
        let gap: CGFloat = 12
        let orbRadius: CGFloat = 27
        let horizontalPadding: CGFloat = 8
        let verticalPadding: CGFloat = 8
        let preferredX = orbCenter.x > containerSize.width / 2
            ? orbCenter.x - orbRadius - gap - panelSize.width / 2
            : orbCenter.x + orbRadius + gap + panelSize.width / 2
        return CGPoint(
            x: min(max(preferredX, panelSize.width / 2 + horizontalPadding),
                   containerSize.width - panelSize.width / 2 - horizontalPadding),
            y: min(max(orbCenter.y, panelSize.height / 2 + verticalPadding),
                   containerSize.height - panelSize.height / 2 - verticalPadding)
        )
    }

    private var visibleCursorShape: Int {
        guard session.hasRemoteCursorState,
              session.remoteCursorVisible,
              session.remoteCursorShape != 1 else {
            // The desktop hides its native cursor from the captured frame and
            // can therefore report a hidden cursor while remote input is
            // active. Keep the iOS-side touch cursor visible in that state.
            return 0
        }
        return session.remoteCursorShape
    }

}

private enum RemoteSystemKeyboardPage {
    case letters
    case numbers
    case symbols
}

private enum RemoteSystemKeyAction {
    case text(String)
    case shift
    case backspace
    case returnKey
    case page(RemoteSystemKeyboardPage)
    case switchKeyboard
    case dismiss
}

private enum RemoteSystemKeyStyle {
    case character
    case special
    case accent
}

private struct RemoteKeyboardKeyCap: View {
    let title: String?
    let symbol: String?
    let style: RemoteSystemKeyStyle
    let active: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Group {
                if let symbol {
                    Image(systemName: symbol)
                        .font(.system(size: 14, weight: .semibold))
                } else {
                    Text(title ?? "")
                        .font(.system(size: labelFontSize,
                                      weight: .medium,
                                      design: .rounded))
                        .multilineTextAlignment(.center)
                        .lineLimit(2)
                        .minimumScaleFactor(0.62)
                }
            }
            .foregroundStyle(usesAccentColor
                             ? Color.white : RemoteKeyboardPalette.keyText)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(backgroundColor,
                        in: RoundedRectangle(
                            cornerRadius: RemoteKeyboardMetrics.cornerRadius,
                            style: .continuous
                        ))
            .shadow(color: .black.opacity(0.14), radius: 0.4, y: 0.8)
        }
        .buttonStyle(RemoteKeyboardPressStyle())
    }

    private var labelFontSize: CGFloat {
        guard let title else { return 14 }
        if title.contains("\n") { return 9 }
        switch title.count {
        case 0...1: return 14
        case 2...3: return 11.5
        default: return 10
        }
    }

    private var usesAccentColor: Bool {
        if active { return true }
        if case .accent = style { return true }
        return false
    }

    private var backgroundColor: Color {
        if usesAccentColor { return RemoteKeyboardPalette.accentKey }
        switch style {
        case .character: return RemoteKeyboardPalette.characterKey
        case .special: return RemoteKeyboardPalette.specialKey
        case .accent: return RemoteKeyboardPalette.accentKey
        }
    }
}

private struct RemoteKeyboardPressStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .brightness(configuration.isPressed ? -0.08 : 0)
            .scaleEffect(configuration.isPressed ? 0.97 : 1)
    }
}

private struct RemoteSystemKey {
    let title: String?
    let symbol: String?
    let width: CGFloat
    let style: RemoteSystemKeyStyle
    let action: RemoteSystemKeyAction

    init(_ title: String,
         width: CGFloat = 1,
         style: RemoteSystemKeyStyle = .character,
         action: RemoteSystemKeyAction) {
        self.title = title
        self.symbol = nil
        self.width = width
        self.style = style
        self.action = action
    }

    init(symbol: String,
         width: CGFloat = 1,
         style: RemoteSystemKeyStyle = .special,
         action: RemoteSystemKeyAction) {
        self.title = nil
        self.symbol = symbol
        self.width = width
        self.style = style
        self.action = action
    }

    var isShift: Bool {
        if case .shift = action { return true }
        return false
    }

    var isLetter: Bool {
        guard case let .text(value) = action else { return false }
        return value.rangeOfCharacter(from: .letters) != nil
    }
}

private struct RemoteSystemKeyboard: View {
    @ObservedObject var session: RemoteSessionModel
    let switchKeyboard: () -> Void
    let dismiss: () -> Void

    @State private var page: RemoteSystemKeyboardPage = .letters
    @State private var shiftEnabled = false

    var body: some View {
        VStack(spacing: RemoteKeyboardMetrics.rowSpacing) {
            ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                RemoteSystemKeyboardRow(
                    keys: row,
                    shiftEnabled: shiftEnabled,
                    action: handleKey
                )
            }
        }
    }

    private var rows: [[RemoteSystemKey]] {
        switch page {
        case .letters:
            return letterRows
        case .numbers:
            return numberRows
        case .symbols:
            return symbolRows
        }
    }

    private var letterRows: [[RemoteSystemKey]] {
        [
            textKeys("qwertyuiop"),
            textKeys("asdfghjkl"),
            [RemoteSystemKey(symbol: "shift",
                             width: 1.55,
                             action: .shift)] +
                textKeys("zxcvbnm") +
                [RemoteSystemKey(symbol: "delete.left",
                                 width: 1.55,
                                 action: .backspace)],
            bottomRow(pageTitle: "123", targetPage: .numbers)
        ]
    }

    private var numberRows: [[RemoteSystemKey]] {
        [
            valueKeys(["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"]),
            valueKeys(["-", "/", ":", ";", "(", ")", "$", "&", "@", "\""]),
            [RemoteSystemKey("#+=", width: 1.55, style: .special,
                             action: .page(.symbols))] +
                valueKeys([".", ",", "?", "!", "'"]) +
                [RemoteSystemKey(symbol: "delete.left", width: 1.55,
                                 action: .backspace)],
            bottomRow(pageTitle: "ABC", targetPage: .letters)
        ]
    }

    private var symbolRows: [[RemoteSystemKey]] {
        [
            valueKeys(["[", "]", "{", "}", "#", "%", "^", "*", "+", "="]),
            valueKeys(["_", "\\", "|", "~", "<", ">", "$", "&", "@", "`"]),
            [RemoteSystemKey("123", width: 1.55, style: .special,
                             action: .page(.numbers))] +
                valueKeys([".", ",", "?", "!", "'"]) +
                [RemoteSystemKey(symbol: "delete.left", width: 1.55,
                                 action: .backspace)],
            bottomRow(pageTitle: "ABC", targetPage: .letters)
        ]
    }

    private func textKeys(_ letters: String) -> [RemoteSystemKey] {
        letters.map { character in
            let value = String(character)
            return RemoteSystemKey(value, action: .text(value))
        }
    }

    private func valueKeys(_ values: [String]) -> [RemoteSystemKey] {
        values.map { RemoteSystemKey($0, action: .text($0)) }
    }

    private func bottomRow(pageTitle: String,
                           targetPage: RemoteSystemKeyboardPage)
        -> [RemoteSystemKey] {
        [
            RemoteSystemKey(pageTitle, width: 1.45, style: .special,
                            action: .page(targetPage)),
            RemoteSystemKey(symbol: "arrow.left.arrow.right", width: 1.2,
                            action: .switchKeyboard),
            RemoteSystemKey("空格", width: 5, action: .text(" ")),
            RemoteSystemKey("回车", width: 1.55, style: .accent,
                            action: .returnKey),
            RemoteSystemKey(symbol: "keyboard.chevron.compact.down", width: 1.2,
                            action: .dismiss)
        ]
    }

    private func handleKey(_ key: RemoteSystemKey) {
        switch key.action {
        case let .text(value):
            let isLetter = value.rangeOfCharacter(from: .letters) != nil
            session.bridge.sendText(shiftEnabled && isLetter
                                    ? value.uppercased() : value)
        case .shift:
            withTransaction(Transaction(animation: nil)) {
                shiftEnabled.toggle()
            }
        case .backspace:
            session.sendKeyStroke(0x08)
        case .returnKey:
            session.sendKeyStroke(0x0D)
        case let .page(newPage):
            page = newPage
        case .switchKeyboard:
            switchKeyboard()
        case .dismiss:
            dismiss()
        }
    }
}

private struct RemoteSystemKeyboardRow: View {
    let keys: [RemoteSystemKey]
    let shiftEnabled: Bool
    let action: (RemoteSystemKey) -> Void

    var body: some View {
        GeometryReader { proxy in
            let spacing = RemoteKeyboardMetrics.keySpacing
            let totalWidth = keys.reduce(CGFloat.zero) { $0 + $1.width }
            let keySpace = max(0, proxy.size.width -
                               CGFloat(max(0, keys.count - 1)) * spacing)

            HStack(spacing: spacing) {
                ForEach(Array(keys.enumerated()), id: \.offset) { _, key in
                    let isActive = key.isShift && shiftEnabled
                    let visibleTitle = shiftEnabled && key.isLetter
                        ? key.title?.uppercased() : key.title
                    let visibleSymbol = isActive && key.isShift
                        ? "shift.fill" : key.symbol
                    RemoteKeyboardKeyCap(
                        title: visibleTitle,
                        symbol: visibleSymbol,
                        style: key.style,
                        active: isActive
                    ) {
                        action(key)
                    }
                    .id("\(visibleTitle ?? visibleSymbol ?? "key")-\(isActive)")
                    .frame(width: keySpace * key.width / totalWidth)
                    .accessibilityLabel(visibleTitle ?? "键盘操作")
                }
            }
        }
        .frame(height: RemoteKeyboardMetrics.rowHeight)
    }
}

private enum RemoteComputerKeyAction {
    case stroke(UInt)
    case modifier(UInt)
    case switchKeyboard
    case dismiss
}

private struct RemoteComputerKey {
    let title: String?
    let symbol: String?
    let width: CGFloat
    let style: RemoteSystemKeyStyle
    let action: RemoteComputerKeyAction

    init(_ title: String,
         _ keyCode: UInt,
         width: CGFloat = 1,
         isModifier: Bool = false,
         style: RemoteSystemKeyStyle? = nil) {
        self.title = title
        self.symbol = nil
        self.width = width
        self.style = style ?? (isModifier ? .special : .character)
        self.action = isModifier ? .modifier(keyCode) : .stroke(keyCode)
    }

    init(_ title: String,
         width: CGFloat = 1,
         style: RemoteSystemKeyStyle = .special,
         action: RemoteComputerKeyAction) {
        self.title = title
        self.symbol = nil
        self.width = width
        self.style = style
        self.action = action
    }

    init(symbol: String,
         _ keyCode: UInt,
         width: CGFloat = 1,
         isModifier: Bool = false,
         style: RemoteSystemKeyStyle? = nil) {
        self.title = nil
        self.symbol = symbol
        self.width = width
        self.style = style ?? (isModifier ? .special : .character)
        self.action = isModifier ? .modifier(keyCode) : .stroke(keyCode)
    }

    init(symbol: String,
         width: CGFloat = 1,
         style: RemoteSystemKeyStyle = .special,
         action: RemoteComputerKeyAction) {
        self.title = nil
        self.symbol = symbol
        self.width = width
        self.style = style
        self.action = action
    }

    var modifierCode: UInt? {
        if case let .modifier(keyCode) = action { return keyCode }
        return nil
    }
}

private struct RemoteComputerKeyboard: View {
    @ObservedObject var session: RemoteSessionModel
    let switchKeyboard: () -> Void
    let dismiss: () -> Void
    @State private var activeModifiers: Set<UInt> = []

    private static let rows: [[RemoteComputerKey]] = [
        [
            .init("Esc", 0x1B, width: 1.2),
            .init("F1", 0x70), .init("F2", 0x71), .init("F3", 0x72),
            .init("F4", 0x73), .init("F5", 0x74), .init("F6", 0x75),
            .init("F7", 0x76), .init("F8", 0x77), .init("F9", 0x78),
            .init("F10", 0x79), .init("F11", 0x7A), .init("F12", 0x7B),
            .init("Del", 0x2E, width: 1.2)
        ],
        [
            .init("~\n`", 0xC0),
            .init("!\n1", 0x31), .init("@\n2", 0x32),
            .init("#\n3", 0x33), .init("$\n4", 0x34),
            .init("%\n5", 0x35), .init("^\n6", 0x36),
            .init("&\n7", 0x37), .init("*\n8", 0x38),
            .init("(\n9", 0x39), .init(")\n0", 0x30),
            .init("_\n-", 0xBD), .init("+\n=", 0xBB),
            .init(symbol: "delete.left", 0x08, width: 1.65,
                  style: .special)
        ],
        [
            .init("Tab", 0x09, width: 1.45, style: .special),
            .init("Q", 0x51), .init("W", 0x57), .init("E", 0x45),
            .init("R", 0x52), .init("T", 0x54), .init("Y", 0x59),
            .init("U", 0x55), .init("I", 0x49), .init("O", 0x4F),
            .init("P", 0x50), .init("{\n[", 0xDB),
            .init("}\n]", 0xDD), .init("|\n\\", 0xDC, width: 1.25)
        ],
        [
            .init("Caps", 0x14, width: 1.7, style: .special),
            .init("A", 0x41), .init("S", 0x53), .init("D", 0x44),
            .init("F", 0x46), .init("G", 0x47), .init("H", 0x48),
            .init("J", 0x4A), .init("K", 0x4B), .init("L", 0x4C),
            .init(":\n;", 0xBA), .init("\"\n'", 0xDE),
            .init("回车", 0x0D, width: 1.9, style: .accent)
        ],
        [
            .init(symbol: "shift", 0x10, width: 2.15, isModifier: true),
            .init("Z", 0x5A), .init("X", 0x58), .init("C", 0x43),
            .init("V", 0x56), .init("B", 0x42), .init("N", 0x4E),
            .init("M", 0x4D), .init("<\n,", 0xBC),
            .init(">\n.", 0xBE), .init("?\n/", 0xBF),
            .init(symbol: "shift", 0x10, width: 2.15, isModifier: true)
        ],
        [
            .init("Ctrl", 0x11, width: 1.35, isModifier: true),
            .init("Win", 0x5B, width: 1.25, isModifier: true),
            .init("Alt", 0x12, width: 1.25, isModifier: true),
            .init("空格", 0x20, width: 3.8),
            .init("Alt", 0x12, width: 1.25, isModifier: true),
            .init("Ctrl", 0x11, width: 1.35, isModifier: true),
            .init("←", 0x25, style: .special),
            .init("↑", 0x26, style: .special),
            .init("↓", 0x28, style: .special),
            .init("→", 0x27, style: .special),
            .init(symbol: "arrow.left.arrow.right", width: 1.35,
                  action: .switchKeyboard),
            .init(symbol: "keyboard.chevron.compact.down", width: 1.35,
                  action: .dismiss)
        ]
    ]

    var body: some View {
        VStack(spacing: RemoteKeyboardMetrics.rowSpacing) {
            ForEach(Array(Self.rows.enumerated()), id: \.offset) { _, row in
                RemoteComputerKeyboardRow(
                    keys: row,
                    activeModifiers: activeModifiers,
                    action: handleKey
                )
            }
        }
        .onDisappear(perform: releaseModifiers)
    }

    private func handleKey(_ key: RemoteComputerKey) {
        switch key.action {
        case let .modifier(keyCode):
            if activeModifiers.contains(keyCode) {
                session.sendKeyState(keyCode, isDown: false)
                activeModifiers.remove(keyCode)
            } else {
                session.sendKeyState(keyCode, isDown: true)
                activeModifiers.insert(keyCode)
            }
        case let .stroke(keyCode):
            session.sendKeyStroke(keyCode)
        case .switchKeyboard:
            releaseModifiers()
            switchKeyboard()
        case .dismiss:
            releaseModifiers()
            dismiss()
        }
    }

    private func releaseModifiers() {
        for keyCode in activeModifiers {
            session.sendKeyState(keyCode, isDown: false)
        }
        activeModifiers.removeAll()
    }
}

private struct RemoteComputerKeyboardRow: View {
    let keys: [RemoteComputerKey]
    let activeModifiers: Set<UInt>
    let action: (RemoteComputerKey) -> Void

    var body: some View {
        GeometryReader { proxy in
            let spacing = RemoteKeyboardMetrics.keySpacing
            let totalWidth = keys.reduce(CGFloat.zero) { $0 + $1.width }
            let keySpace = max(0, proxy.size.width -
                               CGFloat(max(0, keys.count - 1)) * spacing)

            HStack(spacing: spacing) {
                ForEach(Array(keys.enumerated()), id: \.offset) { _, key in
                    let isActive = key.modifierCode.map(activeModifiers.contains) ?? false
                    let visibleSymbol = isActive && key.symbol == "shift"
                        ? "shift.fill" : key.symbol
                    RemoteKeyboardKeyCap(
                        title: key.title,
                        symbol: visibleSymbol,
                        style: key.style,
                        active: isActive
                    ) {
                        action(key)
                    }
                    .frame(width: keySpace * key.width / totalWidth)
                    .accessibilityLabel((key.title ?? "键盘操作")
                        .replacingOccurrences(of: "\n", with: " "))
                }
            }
        }
        .frame(height: RemoteKeyboardMetrics.rowHeight)
    }
}

private struct CrossDeskStatusOrb: View {
    let isExpanded: Bool

    private static let appIcon: UIImage? = {
        let primaryIcon = (Bundle.main.infoDictionary?["CFBundleIcons"]
            as? [String: Any])?["CFBundlePrimaryIcon"] as? [String: Any]
        let iconName = primaryIcon?["CFBundleIconName"] as? String
        let iconFiles = primaryIcon?["CFBundleIconFiles"] as? [String]
        return iconName.flatMap(UIImage.init(named:))
            ?? iconFiles?.last.flatMap(UIImage.init(named:))
            ?? UIImage(named: "AppIcon")
    }()

    var body: some View {
        ZStack(alignment: .bottomTrailing) {
            Circle()
                .fill(Color(.secondarySystemBackground).opacity(0.96))

            if let icon = Self.appIcon {
                Image(uiImage: icon)
                    .resizable()
                    .scaledToFill()
                    .clipShape(Circle())
                    .padding(4)
            } else {
                Image(systemName: "desktopcomputer")
                    .font(.system(size: 22, weight: .semibold))
                    .foregroundStyle(.blue)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .overlay {
            Circle()
                .stroke(Color.white.opacity(isExpanded ? 0.95 : 0.65),
                        lineWidth: isExpanded ? 2 : 1)
        }
        .compositingGroup()
        .accessibilityLabel(isExpanded ? "收起远程控制菜单" : "展开远程控制菜单")
    }
}

private struct FloatingSessionMenu: View {
    @ObservedObject var session: RemoteSessionModel
    let showKeyboard: () -> Void
    let chooseFile: () -> Void
    let close: () -> Void
    let disconnect: () -> Void

    private let columns = Array(repeating: GridItem(.flexible(), spacing: 7),
                                count: 3)

    var body: some View {
        VStack(spacing: 9) {
            HStack(spacing: 8) {
                Circle()
                    .fill(session.isConnected ? Color.green : Color.orange)
                    .frame(width: 9, height: 9)
                VStack(alignment: .leading, spacing: 1) {
                    Text(session.connectionStatus)
                        .font(.subheadline.weight(.semibold))
                    Text(statusDetail)
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button(action: close) {
                    Image(systemName: "xmark.circle.fill")
                        .font(.title3)
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
            }

            LazyVGrid(columns: columns, spacing: 7) {
                FloatingControlButton(title: "键盘", symbol: "keyboard",
                                      action: showKeyboard)

                Menu {
                    ForEach(Array(session.displays.enumerated()), id: \.offset) { index, name in
                        Button {
                            session.selectDisplay(index)
                        } label: {
                            if index == session.selectedDisplay {
                                Label(name, systemImage: "checkmark")
                            } else {
                                Text(name)
                            }
                        }
                    }
                } label: {
                    FloatingControlLabel(title: "显示器", symbol: "display")
                }
                .buttonStyle(.plain)

                FloatingControlButton(
                    title: session.audioEnabled ? "声音" : "静音",
                    symbol: session.audioEnabled ? "speaker.wave.2" : "speaker.slash",
                    action: session.toggleAudio
                )
                FloatingControlButton(
                    title: session.mouseControlMode == .relative ? "相对鼠标" : "绝对鼠标",
                    symbol: "computermouse",
                    action: {
                        session.mouseControlMode = session.mouseControlMode == .relative
                            ? .absolute : .relative
                    }
                )
                FloatingControlButton(title: "发送文件",
                                      symbol: "folder.badge.plus",
                                      action: chooseFile)
                FloatingControlButton(title: "Ctrl+Alt+Del",
                                      symbol: "lock.trianglebadge.exclamationmark",
                                      action: session.bridge.sendSecureAttentionSequence)
            }

            if !session.transferStatus.isEmpty || !session.clipboardStatus.isEmpty {
                HStack(spacing: 8) {
                    Text(!session.transferStatus.isEmpty
                         ? session.transferStatus : session.clipboardStatus)
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                    Spacer(minLength: 0)
                    if session.transferProgress > 0 && session.transferProgress < 1 {
                        ProgressView(value: session.transferProgress)
                            .frame(width: 60)
                    }
                    if let file = session.receivedFileURL {
                        ShareLink(item: file) {
                            Image(systemName: "square.and.arrow.up")
                        }
                    }
                }
            }

            Button(role: .destructive, action: disconnect) {
                Text("断开连接")
                    .font(.subheadline.weight(.semibold))
                    .frame(maxWidth: .infinity)
                    .frame(height: 34)
            }
            .buttonStyle(.borderedProminent)
            .tint(.red)
        }
        .padding(12)
        .background(Color.white,
                    in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .stroke(Color(.separator).opacity(0.24), lineWidth: 1)
        }
        .shadow(color: .black.opacity(0.34), radius: 18, y: 7)
        .environment(\.colorScheme, .light)
    }

    private var statusDetail: String {
        var components = [session.usingTURN ? "TURN" : "P2P",
                          session.formattedBitrate]
        if session.lossRate > 0 {
            components.append(String(format: "丢包 %.1f%%", session.lossRate * 100))
        }
        return components.joined(separator: " · ")
    }
}

private struct FloatingControlButton: View {
    let title: String
    let symbol: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            FloatingControlLabel(title: title, symbol: symbol)
        }
        .buttonStyle(.plain)
    }
}

private struct FloatingControlLabel: View {
    let title: String
    let symbol: String

    var body: some View {
        VStack(spacing: 4) {
            Image(systemName: symbol)
                .font(.system(size: 16, weight: .semibold))
            Text(title)
                .font(.system(size: 9.5, weight: .medium))
                .lineLimit(1)
                .minimumScaleFactor(0.72)
        }
        .foregroundStyle(.primary)
        .frame(maxWidth: .infinity)
        .frame(height: 52)
        .background(Color.primary.opacity(0.07),
                    in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }
}

private struct RemoteCursorOverlay: View {
    let normalizedPosition: CGPoint
    let normalizedVisualOffset: CGPoint
    let contentSize: CGSize
    let viewportScale: CGFloat
    let shape: Int

    var body: some View {
        let scale = max(viewportScale, 0.0001)
        let x = min(max(normalizedPosition.x + normalizedVisualOffset.x, 0), 1) *
            contentSize.width
        let y = min(max(normalizedPosition.y + normalizedVisualOffset.y, 0), 1) *
            contentSize.height
        let offset = RemoteCursorGlyph.hotspotOffset(for: shape)

        ZStack(alignment: .topLeading) {
            RemoteCursorGlyph(shape: shape)
                // The cursor lives inside the exact same transformed view as
                // the decoded frame, so its anchor cannot drift from video
                // because of an independently reconstructed transform. Undo
                // the outer zoom for the glyph itself and pre-divide the
                // hotspot offset so the cursor remains a constant screen size.
                .scaleEffect(1 / scale, anchor: .center)
                .position(x: x + offset.x / scale,
                          y: y + offset.y / scale)
        }
        .frame(width: contentSize.width, height: contentSize.height,
               alignment: .topLeading)
    }
}

private struct RemoteCursorGlyph: View {
    let shape: Int

    var body: some View {
        Group {
            if shape == 0 {
                ZStack {
                    RemoteCursorArrowShape()
                        .stroke(Color.black, lineWidth: 1.75)
                    RemoteCursorArrowShape()
                        .fill(Color.white)
                }
                .frame(width: 12, height: 15)
            } else if shape == 7 {
                ZStack {
                    RemoteIBeamShape()
                        .stroke(Color.black, lineWidth: 2.5)
                    RemoteIBeamShape()
                        .stroke(Color.white, lineWidth: 1)
                }
                .frame(width: 12, height: 15)
            } else {
                Image(systemName: availableSymbolName)
                    .font(.system(size: 10.5, weight: .black))
                    .symbolRenderingMode(.monochrome)
                    .foregroundStyle(.white)
                    .shadow(color: .black, radius: 0.75)
            }
        }
        .frame(width: 15, height: 15)
        .accessibilityHidden(true)
    }

    static func hotspotOffset(for shape: Int) -> CGPoint {
        switch shape {
        case 0:
            // The arrow path's visual tip is near (2.5, 0.6) inside its
            // 15-point container, rather than at the container origin.
            return CGPoint(x: 5, y: 7)
        case 3:
            return CGPoint(x: 4.5, y: 4.5)
        default:
            return .zero
        }
    }

    private var availableSymbolName: String {
        let desired: String
        switch shape {
        case 2: desired = "questionmark.circle.fill"
        case 3: desired = "hand.point.up.left.fill"
        case 4: desired = "arrow.triangle.2.circlepath"
        case 5: desired = "hourglass"
        case 6: desired = "scope"
        case 8: desired = "arrow.turn.up.right"
        case 9: desired = "plus.square.on.square"
        case 10: desired = "arrow.up.and.down.and.arrow.left.and.right"
        case 11, 12: desired = "nosign"
        case 13, 14: desired = "hand.raised.fill"
        case 15, 25: desired = "arrow.left.and.right"
        case 16, 26: desired = "arrow.up.and.down"
        case 17: desired = "arrow.up"
        case 18: desired = "arrow.right"
        case 19: desired = "arrow.down"
        case 20: desired = "arrow.left"
        case 21: desired = "arrow.up.right"
        case 22: desired = "arrow.up.left"
        case 23: desired = "arrow.down.right"
        case 24: desired = "arrow.down.left"
        case 27: desired = "arrow.up.right.and.arrow.down.left"
        case 28: desired = "arrow.up.left.and.arrow.down.right"
        default: desired = "arrow.up.left"
        }
        return UIImage(systemName: desired) == nil ? "arrow.up.left" : desired
    }
}

private struct RemoteCursorArrowShape: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.minX + rect.width * 0.08,
                              y: rect.minY + rect.height * 0.04))
        path.addLine(to: CGPoint(x: rect.minX + rect.width * 0.08,
                                 y: rect.minY + rect.height * 0.78))
        path.addLine(to: CGPoint(x: rect.minX + rect.width * 0.34,
                                 y: rect.minY + rect.height * 0.60))
        path.addLine(to: CGPoint(x: rect.minX + rect.width * 0.55,
                                 y: rect.minY + rect.height * 0.94))
        path.addLine(to: CGPoint(x: rect.minX + rect.width * 0.73,
                                 y: rect.minY + rect.height * 0.84))
        path.addLine(to: CGPoint(x: rect.minX + rect.width * 0.52,
                                 y: rect.minY + rect.height * 0.51))
        path.addLine(to: CGPoint(x: rect.minX + rect.width * 0.88,
                                 y: rect.minY + rect.height * 0.49))
        path.closeSubpath()
        return path
    }
}

private struct RemoteIBeamShape: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        let centerX = rect.midX
        let top = rect.minY + 2
        let bottom = rect.maxY - 2
        path.move(to: CGPoint(x: centerX, y: top))
        path.addLine(to: CGPoint(x: centerX, y: bottom))
        path.move(to: CGPoint(x: rect.minX + 5, y: top))
        path.addLine(to: CGPoint(x: rect.maxX - 5, y: top))
        path.move(to: CGPoint(x: rect.minX + 5, y: bottom))
        path.addLine(to: CGPoint(x: rect.maxX - 5, y: bottom))
        return path
    }
}
