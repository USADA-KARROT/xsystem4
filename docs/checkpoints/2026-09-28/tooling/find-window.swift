import CoreGraphics
import Foundation
// usage: find-window <owner-substring>  -> prints "windowID ownerName WxH" per matching on-screen window
let needle = CommandLine.arguments.count > 1 ? CommandLine.arguments[1].lowercased() : "xsystem4"
let opts = CGWindowListOption(arrayLiteral: .optionAll)
guard let list = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as? [[String: Any]] else { exit(1) }
for w in list {
    let owner = (w[kCGWindowOwnerName as String] as? String) ?? ""
    let name = (w[kCGWindowName as String] as? String) ?? ""
    guard owner.lowercased().contains(needle) || name.lowercased().contains(needle) else { continue }
    let id = w[kCGWindowNumber as String] as? Int ?? -1
    let layer = w[kCGWindowLayer as String] as? Int ?? -1
    let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let wd = b["Width"] as? Double ?? 0, ht = b["Height"] as? Double ?? 0
    print("\(id)\t\(owner)\t\(name)\tlayer=\(layer)\t\(Int(wd))x\(Int(ht))")
}
