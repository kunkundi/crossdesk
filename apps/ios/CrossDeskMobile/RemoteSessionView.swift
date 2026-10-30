import SwiftUI
import UIKit
import UniformTypeIdentifiers

private enum RemoteKeyboardPalette {
    static let background = adaptive(light: .systemGray4,
                                     dark: .secondarySystemBackground)
    static let characterKey = adaptive(light: .white,
                                       dark: UIColor(white: 0.36, alpha: 1))
    static let specialKey = adaptive(light: .systemGray2,
                                     dark: UIColor(white: 0.22, alpha: 1))
    static let pressedKey = adaptive(light: .systemGray3,
                                     dark: UIColor(white: 0.48, alpha: 1))
    static let keyText = Color(uiColor: .label)
    static let keyShadow = Color.black.opacity(0.3)

    private static func adaptive(light: UIColor, dark: UIColor) -> Color {
        Color(uiColor: UIColor { traits in
            traits.userInterfaceStyle == .dark ? dark : light
        })
    }
}

private enum RemoteKeyboardMetrics {
    static let keySpacing: CGFloat = 5
    static let rowSpacing: CGFloat = 6
    static let cornerRadius: CGFloat = 7
    static let panelPadding: CGFloat = 6
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
    @Environment(\.scenePhase) private var scenePhase
    @State private var mouseInput = RemoteMouseInput()
    @State private var virtualMouseVisible = false
    @StateObject private var systemKeyboardContext = RemoteKeyboardInputContext()
    @StateObject private var keyboardGeometry = RemoteKeyboardGeometry()
    @State private var keyboardInputVisible = false
    @State private var keyboardMode: RemoteKeyboardMode = .system
    @State private var animateSystemKeyboardPresentation = true
    @State private var waitingForSystemKeyboard = false
    @State private var viewport = RemoteViewportState()
    @State private var showingFileImporter = false
    @State private var disconnectConfirmationVisible = false
    @State private var cursorPosition: CGPoint?
    @State private var statusMenuVisible = false
    @State private var statusOrbCenter: CGPoint?
    @GestureState private var statusOrbDragTranslation = CGSize.zero

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
                            NativeVideoView(
                                pixelBuffer: session.pixelBuffer,
                                frameID: session.currentVideoFrameID,
                                captureUptime: session.frameCaptureUptime,
                                onFrameSubmitted: session.recordVideoFrameSubmission,
                                pictureInPicture: session.pictureInPicture
                            )

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
                        mouseInput: mouseInput,
                        videoSize: session.displayGeometrySize,
                        controlMode: session.mouseControlMode,
                        remoteCursorPosition: session.remoteCursorPosition,
                        remoteCursorPositionRevision: session.remoteCursorPositionRevision,
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
                            mouseInput.position(x: x, y: y)
                            cursorPosition = CGPoint(x: CGFloat(x), y: CGFloat(y))
                            session.bridge.sendPointer(x: x, y: y, action: .move)
                        },
                        leftDown: { x, y in
                            mouseInput.position(x: x, y: y)
                            mouseInput.setGestureLeft(true)
                        },
                        leftUp: { x, y in
                            mouseInput.position(x: x, y: y)
                            mouseInput.setGestureLeft(false)
                        },
                        rightClick: { x, y in
                            systemKeyboardContext.remoteFocusWillChange()
                            session.bridge.sendPointer(x: x, y: y, action: .rightDown)
                            session.bridge.sendPointer(x: x, y: y, action: .rightUp)
                        },
                        scroll: { x, y, dx, dy in
                            mouseInput.position(x: x, y: y)
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
                ZStack {
                    if keyboardMode == .system {
                        RemoteSystemKeyboard(session: session,
                            inputContext: systemKeyboardContext,
                            geometry: keyboardGeometry,
                            animatePresentation: animateSystemKeyboardPresentation,
                            didPresent: { waitingForSystemKeyboard = false },
                            switchKeyboard: { keyboardMode = .computer },
                            dismiss: { keyboardInputVisible = false })
                    }
                    if keyboardMode == .computer || waitingForSystemKeyboard {
                        dockedKeyboard
                    }
                }
                .zIndex(20)
            }

            floatingStatusControls
                .zIndex(30)

            if session.isConnected && session.privacyScreen.unlockNotice {
                privacyUnlockOverlay.zIndex(60)
            }
            if disconnectConfirmationVisible {
                disconnectConfirmationOverlay
                    .zIndex(50)
            }
        }
        .alert(NSLocalizedString("升级提示", comment: ""), isPresented: Binding(
            get: { !session.privacyScreen.unlockNotice && session.remoteUpdateMessage != nil },
            set: { if !$0 && !session.privacyScreen.unlockNotice { session.dismissRemoteUpdate() } }
        )) {
            Button(NSLocalizedString("知道了", comment: ""), role: .cancel) { session.dismissRemoteUpdate() }
        } message: {
            Text(session.remoteUpdateMessage ?? "")
        }
        .onValueChange(of: session.privacyScreen.unlockNotice) { visible in
            if visible {
                mouseInput.releaseAll()
                keyboardInputVisible = false
            }
        }
        .onAppear {
            mouseInput.send = { x, y, flag, wheel in
                cursorPosition = CGPoint(x: CGFloat(x), y: CGFloat(y))
                if flag == 1 || flag == 3 { systemKeyboardContext.remoteFocusWillChange() }
                switch flag {
                case 0: session.bridge.sendPointer(x: x, y: y, action: .move)
                case 1: session.bridge.sendPointer(x: x, y: y, action: .leftDown)
                case 2: session.bridge.sendPointer(x: x, y: y, action: .leftUp)
                case 3: session.bridge.sendPointer(x: x, y: y, action: .rightDown)
                case 4: session.bridge.sendPointer(x: x, y: y, action: .rightUp)
                case 7: session.bridge.sendScrollX(x, y: y, deltaX: 0, deltaY: wheel)
                default: break
                }
            }
            if let point = session.remoteCursorPosition {
                mouseInput.position(x: Float(point.x), y: Float(point.y))
            }
        }
        .onDisappear {
            mouseInput.releaseAll()
            mouseInput.send = nil
            virtualMouseVisible = false
            keyboardInputVisible = false
            viewport = RemoteViewportState()
            cursorPosition = nil
            statusMenuVisible = false
            statusOrbCenter = nil
        }
        .onValueChange(of: session.selectedDisplay) { _ in
            systemKeyboardContext.remoteFocusWillChange()
            mouseInput.releaseAll()
            mouseInput.position(x: 0.5, y: 0.5)
            cursorPosition = nil
            viewport = RemoteViewportState()
        }
        .onValueChange(of: session.mouseControlMode) { _ in
            mouseInput.releaseAll()
            cursorPosition = session.remoteCursorPosition
            if let point = cursorPosition {
                mouseInput.position(x: Float(point.x), y: Float(point.y))
            }
        }
        .onValueChange(of: scenePhase) { phase in
            if phase != .active { mouseInput.releaseAll() }
            if phase == .background { keyboardInputVisible = false }
        }
        .onValueChange(of: keyboardInputVisible) { visible in
            if !visible {
                animateSystemKeyboardPresentation = true
                waitingForSystemKeyboard = false
            }
        }
        .onValueChange(of: session.isConnected) { connected in
            if !connected {
                mouseInput.releaseAll()
                keyboardInputVisible = false
            }
        }
        .onValueChange(of: session.remoteCursorPositionRevision) { _ in
            guard !mouseInput.hasVirtualButton, let position = session.remoteCursorPosition else { return }
            mouseInput.position(x: Float(position.x), y: Float(position.y))
            cursorPosition = position
        }
        .fileImporter(isPresented: $showingFileImporter,
                      allowedContentTypes: [.item],
                      allowsMultipleSelection: false) { result in
            if case let .success(urls) = result, let url = urls.first {
                session.sendFile(url)
            } else if case .failure = result {
                session.fileImportFailed()
            }
        }
    }

    private var privacyUnlockOverlay: some View {
        ZStack {
            Color.black.opacity(0.28).ignoresSafeArea().contentShape(Rectangle()).onTapGesture { }
            VStack(spacing: 0) {
                VStack(spacing: 12) {
                    Text(NSLocalizedString("隐私屏已被解锁", comment: "")).font(.headline)
                    Text(NSLocalizedString("被控端已通过组合键解除隐私屏。点击“锁定”将重新开启隐私屏，并禁止本机使用组合键解锁。", comment: ""))
                        .font(.subheadline).foregroundStyle(.secondary)
                    let feedback = session.privacyScreen.feedback(at: session.privacyClock)
                    if !feedback.isEmpty { Text(feedback).font(.caption).foregroundStyle(.secondary) }
                }.padding(24)
                Divider()
                HStack(spacing: 0) {
                    Button(NSLocalizedString("确认", comment: "")) { session.privacyScreen.unlockNotice = false }
                        .frame(maxWidth: .infinity, minHeight: 48)
                    Divider().frame(height: 48)
                    Button(NSLocalizedString("锁定", comment: "")) { session.lockPrivacyScreen() }
                        .fontWeight(.semibold).frame(maxWidth: .infinity, minHeight: 48)
                        .disabled(!session.privacyScreen.canLock(at: session.privacyClock))
                }
            }
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
            .clipShape(RoundedRectangle(cornerRadius: 16))
            .shadow(color: .black.opacity(0.2), radius: 18, y: 7)
            .frame(maxWidth: 380).padding(20)
        }
        .accessibilityAddTraits(.isModal)
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
                        Text(NSLocalizedString("断开远程连接？", comment: ""))
                            .font(.headline)
                            .foregroundStyle(Color.black.opacity(0.9))

                        Text(NSLocalizedString("断开后将返回首页", comment: ""))
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
                        Text(NSLocalizedString("断开连接", comment: ""))
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
                        Text(NSLocalizedString("取消", comment: ""))
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
        // Keep the orb and expanded menu inside the safe area so their initial
        // positions and drag bounds clear the Dynamic Island in either orientation.
        GeometryReader { proxy in
            let containerSize = proxy.size
            let restingOrbCenter = boundedOrbCenter(in: containerSize)
            let orbCenter = constrainedOrbCenter(
                CGPoint(x: restingOrbCenter.x + statusOrbDragTranslation.width,
                        y: restingOrbCenter.y + statusOrbDragTranslation.height),
                in: containerSize
            )
            let menuColumns = containerSize.height < 400 && containerSize.width >= 400 ? 4 : 3
            let panelWidth = min(max(1, containerSize.width - 16), menuColumns == 4
                                 ? 380 : min(340, max(280, containerSize.width - 160)))

            ZStack(alignment: .topLeading) {
                if virtualMouseVisible && !statusMenuVisible && session.pixelBuffer != nil {
                    VirtualMouseBar(input: mouseInput)
                        .frame(width: 140, height: 48)
                        .position(virtualMouseCenter(orbCenter: orbCenter, in: containerSize))
                }
                if statusMenuVisible {
                    Color.clear
                        .contentShape(Rectangle())
                        .onTapGesture {
                            withAnimation(.easeOut(duration: 0.14)) {
                                statusMenuVisible = false
                            }
                        }
                }

                FloatingMenuLayout(orbCenter: orbCenter, panelWidth: panelWidth) {
                    if statusMenuVisible {
                        FloatingSessionMenu(
                            session: session,
                            columnCount: menuColumns,
                            virtualMouseVisible: virtualMouseVisible,
                            toggleMouse: {
                                mouseInput.releaseAll()
                                virtualMouseVisible.toggle()
                                statusMenuVisible = false
                            },
                            releaseMouse: mouseInput.releaseAll,
                            showKeyboard: {
                                statusMenuVisible = false
                                keyboardInputVisible.toggle()
                            },
                            chooseFile: {
                                statusMenuVisible = false
                                keyboardInputVisible = false
                                showingFileImporter = true
                            },
                            close: {
                                withAnimation(.easeOut(duration: 0.14)) {
                                    statusMenuVisible = false
                                }
                            },
                            disconnect: {
                                mouseInput.releaseAll()
                                statusMenuVisible = false
                                keyboardInputVisible = false
                                withAnimation(.easeOut(duration: 0.18)) {
                                    disconnectConfirmationVisible = true
                                }
                            }
                        )
                        .transition(.scale(scale: 0.94, anchor: .topTrailing)
                            .combined(with: .opacity))
                    }
                }
                .frame(width: containerSize.width, height: containerSize.height)
                .allowsHitTesting(statusMenuVisible)

                CrossDeskStatusOrb(isExpanded: statusMenuVisible)
                    .frame(width: 54, height: 54)
                    .contentShape(Circle())
                    .position(orbCenter)
                    .onTapGesture {
                        mouseInput.releaseAll()
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
                                mouseInput.releaseAll()
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
            .frame(width: containerSize.width, height: containerSize.height)
            .onValueChange(of: containerSize) { _ in mouseInput.releaseAll() }
        }
    }

    private func virtualMouseCenter(orbCenter: CGPoint, in size: CGSize) -> CGPoint {
        let halfWidth: CGFloat = 70
        let halfHeight: CGFloat = 24
        let reach: CGFloat = 27 + 6 + halfWidth
        var x = orbCenter.x - reach
        var y = orbCenter.y
        if x - halfWidth < 8 {
            x = orbCenter.x + reach
            if x + halfWidth > size.width - 8 {
                x = orbCenter.x
                y = orbCenter.y + 27 + 6 + halfHeight
                if y + halfHeight > size.height - 8 {
                    y = orbCenter.y - 27 - 6 - halfHeight
                }
            }
        }
        x = min(max(x, halfWidth + 8), max(halfWidth + 8, size.width - halfWidth - 8))
        if keyboardInputVisible && keyboardMode == .computer {
            let keyboardSize = keyboardPanelSize(in: size)
            let center = keyboardCenter(in: size, panelSize: keyboardSize)
            let keyboardRect = CGRect(x: center.x - keyboardSize.width / 2, y: center.y - keyboardSize.height / 2, width: keyboardSize.width, height: keyboardSize.height)
            if keyboardRect.intersects(CGRect(x: x - halfWidth, y: y - halfHeight, width: 140, height: 48)) {
                y = keyboardRect.minY - 6 - halfHeight
                if y - halfHeight < 8 { y = keyboardRect.maxY + 6 + halfHeight }
            }
        }
        return CGPoint(x: x, y: min(max(y, halfHeight + 8), max(halfHeight + 8, size.height - halfHeight - 8)))
    }

    private var dockedKeyboard: some View {
        GeometryReader { proxy in
            let containerSize = CGSize(width: proxy.size.width,
                                       height: proxy.size.height + proxy.safeAreaInsets.bottom)
            let windowSize = CGSize(
                width: proxy.size.width + proxy.safeAreaInsets.leading + proxy.safeAreaInsets.trailing,
                height: containerSize.height + proxy.safeAreaInsets.top)
            let panelSize = keyboardPanelSize(in: containerSize,
                                             bottomInset: proxy.safeAreaInsets.bottom,
                                             windowSize: windowSize)
            remoteKeyboardPanel(bottomInset: proxy.safeAreaInsets.bottom)
                .frame(width: panelSize.width, height: panelSize.height)
                .position(keyboardCenter(in: containerSize, panelSize: panelSize))
                .transaction { transaction in
                    transaction.animation = nil
                }
        }
        .ignoresSafeArea(.keyboard)
    }

    private func remoteKeyboardPanel(bottomInset: CGFloat) -> some View {
        RemoteComputerKeyboard(
            session: session,
            switchKeyboard: {
                animateSystemKeyboardPresentation = false
                waitingForSystemKeyboard = true
                keyboardMode = .system
            },
            dismiss: { keyboardInputVisible = false }
        )
        .padding(.bottom, bottomInset)
        .background(RemoteKeyboardPalette.background)
    }

    private func keyboardPanelSize(in containerSize: CGSize,
                                   bottomInset: CGFloat = 0,
                                   windowSize: CGSize? = nil) -> CGSize {
        CGSize(width: min(780, max(0, containerSize.width)),
               height: min(containerSize.height,
                           keyboardGeometry.panelHeight(in: windowSize ?? containerSize,
                                                        bottomInset: bottomInset)))
    }

    private func keyboardCenter(in containerSize: CGSize,
                                panelSize: CGSize) -> CGPoint {
        CGPoint(x: containerSize.width / 2,
                y: containerSize.height - panelSize.height / 2)
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

private enum RemoteKeyboardKeyStyle {
    case character
    case special
}

private struct RemoteKeyboardKeyCap: View {
    @Environment(\.verticalSizeClass) private var verticalSizeClass
    let title: String?
    let symbol: String?
    let style: RemoteKeyboardKeyStyle
    let active: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Group {
                if let symbol {
                    Image(systemName: symbol)
                        .font(.system(size: 17, weight: .regular))
                } else if let title, title.contains("\n") {
                    let legends = title.components(separatedBy: "\n")
                    if verticalSizeClass == .compact {
                        HStack(spacing: 3) {
                            Text(legends[1]).font(.system(size: 17))
                            Text(legends[0]).font(.system(size: 10))
                                .foregroundStyle(.secondary)
                        }
                    } else {
                        VStack(spacing: 0) {
                            Text(legends[0])
                                .font(.system(size: 10))
                                .foregroundStyle(.secondary)
                            Text(legends[1])
                                .font(.system(size: 18))
                        }
                    }
                } else {
                    Text(title ?? "")
                        .font(.system(size: labelFontSize,
                                      weight: .regular))
                        .multilineTextAlignment(.center)
                        .lineLimit(1)
                        .minimumScaleFactor(0.62)
                }
            }
            .foregroundStyle(active ? Color.black : RemoteKeyboardPalette.keyText)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
        }
        .buttonStyle(RemoteKeyboardPressStyle(backgroundColor: backgroundColor))
        .accessibilityAddTraits(active ? .isSelected : [])
    }

    private var labelFontSize: CGFloat {
        guard let title else { return 14 }
        switch title.count {
        case 0...1: return 20
        case 2...3: return 14
        default: return 14
        }
    }

    private var backgroundColor: Color {
        // Match the system keyboard's latched Shift appearance.
        if active { return .white }
        switch style {
        case .character: return RemoteKeyboardPalette.characterKey
        case .special: return RemoteKeyboardPalette.specialKey
        }
    }
}

private struct RemoteKeyboardPressStyle: ButtonStyle {
    let backgroundColor: Color

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background {
                RoundedRectangle(cornerRadius: RemoteKeyboardMetrics.cornerRadius,
                                 style: .continuous)
                    .fill(configuration.isPressed
                          ? RemoteKeyboardPalette.pressedKey : backgroundColor)
                    .shadow(color: RemoteKeyboardPalette.keyShadow, radius: 0, y: 1)
            }
    }
}

private final class RemoteKeyboardFeedback: ObservableObject {
    private var generator: UIImpactFeedbackGenerator?

    func prepare() {
        if generator == nil { generator = UIImpactFeedbackGenerator(style: .light) }
        generator?.prepare()
    }

    func keyPressed() {
        let generator = generator ?? UIImpactFeedbackGenerator(style: .light)
        self.generator = generator
        generator.impactOccurred(intensity: 1.0)
        // Keep rapid typing responsive without repeatedly allocating generators.
        generator.prepare()
    }

    func stop() {
        generator = nil
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
    let style: RemoteKeyboardKeyStyle
    let action: RemoteComputerKeyAction

    init(_ title: String,
         _ keyCode: UInt,
         width: CGFloat = 1,
         isModifier: Bool = false,
         style: RemoteKeyboardKeyStyle? = nil) {
        self.title = title
        self.symbol = nil
        self.width = width
        self.style = style ?? (isModifier ? .special : .character)
        self.action = isModifier ? .modifier(keyCode) : .stroke(keyCode)
    }

    init(_ title: String,
         width: CGFloat = 1,
         style: RemoteKeyboardKeyStyle = .special,
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
         style: RemoteKeyboardKeyStyle? = nil) {
        self.title = nil
        self.symbol = symbol
        self.width = width
        self.style = style ?? (isModifier ? .special : .character)
        self.action = isModifier ? .modifier(keyCode) : .stroke(keyCode)
    }

    init(symbol: String,
         width: CGFloat = 1,
         style: RemoteKeyboardKeyStyle = .special,
         action: RemoteComputerKeyAction) {
        self.title = nil
        self.symbol = symbol
        self.width = width
        self.style = style
        self.action = action
    }

    var accessibilityLabel: String {
        if let title { return title.replacingOccurrences(of: "\n", with: " ") }
        switch symbol {
        case "shift": return "Shift"
        case "delete.left": return NSLocalizedString("退格", comment: "")
        default: return NSLocalizedString("键盘操作", comment: "")
        }
    }

    var modifierCode: UInt? {
        if case let .modifier(keyCode) = action { return keyCode }
        return nil
    }
}

private enum RemoteComputerKeyboardPage: String, CaseIterable {
    case input = "字母数字"
    case controls = "符号功能"

    var rows: [[RemoteComputerKey]] {
        let shift = RemoteComputerKey(symbol: "shift", 0x10, isModifier: true)
        let backspace = RemoteComputerKey(symbol: "delete.left", 0x08, style: .special)
        switch self {
        case .input:
            return [
                [.init("!\n1", 0x31), .init("@\n2", 0x32), .init("#\n3", 0x33),
                 .init("$\n4", 0x34), .init("%\n5", 0x35), .init("^\n6", 0x36),
                 .init("&\n7", 0x37), .init("*\n8", 0x38), .init("(\n9", 0x39),
                 .init(")\n0", 0x30)],
                [.init("Q", 0x51), .init("W", 0x57), .init("E", 0x45),
                 .init("R", 0x52), .init("T", 0x54), .init("Y", 0x59),
                 .init("U", 0x55), .init("I", 0x49), .init("O", 0x4F),
                 .init("P", 0x50)],
                [.init("A", 0x41), .init("S", 0x53), .init("D", 0x44),
                 .init("F", 0x46), .init("G", 0x47), .init("H", 0x48),
                 .init("J", 0x4A), .init("K", 0x4B), .init("L", 0x4C)],
                [shift, .init("Z", 0x5A), .init("X", 0x58), .init("C", 0x43),
                 .init("V", 0x56), .init("B", 0x42), .init("N", 0x4E),
                 .init("M", 0x4D), backspace]
            ]
        case .controls:
            return [
                [.init("~\n`", 0xC0), .init("_\n-", 0xBD), .init("+\n=", 0xBB),
                 .init("{\n[", 0xDB), .init("}\n]", 0xDD), .init("|\n\\", 0xDC),
                 .init(":\n;", 0xBA), .init("\"\n'", 0xDE),
                 .init("<\n,", 0xBC), .init(">\n.", 0xBE), .init("?\n/", 0xBF)],
                [.init("F1", 0x70), .init("F2", 0x71), .init("F3", 0x72),
                 .init("F4", 0x73), .init("F5", 0x74), .init("F6", 0x75)],
                [.init("F7", 0x76), .init("F8", 0x77), .init("F9", 0x78),
                 .init("F10", 0x79), .init("F11", 0x7A), .init("F12", 0x7B)],
                [.init("Esc", 0x1B, style: .special), .init("Tab", 0x09, style: .special),
                 .init("Caps", 0x14), .init("Ins", 0x2D), .init("Del", 0x2E),
                 .init("Home", 0x24), .init("End", 0x23)],
                [shift, .init("←", 0x25), .init("↑", 0x26), .init("↓", 0x28),
                 .init("→", 0x27), .init("PgUp", 0x21), .init("PgDn", 0x22), backspace]
            ]
        }
    }

    func rows(compact: Bool) -> [[RemoteComputerKey]] {
        let rows = self.rows
        guard compact, self == .controls else { return rows }
        // Landscape has room for one full function-key row, leaving the same
        // five-row height budget as the input page (including common keys).
        return [rows[0], rows[1] + rows[2], rows[3], rows[4]]
    }
}

private struct RemoteComputerKeyboard: View {
    @ObservedObject var session: RemoteSessionModel
    @Environment(\.verticalSizeClass) private var verticalSizeClass
    @StateObject private var feedback = RemoteKeyboardFeedback()
    let switchKeyboard: () -> Void
    let dismiss: () -> Void
    @State private var activeModifiers: Set<UInt> = []
    @State private var page: RemoteComputerKeyboardPage = .input

    private static let commonRow: [RemoteComputerKey] = [
        .init("Ctrl", 0x11, isModifier: true),
        .init("Win", 0x5B, isModifier: true),
        .init("Alt", 0x12, isModifier: true),
        .init(NSLocalizedString("空格", comment: ""), 0x20, width: 2.6),
        .init(NSLocalizedString("回车", comment: ""), 0x0D, width: 1.4, style: .special)
    ]

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 4) {
                Button(NSLocalizedString("系统键盘", comment: "")) {
                    feedback.keyPressed()
                    releaseModifiers()
                    switchKeyboard()
                }
                .font(.system(size: 15, weight: .medium))
                .frame(width: 80, height: 44)
                .accessibilityLabel(NSLocalizedString("切换到系统键盘", comment: ""))
                if verticalSizeClass == .compact {
                    pageTabs
                } else {
                    Spacer(minLength: 0)
                }
                RemoteKeyboardDismissButton(accessibilityLabel: NSLocalizedString("收起电脑键盘", comment: "")) {
                    feedback.keyPressed()
                    releaseModifiers()
                    dismiss()
                }
                .frame(width: RemoteKeyboardDismissButton.sideLength,
                       height: RemoteKeyboardDismissButton.sideLength)
            }
            .padding(.horizontal, 4)
            .frame(height: 44)

            if verticalSizeClass != .compact {
                pageTabs
                    .padding(.horizontal, RemoteKeyboardMetrics.panelPadding)
            }

            GeometryReader { proxy in
                // Anchor both bottom rows to the input page's dimensions so
                // shared controls keep their size and position across pages.
                let inputRows = RemoteComputerKeyboardPage.input
                    .rows(compact: verticalSizeClass == .compact)
                let rows = page.rows(compact: verticalSizeClass == .compact)
                let inputRowCount = CGFloat(inputRows.count)
                let sharedRowHeight = max(0, (proxy.size.height -
                    inputRowCount * RemoteKeyboardMetrics.rowSpacing) / (inputRowCount + 1))
                let inputEdgeRow = inputRows.last ?? []
                let edgeKeyWidth = max(0, proxy.size.width -
                    CGFloat(max(0, inputEdgeRow.count - 1)) * RemoteKeyboardMetrics.keySpacing) /
                    max(1, inputEdgeRow.reduce(CGFloat.zero) { $0 + $1.width })

                VStack(spacing: RemoteKeyboardMetrics.rowSpacing) {
                    VStack(spacing: RemoteKeyboardMetrics.rowSpacing) {
                        ForEach(Array(rows.dropLast().enumerated()), id: \.offset) { _, row in
                            RemoteComputerKeyboardRow(
                                keys: row,
                                activeModifiers: activeModifiers,
                                action: handleKey
                            )
                        }
                    }
                    .frame(maxHeight: .infinity)

                    if let row = rows.last {
                        RemoteComputerKeyboardRow(
                            keys: row,
                            activeModifiers: activeModifiers,
                            action: handleKey,
                            fixedEdgeWidth: edgeKeyWidth
                        )
                        .frame(height: sharedRowHeight)
                    }

                    RemoteComputerKeyboardRow(
                        keys: Self.commonRow,
                        activeModifiers: activeModifiers,
                        action: handleKey
                    )
                    .frame(height: sharedRowHeight)
                }
                .frame(width: proxy.size.width, height: proxy.size.height)
            }
            .padding(RemoteKeyboardMetrics.panelPadding)
        }
        .foregroundStyle(RemoteKeyboardPalette.keyText)
        .buttonStyle(.plain)
        .background(RemoteKeyboardPalette.background)
        .onAppear { feedback.prepare() }
        .onDisappear {
            releaseModifiers()
            feedback.stop()
        }
    }

    private var pageTabs: some View {
        HStack(spacing: 6) {
            ForEach(RemoteComputerKeyboardPage.allCases, id: \.self) { item in
                Button {
                    guard page != item else { return }
                    feedback.keyPressed()
                    page = item
                } label: {
                    Text(NSLocalizedString(item.rawValue, comment: ""))
                        .font(.system(size: 14, weight: page == item ? .semibold : .regular))
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(page == item ? RemoteKeyboardPalette.specialKey : .clear,
                                    in: RoundedRectangle(cornerRadius: 6))
                        .contentShape(Rectangle())
                }
                .accessibilityLabel(String(format: NSLocalizedString("%1$@键盘页", comment: ""), NSLocalizedString(item.rawValue, comment: "")))
                .accessibilityAddTraits(page == item ? .isSelected : [])
            }
        }
        .frame(height: 36)
    }

    private func handleKey(_ key: RemoteComputerKey) {
        feedback.keyPressed()
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
    var fixedEdgeWidth: CGFloat? = nil

    var body: some View {
        GeometryReader { proxy in
            let spacing = RemoteKeyboardMetrics.keySpacing
            let edgeWidth = keys.count > 2 ? fixedEdgeWidth : nil
            let flexibleKeys = edgeWidth == nil ? keys[...] : keys.dropFirst().dropLast()
            let totalWidth = flexibleKeys.reduce(CGFloat.zero) { $0 + $1.width }
            let keySpace = max(0, proxy.size.width -
                               2 * (edgeWidth ?? 0) -
                               CGFloat(max(0, keys.count - 1)) * spacing)
            let unitWidth = keySpace / totalWidth

            HStack(spacing: spacing) {
                ForEach(Array(keys.enumerated()), id: \.offset) { index, key in
                    let width = index == 0 || index == keys.count - 1
                        ? (edgeWidth ?? unitWidth * key.width) : unitWidth * key.width
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
                    .frame(width: width)
                    .accessibilityLabel(key.accessibilityLabel)
                }
            }
            .frame(width: proxy.size.width, height: proxy.size.height)
        }
        .frame(maxHeight: .infinity)
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
        .accessibilityLabel(isExpanded ? NSLocalizedString("收起远程控制菜单", comment: "") : NSLocalizedString("展开远程控制菜单", comment: ""))
    }
}

/// Measure the menu before positioning it; no fixed height or later-frame
/// preference update is needed when changing pages, text size or orientation.
private struct FloatingMenuLayout: Layout {
    let orbCenter: CGPoint
    let panelWidth: CGFloat

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews,
                     cache: inout ()) -> CGSize {
        proposal.replacingUnspecifiedDimensions()
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize,
                       subviews: Subviews, cache: inout ()) {
        guard let menu = subviews.first else { return }
        let size = menu.sizeThatFits(ProposedViewSize(
            width: panelWidth, height: max(0, bounds.height - 16)))
        let preferredX = orbCenter.x > bounds.width / 2
            ? orbCenter.x - 39 - size.width / 2
            : orbCenter.x + 39 + size.width / 2
        let center = CGPoint(
            x: bounds.minX + min(max(preferredX, size.width / 2 + 8),
                                bounds.width - size.width / 2 - 8),
            y: bounds.minY + min(max(orbCenter.y, size.height / 2 + 8),
                                bounds.height - size.height / 2 - 8))
        menu.place(at: center, anchor: .center, proposal: ProposedViewSize(size))
    }
}

private struct FloatingSessionMenu: View {
    @ObservedObject var session: RemoteSessionModel
    let virtualMouseVisible: Bool
    let toggleMouse: () -> Void
    let releaseMouse: () -> Void
    private enum Page { case controls, videoSettings, networkStats }
    @State private var page: Page = .controls
    @State private var displayedNetworkSnapshot: RemoteNetworkSnapshot
    @State private var displayedFrameSize: CGSize
    let showKeyboard: () -> Void
    let chooseFile: () -> Void
    let close: () -> Void
    let disconnect: () -> Void

    let columnCount: Int
    private var columns: [GridItem] {
        Array(repeating: GridItem(.flexible(), spacing: 7), count: columnCount)
    }

    init(session: RemoteSessionModel,
         columnCount: Int,
         virtualMouseVisible: Bool,
         toggleMouse: @escaping () -> Void,
         releaseMouse: @escaping () -> Void,
         showKeyboard: @escaping () -> Void,
         chooseFile: @escaping () -> Void,
         close: @escaping () -> Void,
         disconnect: @escaping () -> Void) {
        self.session = session
        self.columnCount = columnCount
        self.virtualMouseVisible = virtualMouseVisible
        self.toggleMouse = toggleMouse
        self.releaseMouse = releaseMouse
        self.showKeyboard = showKeyboard
        self.chooseFile = chooseFile
        self.close = close
        self.disconnect = disconnect
        // Render the first frame with real values instead of replacing empty
        // placeholders while the panel's insertion animation is running.
        _displayedNetworkSnapshot = State(initialValue: session.networkSnapshot)
        _displayedFrameSize = State(initialValue: session.frameSize)
    }

    var body: some View {
        // Prefer the full natural-height panel. Only use a scrolling body when
        // the available safe-area height cannot contain all of its content.
        ViewThatFits(in: .vertical) {
            panel(scrolls: false).fixedSize(horizontal: false, vertical: true)
            panel(scrolls: true)
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
        .task(id: session.selectedDisplay) {
            // This task survives ordinary video-frame redraws and is cancelled
            // when the menu closes. Rendering only reads the sampled values.
            while !Task.isCancelled {
                displayedNetworkSnapshot = session.networkSnapshot
                displayedFrameSize = session.frameSize
                do {
                    try await Task.sleep(nanoseconds: 1_000_000_000)
                } catch {
                    return
                }
            }
        }
    }

    private func panel(scrolls: Bool) -> some View {
        VStack(spacing: 9) {
            menuHeader
            if scrolls {
                ScrollView { menuContent }
            } else {
                menuContent
            }
            disconnectButton
        }
    }

    @ViewBuilder private var menuHeader: some View {
        HStack(spacing: 8) {
            if page != .controls {
                Button {
                    page = .controls
                } label: {
                    Image(systemName: "chevron.left")
                        .frame(width: 28, height: 32)
                }
                .buttonStyle(.plain)
                .accessibilityLabel(NSLocalizedString("返回控制栏", comment: ""))
                Text(page == .videoSettings ? NSLocalizedString("画面设置", comment: "") : NSLocalizedString("网络状态", comment: ""))
                    .font(.subheadline.weight(.semibold))
            } else {
                Circle()
                    .fill(session.isConnected ? Color.green : Color.orange)
                    .frame(width: 9, height: 9)
                Text(session.connectionStatus)
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(1)
            }
            Spacer()
            Button(action: close) {
                Image(systemName: "xmark.circle.fill")
                    .font(.title3)
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .accessibilityLabel(NSLocalizedString("关闭控制栏", comment: ""))
        }

        if page == .videoSettings, !session.videoSettingsFeedback.isEmpty {
            Text(session.videoSettingsFeedback)
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    @ViewBuilder private var menuContent: some View {
        if page == .videoSettings {
            FloatingVideoSettings(session: session)
        } else if page == .networkStats {
            FloatingNetworkStatistics(snapshot: displayedNetworkSnapshot,
                                      frameSize: displayedFrameSize)
        } else {
            VStack(spacing: 9) {
                LazyVGrid(columns: columns, spacing: 7) {
                    FloatingControlButton(title: NSLocalizedString("键盘", comment: ""), symbol: "keyboard",
                                          action: showKeyboard)
                    FloatingControlButton(title: virtualMouseVisible ? NSLocalizedString("收起鼠标", comment: "") : NSLocalizedString("虚拟鼠标", comment: ""),
                                          symbol: "computermouse", action: toggleMouse)
                    FloatingControlButton(
                        title: session.mouseControlMode == .relative ? NSLocalizedString("相对鼠标", comment: "") : NSLocalizedString("绝对鼠标", comment: ""),
                        symbol: "computermouse",
                        action: {
                            session.mouseControlMode = session.mouseControlMode == .relative
                                ? .absolute : .relative
                        }
                    )

                    Menu {
                        ForEach(Array(session.displays.enumerated()), id: \.offset) { index, name in
                            Button {
                                releaseMouse()
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
                        FloatingControlLabel(title: NSLocalizedString("显示器", comment: ""), symbol: "display")
                    }
                    .buttonStyle(.plain)

                    FloatingControlButton(title: NSLocalizedString("画面设置", comment: ""), symbol: "slider.horizontal.3") {
                        page = .videoSettings
                    }
                    FloatingControlButton(
                        title: session.audioEnabled ? NSLocalizedString("声音", comment: "") : NSLocalizedString("静音", comment: ""),
                        symbol: session.audioEnabled ? "speaker.wave.2" : "speaker.slash",
                        action: session.toggleAudio
                    )
                    FloatingControlButton(title: NSLocalizedString("发送文件", comment: ""),
                                          symbol: "folder.badge.plus",
                                          action: chooseFile)
                    FloatingControlButton(title: NSLocalizedString("网络状态", comment: ""), symbol: "chart.bar.xaxis") {
                        page = .networkStats
                    }
                    if session.remotePlatform == .windows {
                        FloatingControlButton(title: "Ctrl+Alt+Del",
                                              symbol: "lock.trianglebadge.exclamationmark",
                                              action: session.sendSecureAttention)
                    }
                    FloatingControlButton(title: NSLocalizedString("剪贴板", comment: ""), symbol: "doc.on.clipboard",
                                          action: session.sendClipboard)
                    if session.privacyScreen.desktopAvailable {
                        FloatingControlButton(
                            title: session.privacyScreen.buttonTitle(at: session.privacyClock),
                            symbol: session.privacyScreen.enabled(at: session.privacyClock) ? "eye.slash" : "eye",
                            selected: session.privacyScreen.enabled(at: session.privacyClock),
                            action: session.togglePrivacyScreen
                        )
                        .disabled(!session.isConnected || !session.privacyScreen.canToggle(at: session.privacyClock))
                        .opacity(session.isConnected && session.privacyScreen.canToggle(at: session.privacyClock) ? 1 : 0.45)
                        .accessibilityLabel(NSLocalizedString("隐私屏", comment: ""))
                        .accessibilityValue(session.privacyScreen.status(at: session.privacyClock))
                        .accessibilityHint(session.privacyScreen.active ? NSLocalizedString("关闭隐私屏", comment: "") : NSLocalizedString("开启隐私屏", comment: ""))
                    }
                }

                let privacyFeedback = session.privacyScreen.feedback(at: session.privacyClock)
                if !privacyFeedback.isEmpty {
                    Text(privacyFeedback).font(.caption2).foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                if !session.controlStatus.isEmpty {
                    Text(session.controlStatus).font(.caption2).foregroundStyle(.secondary)
                }
                if !session.clipboardStatus.isEmpty {
                    Text(session.clipboardStatus).font(.caption2).foregroundStyle(.secondary)
                }

                if !session.transferStatus.isEmpty {
                    HStack(spacing: 8) {
                        Text(session.transferStatus)
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
            }
        }
    }

    private var disconnectButton: some View {
        Button(role: .destructive, action: disconnect) {
            Text(NSLocalizedString("断开连接", comment: ""))
                .font(.subheadline.weight(.semibold))
                .frame(maxWidth: .infinity)
                .frame(height: 34)
        }
        .buttonStyle(.borderedProminent)
        .tint(.red)
    }
}

private struct ScrollingCodecName: View {
    let value: String
    @State private var textWidth: CGFloat = 0
    @State private var startedAt = Date()
    @ScaledMetric(relativeTo: .caption) private var lineHeight = 16.0

    var body: some View {
        GeometryReader { bounds in
            TimelineView(.animation(minimumInterval: 1.0 / 30, paused: textWidth <= bounds.size.width)) { context in
                Text(value)
                    .lineLimit(1)
                    .fixedSize()
                    .background {
                        GeometryReader { text in
                            Color.clear
                                .onAppear { textWidth = text.size.width }
                                .onValueChange(of: text.size.width) { textWidth = $0; startedAt = Date() }
                        }
                    }
                    .offset(x: offset(in: bounds.size.width, at: context.date))
                    .frame(width: bounds.size.width, height: bounds.size.height, alignment: .leading)
                    .clipped()
            }
            .onAppear { startedAt = Date() }
            .onValueChange(of: value) { _ in startedAt = Date() }
            .onValueChange(of: bounds.size.width) { _ in startedAt = Date() }
        }
        .frame(height: lineHeight)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(value)
    }

    private func offset(in width: CGFloat, at time: Date) -> CGFloat {
        let distance = max(0, textWidth - width)
        guard distance > 0 else { return width - textWidth }
        // Pause for one second at each end, then repeat at 30 points/second.
        let phase = max(0, time.timeIntervalSince(startedAt)).truncatingRemainder(dividingBy: Double(distance) / 30 + 2)
        return -min(distance, CGFloat(max(0, phase - 1) * 30))
    }
}

private struct FloatingNetworkStatistics: View {
    let snapshot: RemoteNetworkSnapshot
    let frameSize: CGSize

    var body: some View {
        VStack(spacing: 10) {
            VStack(spacing: 0) {
                trafficRow("", inbound: NSLocalizedString("接收", comment: ""), outbound: NSLocalizedString("发送", comment: ""), loss: NSLocalizedString("丢包率", comment: ""))
                    .foregroundStyle(.secondary)
                trafficRow(NSLocalizedString("视频", comment: ""), statistics: snapshot.report?.video)
                trafficRow(NSLocalizedString("音频", comment: ""), statistics: snapshot.report?.audio)
                trafficRow(NSLocalizedString("数据", comment: ""), statistics: snapshot.report?.data)
                trafficRow(NSLocalizedString("合计", comment: ""), statistics: snapshot.report?.total)
                    .fontWeight(.semibold)
                    .background(Color.blue.opacity(0.07), in: RoundedRectangle(cornerRadius: 5))
            }
            .font(.caption2.monospacedDigit())

            Divider()

            VStack(spacing: 7) {
                detail(NSLocalizedString("帧率", comment: ""), value: "\(snapshot.framesPerSecond) FPS")
                detail(NSLocalizedString("分辨率", comment: ""), value: resolution)
                detail(NSLocalizedString("画面延时", comment: ""), value: RemoteNetworkSnapshot.latency(snapshot.videoLatencyMilliseconds))
                detail(NSLocalizedString("连接延时（RTT）", comment: ""), value: RemoteNetworkSnapshot.latency(snapshot.rttMilliseconds))
                detail(NSLocalizedString("连接方式", comment: ""), value: snapshot.connectionMode)
                HStack {
                    Text(NSLocalizedString("媒体加密", comment: ""))
                        .foregroundStyle(.secondary)
                    Spacer(minLength: 8)
                    if let report = snapshot.report {
                        Label(report.srtpActive ? NSLocalizedString("SRTP 已启用", comment: "") : NSLocalizedString("未启用", comment: ""),
                              systemImage: report.srtpActive ? "lock.shield" : "lock.open")
                            .foregroundStyle(report.srtpActive ? Color.green : Color.secondary)
                    } else {
                        Text("—")
                    }
                }
                .accessibilityElement(children: .combine)
                codec(NSLocalizedString("被控端编码器", comment: ""), value: snapshot.remoteVideoEncoder)
                codec(NSLocalizedString("控制端解码器", comment: ""), value: snapshot.localVideoDecoder)
            }
            .font(.caption.monospacedDigit())
        }
        .padding(.vertical, 2)
    }

    private var resolution: String {
        guard frameSize.width > 0, frameSize.height > 0 else { return "—" }
        return "\(Int(frameSize.width)) × \(Int(frameSize.height))"
    }

    private func detail(_ title: String, value: String) -> some View {
        HStack {
            Text(title).foregroundStyle(.secondary)
            Spacer(minLength: 8)
            Text(value)
        }
        .accessibilityElement(children: .combine)
    }

    private func codec(_ title: String, value: String) -> some View {
        HStack(spacing: 8) {
            Text(title).foregroundStyle(.secondary).fixedSize()
            ScrollingCodecName(value: value.isEmpty ? "—" : value)
        }
        .accessibilityElement(children: .combine)
    }

    private func trafficRow(_ title: String, statistics: RemoteTrafficStatistics?) -> some View {
        trafficRow(title,
                   inbound: RemoteNetworkSnapshot.bitrate(statistics?.inboundBitrate),
                   outbound: RemoteNetworkSnapshot.bitrate(statistics?.outboundBitrate),
                   loss: RemoteNetworkSnapshot.lossRate(statistics?.lossRate))
            .accessibilityLabel(String(format: NSLocalizedString("%1$@，接收 %2$@，发送 %3$@，丢包率 %4$@", comment: ""), String(describing: title), String(describing: RemoteNetworkSnapshot.bitrate(statistics?.inboundBitrate)), String(describing: RemoteNetworkSnapshot.bitrate(statistics?.outboundBitrate)), String(describing: RemoteNetworkSnapshot.lossRate(statistics?.lossRate))))
    }

    private func trafficRow(_ title: String, inbound: String,
                            outbound: String, loss: String) -> some View {
        HStack(spacing: 4) {
            Text(title).frame(width: 48, alignment: .leading)
            Text(inbound).frame(maxWidth: .infinity, alignment: .trailing)
            Text(outbound).frame(maxWidth: .infinity, alignment: .trailing)
            Text(loss).frame(width: 46, alignment: .trailing)
        }
        .lineLimit(1)
        .minimumScaleFactor(0.75)
        .padding(.horizontal, 4)
        .padding(.vertical, 5)
        .accessibilityElement(children: .combine)
    }
}

private struct FloatingVideoSettings: View {
    @ObservedObject var session: RemoteSessionModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            settingRow(NSLocalizedString("画面质量", comment: ""), options: [NSLocalizedString("低", comment: ""), NSLocalizedString("中", comment: ""), NSLocalizedString("高", comment: "")],
                       values: [0, 1, 2], selection: Binding(
                        get: { session.videoSettings.selection.quality },
                        set: { session.updateVideoSettings(quality: $0) }
                       ))
            settingRow(NSLocalizedString("画面采集帧率", comment: ""), options: ["30 fps", "60 fps"],
                       values: [30, 60], selection: Binding(
                        get: { session.videoSettings.selection.frameRate },
                        set: { session.updateVideoSettings(frameRate: $0) }
                       ))
            settingRow(NSLocalizedString("画面偏好", comment: ""), options: VideoAdaptationPolicy.allCases.map(\.title),
                       values: VideoAdaptationPolicy.allCases.map { $0.bridgeValue.rawValue },
                       selection: Binding(
                        get: { session.videoSettings.selection.preference },
                        set: { session.updateVideoSettings(preference: $0) }
                       ))
        }
        .padding(.vertical, 2)
    }

    private func settingRow(_ title: String, options: [String], values: [Int],
                            selection: Binding<Int>) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
            Picker(title, selection: selection) {
                ForEach(options.indices, id: \.self) { index in
                    Text(options[index]).tag(values[index])
                }
            }
            .pickerStyle(.segmented)
            .disabled(!session.canChangeVideoSettings)
        }
    }
}

private struct FloatingControlButton: View {
    let title: String
    let symbol: String
    var selected = false
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            FloatingControlLabel(title: title, symbol: symbol)
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? .isSelected : [])
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
                .lineLimit(2)
                .multilineTextAlignment(.center)
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
