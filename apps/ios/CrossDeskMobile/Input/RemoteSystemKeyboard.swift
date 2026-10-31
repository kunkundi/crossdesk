import SwiftUI
import UIKit

/// UIKit owns composition, candidate selection, keyboard switching and dictation.
/// Confirmed text is mirrored to the remote app; preedit stays in the IME.
struct RemoteSystemKeyboard: View {
    @ObservedObject var session: RemoteSessionModel
    @StateObject private var keyboard = RemoteKeyboardController()
    var inputContext: RemoteKeyboardInputContext? = nil
    var geometry: RemoteKeyboardGeometry? = nil
    var animatePresentation = true
    var didPresent: () -> Void = {}
    let switchKeyboard: () -> Void
    let dismiss: () -> Void

    var body: some View {
        RemoteKeyboardHost(
            controller: keyboard,
            edit: session.sendSystemTextEdit,
            inputContext: inputContext,
            geometry: geometry,
            animatePresentation: animatePresentation,
            didPresent: didPresent,
            key: session.sendKeyStroke,
            status: session.keyboardInputStatus,
            switchKeyboard: switchKeyboard,
            dismiss: dismiss
        )
        // The native host tracks the keyboard itself, including its animation.
        // SwiftUI must not also resize it to the keyboard's safe area.
        .ignoresSafeArea()
        .onDisappear { keyboard.stop() }
    }
}

private struct RemoteKeyboardHost: UIViewControllerRepresentable {
    @Environment(\.colorScheme) private var colorScheme
    let controller: RemoteKeyboardController
    let edit: (Int, String) -> Bool
    let inputContext: RemoteKeyboardInputContext?
    let geometry: RemoteKeyboardGeometry?
    let animatePresentation: Bool
    let didPresent: () -> Void
    let key: (UInt) -> Void
    let status: String
    let switchKeyboard: () -> Void
    let dismiss: () -> Void

    func makeUIViewController(context: Context) -> RemoteKeyboardController {
        update(controller)
        return controller
    }

    func updateUIViewController(_ controller: RemoteKeyboardController,
                                context: Context) {
        update(controller)
    }

    private func update(_ controller: RemoteKeyboardController) {
        controller.editor.applyEdit = edit
        inputContext?.editor = controller.editor
        controller.geometry = geometry
        controller.animatePresentation = animatePresentation
        controller.didPresent = didPresent
        controller.editor.key = key
        controller.inputBar.switchKeyboard = { [weak controller] in
            controller?.rememberKeyboardHeight()
            // Let SwiftUI install the replacement panel before teardown hides
            // the native keyboard, avoiding an empty frame between the two.
            controller?.animateDismissal = false
            switchKeyboard()
        }
        controller.inputBar.dismiss = { [weak controller] in
            controller?.stop()
            dismiss()
        }
        controller.overrideUserInterfaceStyle = colorScheme == .dark ? .dark : .light
        controller.inputBar.setStatus(status)
    }

    static func dismantleUIViewController(_ controller: RemoteKeyboardController,
                                          coordinator: ()) {
        controller.stop()
    }
}

final class RemoteKeyboardController: UIViewController, ObservableObject {
    let editor: RemoteTextInputView
    let inputBar: RemoteKeyboardInputBar
    var geometry: RemoteKeyboardGeometry?
    var animatePresentation = true
    var animateDismissal = true
    var didPresent: (() -> Void)?
    private var acceptingInput = true
    private var animationsEnabledBeforePresentation: Bool?

    init() {
        let editor = RemoteTextInputView()
        editor.automaticallyFocus = false
        self.editor = editor
        self.inputBar = RemoteKeyboardInputBar()
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func loadView() {
        let host = RemoteKeyboardHostView()
        host.willDetach = { [weak self] in self?.stop() }
        view = host
        view.backgroundColor = .clear
        host.inputBar = inputBar
        inputBar.translatesAutoresizingMaskIntoConstraints = false
        host.addSubview(inputBar)
        // Keep a real UITextField for IME and dictation context, but never
        // display a second copy of the remote text or a local caret.
        editor.isAccessibilityElement = false
        editor.translatesAutoresizingMaskIntoConstraints = false
        host.addSubview(editor)

        // Keep one continuous background behind the row and the system
        // keyboard's rounded corners, without reserving an input accessory slot.
        let backdrop = UIView()
        backdrop.backgroundColor = RemoteKeyboardInputBar.keyboardBackground
        backdrop.isUserInteractionEnabled = false
        backdrop.translatesAutoresizingMaskIntoConstraints = false
        host.insertSubview(backdrop, at: 0)
        NSLayoutConstraint.activate([
            editor.leadingAnchor.constraint(equalTo: inputBar.leadingAnchor),
            editor.topAnchor.constraint(equalTo: inputBar.topAnchor),
            editor.widthAnchor.constraint(equalToConstant: 1),
            editor.heightAnchor.constraint(equalToConstant: 1),
            inputBar.leadingAnchor.constraint(equalTo: host.leadingAnchor),
            inputBar.trailingAnchor.constraint(equalTo: host.trailingAnchor),
            inputBar.bottomAnchor.constraint(equalTo: host.keyboardLayoutGuide.topAnchor),
            backdrop.topAnchor.constraint(equalTo: inputBar.topAnchor),
            backdrop.leadingAnchor.constraint(equalTo: host.leadingAnchor),
            backdrop.trailingAnchor.constraint(equalTo: host.trailingAnchor),
            backdrop.bottomAnchor.constraint(equalTo: host.bottomAnchor)
        ])
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        guard acceptingInput, !editor.isFirstResponder else { return }
        if animatePresentation {
            editor.becomeFirstResponder()
        } else {
            // UIKit may start showing the keyboard after becomeFirstResponder
            // returns. Keep this short suppression alive until it is shown,
            // rather than ending it at the end of a synchronous animation block.
            animationsEnabledBeforePresentation = UIView.areAnimationsEnabled
            NotificationCenter.default.addObserver(
                self, selector: #selector(finishKeyboardPresentation),
                name: UIResponder.keyboardDidShowNotification, object: nil)
            UIView.setAnimationsEnabled(false)
            if !editor.becomeFirstResponder() {
                finishKeyboardPresentation()
            }
            view.layoutIfNeeded()
            // A hardware keyboard or an interrupted focus request may produce
            // no did-show notification. Always restore the previous state.
            // Retain the controller until cleanup even if its host disappears.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                self.finishKeyboardPresentation()
            }
        }
    }

    @objc private func finishKeyboardPresentation() {
        guard let enabled = animationsEnabledBeforePresentation else { return }
        animationsEnabledBeforePresentation = nil
        NotificationCenter.default.removeObserver(
            self, name: UIResponder.keyboardDidShowNotification, object: nil)
        UIView.setAnimationsEnabled(enabled)
        if acceptingInput {
            DispatchQueue.main.async { [weak self] in
                guard let self, self.acceptingInput else { return }
                self.didPresent?()
            }
        }
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        rememberKeyboardHeight()
    }

    func rememberKeyboardHeight() {
        guard acceptingInput, editor.isFirstResponder else { return }
        let keyboard = view.keyboardLayoutGuide.layoutFrame
        // Ignore hidden, floating and split keyboards: they do not define a
        // full-width docked panel that the computer keyboard can replace.
        guard keyboard.height > 100,
              keyboard.width >= view.bounds.width - 1 else { return }
        geometry?.remember(height: view.bounds.maxY - keyboard.minY +
                            RemoteKeyboardInputBar.barHeight,
                           size: view.bounds.size)
    }

    func stop() {
        guard acceptingInput else { return }
        acceptingInput = false
        finishKeyboardPresentation()
        editor.stop()
        inputBar.switchKeyboard = nil
        inputBar.dismiss = nil
        let dismiss = {
            self.editor.resignFirstResponder()
            self.editor.removeFromSuperview()
        }
        if animateDismissal {
            dismiss()
        } else {
            UIView.performWithoutAnimation(dismiss)
        }
    }
}

/// Retain the last native height for each window width and orientation, even
/// after the text field resigns first responder during a keyboard switch.
final class RemoteKeyboardGeometry: ObservableObject {
    private var heights: [String: CGFloat] = [:]

    private func key(for size: CGSize) -> String {
        "\(Int(size.width.rounded()))-\(size.width > size.height)"
    }

    func remember(height: CGFloat, size: CGSize) {
        heights[key(for: size)] = height
    }

    func panelHeight(in size: CGSize, bottomInset: CGFloat) -> CGFloat {
        // A rotation may reach a size at which the native keyboard has not yet
        // appeared. The compact iPhone keybed is 161 points; using the full
        // 216-point keybed here makes only the first landscape switch jump.
        // Safe-area and toolbar heights are added separately below.
        let fallback: CGFloat = size.width > size.height ? 161 : 300
        let height = heights[key(for: size)] ??
            (fallback + bottomInset + RemoteKeyboardInputBar.barHeight)
        return min(height, size.height)
    }
}

private final class RemoteKeyboardHostView: UIView {
    var willDetach: (() -> Void)?
    weak var inputBar: UIView?

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        guard let inputBar else { return nil }
        // Video gestures outside the input row must reach the remote viewport.
        return inputBar.hitTest(convert(point, to: inputBar), with: event)
    }

    override func willMove(toSuperview newSuperview: UIView?) {
        if newSuperview == nil { willDetach?() }
        super.willMove(toSuperview: newSuperview)
    }
}

/// Use the same UIKit symbol layout in both the native and SwiftUI toolbars.
/// Matching font sizes alone still gives their symbols different baselines.
struct RemoteKeyboardDismissButton: UIViewRepresentable {
    static let sideLength: CGFloat = 44
    let accessibilityLabel: String
    let action: () -> Void

    static func makeButton() -> UIButton {
        let button = UIButton(type: .system)
        button.tintColor = .label
        button.setImage(UIImage(systemName: "keyboard.chevron.compact.down"), for: .normal)
        button.setPreferredSymbolConfiguration(
            UIImage.SymbolConfiguration(pointSize: 20, weight: .regular, scale: .medium),
            forImageIn: .normal)
        button.contentHorizontalAlignment = .center
        button.contentVerticalAlignment = .center
        return button
    }

    func makeCoordinator() -> Coordinator { Coordinator(action: action) }

    func makeUIView(context: Context) -> UIButton {
        let button = Self.makeButton()
        button.addTarget(context.coordinator, action: #selector(Coordinator.pressed),
                         for: .touchUpInside)
        return button
    }

    func updateUIView(_ button: UIButton, context: Context) {
        button.accessibilityLabel = accessibilityLabel
        context.coordinator.action = action
    }

    final class Coordinator: NSObject {
        var action: () -> Void

        init(action: @escaping () -> Void) { self.action = action }

        @objc func pressed() { action() }
    }
}

final class RemoteKeyboardInputBar: UIView {
    static let barHeight: CGFloat = 44
    static let keyboardBackground = UIColor { traits in
        traits.userInterfaceStyle == .dark ? .secondarySystemBackground : .systemGray4
    }
    var switchKeyboard: (() -> Void)?
    var dismiss: (() -> Void)?
    private let statusLabel = UILabel()
    private let row = UIStackView()
    private var status = ""
    private var statusHeight: CGFloat = 0
    private var heightConstraint: NSLayoutConstraint!

    init() {
        super.init(frame: .zero)
        backgroundColor = .clear
        tintColor = .label
        heightConstraint = heightAnchor.constraint(equalToConstant: Self.barHeight)
        heightConstraint.isActive = true

        let computerButton = UIButton(type: .system)
        computerButton.setTitle(NSLocalizedString("电脑键盘", comment: ""), for: .normal)
        computerButton.titleLabel?.font = .systemFont(ofSize: 15, weight: .medium)
        computerButton.setContentHuggingPriority(.defaultHigh, for: .horizontal)
        computerButton.accessibilityLabel = NSLocalizedString("切换到电脑键盘", comment: "")
        computerButton.addTarget(self, action: #selector(switchToComputer), for: .touchUpInside)

        let dismissButton = RemoteKeyboardDismissButton.makeButton()
        dismissButton.accessibilityLabel = NSLocalizedString("收起系统键盘", comment: "")
        dismissButton.addTarget(self, action: #selector(hideKeyboard), for: .touchUpInside)

        row.axis = .horizontal
        row.alignment = .fill
        row.spacing = 4
        for item in [computerButton, UIView(), dismissButton] { row.addArrangedSubview(item) }
        row.translatesAutoresizingMaskIntoConstraints = false
        addSubview(row)
        NSLayoutConstraint.activate([
            // Keep the minimum touch width while allowing longer translations.
            computerButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 80),
            dismissButton.widthAnchor.constraint(equalToConstant: RemoteKeyboardDismissButton.sideLength),
            row.leadingAnchor.constraint(equalTo: safeAreaLayoutGuide.leadingAnchor, constant: 4),
            row.trailingAnchor.constraint(equalTo: safeAreaLayoutGuide.trailingAnchor, constant: -4),
            row.bottomAnchor.constraint(equalTo: bottomAnchor),
            row.heightAnchor.constraint(equalToConstant: Self.barHeight)
        ])

        statusLabel.font = .systemFont(ofSize: 12)
        statusLabel.textColor = .secondaryLabel
        statusLabel.numberOfLines = 0
        statusLabel.isHidden = true
        addSubview(statusLabel)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override var intrinsicContentSize: CGSize {
        CGSize(width: UIView.noIntrinsicMetric, height: Self.barHeight + statusHeight)
    }

    func setStatus(_ value: String) {
        guard status != value else { return }
        status = value
        statusLabel.text = value
        statusLabel.isHidden = value.isEmpty
        setNeedsLayout()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let inset = max(8, safeAreaInsets.left)
        let width = max(1, bounds.width - inset - max(8, safeAreaInsets.right))
        let height = status.isEmpty ? 0 : ceil(statusLabel.sizeThatFits(
            CGSize(width: width, height: .greatestFiniteMagnitude)).height) + 4
        statusLabel.frame = CGRect(x: inset, y: 2, width: width, height: max(0, height - 4))
        if statusHeight != height {
            statusHeight = height
            heightConstraint.constant = Self.barHeight + height
            invalidateIntrinsicContentSize()
        }
    }

    @objc private func switchToComputer() { switchKeyboard?() }
    @objc private func hideKeyboard() { dismiss?() }
}

/// Resets the local IME context synchronously before a remote click can move
/// the insertion point. Nothing typed at the old location may be rewritten there.
final class RemoteKeyboardInputContext: ObservableObject {
    weak var editor: RemoteTextInputView?

    func remoteFocusWillChange() {
        editor?.finishInputContext()
    }
}

final class RemoteTextInputView: UITextField, UITextFieldDelegate {
    var applyEdit: ((Int, String) -> Bool)?
    var key: ((UInt) -> Void)?
    private(set) var acceptingInput = true
    var automaticallyFocus = true
    private var sentText = ""
    private var changeScheduled = false
    private var resetting = false
    private var generation: UInt64 = 0

    override var canBecomeFirstResponder: Bool { acceptingInput && super.canBecomeFirstResponder }

    init() {
        super.init(frame: .zero)
        delegate = self
        textColor = .clear
        tintColor = .clear
        backgroundColor = .clear
        borderStyle = .none
        keyboardType = .default
        returnKeyType = .default
        autocorrectionType = .default
        autocapitalizationType = .none
        spellCheckingType = .default
        smartQuotesType = .no
        smartDashesType = .no
        smartInsertDeleteType = .no
        inputAssistantItem.leadingBarButtonGroups = []
        inputAssistantItem.trailingBarButtonGroups = []
        addTarget(self, action: #selector(scheduleTextChange), for: .editingChanged)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window != nil && acceptingInput && automaticallyFocus { becomeFirstResponder() }
    }

    override func caretRect(for position: UITextPosition) -> CGRect { .zero }
    override func selectionRects(for range: UITextRange) -> [UITextSelectionRect] { [] }

    func textFieldDidChangeSelection(_ textField: UITextField) { scheduleTextChange() }

    override func insertText(_ text: String) {
        guard acceptingInput else { return }
        if (text == "\n" || text == "\r") && markedTextRange == nil {
            _ = textFieldShouldReturn(self)
            return
        }
        super.insertText(text)
        scheduleTextChange()
    }

    override func replace(_ range: UITextRange, withText text: String) {
        guard acceptingInput else { return }
        super.replace(range, withText: text)
        scheduleTextChange()
    }

    override func setMarkedText(_ markedText: String?, selectedRange: NSRange) {
        guard acceptingInput else { return }
        super.setMarkedText(markedText, selectedRange: selectedRange)
        scheduleTextChange()
    }

    override func unmarkText() {
        super.unmarkText()
        scheduleTextChange()
    }

    override func deleteBackward() {
        guard acceptingInput else { return }
        if !hasText && markedTextRange == nil {
            // Flush any pending deletion before deleting beyond local context.
            if synchronizeText() { key?(0x08) }
        } else {
            super.deleteBackward()
            scheduleTextChange()
        }
    }

    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        guard acceptingInput, markedTextRange == nil else { return false }
        if synchronizeText() {
            key?(0x0d)
            resetContext()
        }
        return false
    }

    @objc private func scheduleTextChange() {
        guard acceptingInput, !resetting, !changeScheduled else { return }
        changeScheduled = true
        let currentGeneration = generation
        // UIKit may replace text and clear markedTextRange in separate calls.
        // Wait one run-loop turn so only the completed IME transaction is sent.
        DispatchQueue.main.async { [weak self] in
            guard let self, self.generation == currentGeneration else { return }
            self.changeScheduled = false
            _ = self.synchronizeText()
        }
    }

    @discardableResult
    func synchronizeText() -> Bool {
        guard acceptingInput, !resetting, markedTextRange == nil else { return false }
        let current = text ?? ""
        guard current != sentText else { return true }
        let prefixCount = zip(sentText, current).prefix(while: { $0 == $1 }).count
        let deleteCount = sentText.count - prefixCount
        let insertion = String(current.dropFirst(prefixCount))
        guard applyEdit?(deleteCount, insertion) == true else {
            // Do not keep invisible rejected input to replay on a later key.
            resetContext()
            return false
        }
        sentText = current
        return true
    }

    func finishInputContext() {
        guard acceptingInput else { return }
        _ = synchronizeText()
        resetContext()
    }

    func stop() {
        acceptingInput = false
        applyEdit = nil
        key = nil
        resetContext()
    }

    private func resetContext() {
        generation &+= 1
        changeScheduled = false
        resetting = true
        super.unmarkText()
        text = ""
        sentText = ""
        resetting = false
    }
}
