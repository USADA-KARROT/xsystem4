import Foundation
import AppKit
import CoreGraphics
import ImageIO
import ScreenCaptureKit
import UniformTypeIdentifiers
// usage: capture-window <windowID> <out.png>
// Captures a single window through ScreenCaptureKit (works when the window is occluded).
_ = NSApplication.shared  // connect to the window server before using CoreGraphics/SCK
let args = CommandLine.arguments
guard args.count == 3, let raw = UInt32(args[1]) else { print("usage: capture-window <windowID> <out.png>"); exit(64) }
let wid = CGWindowID(raw)
let out = URL(fileURLWithPath: args[2])
let sem = DispatchSemaphore(value: 0)
var code: Int32 = 0
Task {
    do {
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: false)
        guard let w = content.windows.first(where: { $0.windowID == wid }) else {
            print("window \(wid) not found"); code = 2; sem.signal(); return
        }
        let filter = SCContentFilter(desktopIndependentWindow: w)
        let cfg = SCStreamConfiguration()
        cfg.width = Int(w.frame.width * 2)
        cfg.height = Int(w.frame.height * 2)
        cfg.showsCursor = false
        let img = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: cfg)
        guard let dest = CGImageDestinationCreateWithURL(out as CFURL, UTType.png.identifier as CFString, 1, nil) else {
            print("cannot create \(out.path)"); code = 3; sem.signal(); return
        }
        CGImageDestinationAddImage(dest, img, nil)
        CGImageDestinationFinalize(dest)
        print("ok \(img.width)x\(img.height)")
    } catch {
        print("error: \(error)"); code = 1
    }
    sem.signal()
}
sem.wait()
exit(code)
