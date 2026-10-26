import Cocoa
import AVFoundation

final class CameraView: NSView {
    let preview: AVCaptureVideoPreviewLayer
    var quarterTurns = 1 { didSet { needsLayout = true } }
    init(session: AVCaptureSession) {
        preview = AVCaptureVideoPreviewLayer(session: session)
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        preview.videoGravity = .resizeAspect
        layer?.addSublayer(preview)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is unsupported") }
    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let sideways = quarterTurns % 2 != 0
        preview.bounds = CGRect(x: 0, y: 0,
                                width: sideways ? bounds.height : bounds.width,
                                height: sideways ? bounds.width : bounds.height)
        preview.position = CGPoint(x: bounds.midX, y: bounds.midY)
        preview.setAffineTransform(CGAffineTransform(rotationAngle: CGFloat(quarterTurns) * .pi / 2))
        CATransaction.commit()
    }
}

final class PreviewApp: NSObject, NSApplicationDelegate, AVCaptureVideoDataOutputSampleBufferDelegate {
    let session = AVCaptureSession()
    let queue = DispatchQueue(label: "camera.capture")
    var window: NSWindow!
    var firstFrame = true
    var cameraView: CameraView!
    var rotationLabel: NSTextField!
    @objc func rotateLeft() { rotate(-1) }
    @objc func rotateRight() { rotate(1) }
    func rotate(_ delta: Int) {
        cameraView.quarterTurns = (cameraView.quarterTurns + delta + 4) % 4
        UserDefaults.standard.set(cameraView.quarterTurns, forKey: "previewQuarterTurns")
        rotationLabel.stringValue = "旋转 \(cameraView.quarterTurns * 90)°"
    }
    func applicationDidFinishLaunching(_ notification: Notification) {
        AVCaptureDevice.requestAccess(for: .video) { allowed in
            DispatchQueue.main.async {
                if allowed { self.start() }
                else { self.fail("请在系统设置 → 隐私与安全性 → 摄像头中允许 CameraPreview。") }
            }
        }
    }
    func fail(_ message: String) {
        NSLog("%@", message)
        let alert = NSAlert()
        alert.messageText = "摄像头预览无法启动"
        alert.informativeText = message
        alert.runModal()
        NSApp.terminate(nil)
    }
    func start() {
        let discovery = AVCaptureDevice.DiscoverySession(deviceTypes: [.external, .builtInWideAngleCamera], mediaType: .video, position: .unspecified)
        for device in discovery.devices { NSLog("Camera: %@", device.localizedName) }
        guard let device = discovery.devices.first(where: { $0.localizedName.localizedCaseInsensitiveContains("HD CAMERA") })
            ?? discovery.devices.first(where: { $0.deviceType == .external }) else {
            fail("未发现外接 USB 摄像头，请检查连接。")
            return
        }
        do {
            session.beginConfiguration()
            session.sessionPreset = .high
            let input = try AVCaptureDeviceInput(device: device)
            guard session.canAddInput(input) else { session.commitConfiguration(); fail("无法添加摄像头输入。"); return }
            session.addInput(input)
            let output = AVCaptureVideoDataOutput()
            output.alwaysDiscardsLateVideoFrames = true
            output.setSampleBufferDelegate(self, queue: queue)
            guard session.canAddOutput(output) else { session.commitConfiguration(); fail("无法添加视频输出。"); return }
            session.addOutput(output)
            session.commitConfiguration()
            window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 960, height: 720), styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
            window.title = "\(device.localizedName) · 实时预览"
            window.isReleasedWhenClosed = false
            window.minSize = NSSize(width: 480, height: 320)
            let view = window.contentView!
            cameraView = CameraView(session: session)
            cameraView.quarterTurns = (UserDefaults.standard.object(forKey: "previewQuarterTurns") as? Int ?? 1) % 4
            rotationLabel = NSTextField(labelWithString: "旋转 \(cameraView.quarterTurns * 90)°")
            let left = NSButton(title: "↶ 左转 90°", target: self, action: #selector(rotateLeft))
            let right = NSButton(title: "↷ 右转 90°", target: self, action: #selector(rotateRight))
            let controls = NSStackView(views: [left, rotationLabel, right])
            controls.spacing = 16
            controls.orientation = .horizontal
            cameraView.translatesAutoresizingMaskIntoConstraints = false
            controls.translatesAutoresizingMaskIntoConstraints = false
            view.addSubview(cameraView)
            view.addSubview(controls)
            NSLayoutConstraint.activate([
                controls.topAnchor.constraint(equalTo: view.topAnchor, constant: 10),
                controls.centerXAnchor.constraint(equalTo: view.centerXAnchor),
                cameraView.topAnchor.constraint(equalTo: controls.bottomAnchor, constant: 10),
                cameraView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
                cameraView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
                cameraView.bottomAnchor.constraint(equalTo: view.bottomAnchor)
            ])
            window.center()
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            queue.async { self.session.startRunning() }
        } catch { fail(error.localizedDescription) }
    }
    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        if firstFrame {
            firstFrame = false
            if let description = CMSampleBufferGetFormatDescription(sampleBuffer) {
                let size = CMVideoFormatDescriptionGetDimensions(description)
                let message = "Preview active: \(size.width)x\(size.height)\n"
                NSLog("%@", message)
                let path = Bundle.main.bundleURL.deletingLastPathComponent().appendingPathComponent("camera-preview-status.txt")
                try? message.write(to: path, atomically: true, encoding: .utf8)
            }
        }
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationWillTerminate(_ notification: Notification) { session.stopRunning() }
}
let app = NSApplication.shared
let delegate = PreviewApp()
app.setActivationPolicy(.regular)
app.delegate = delegate
app.run()
