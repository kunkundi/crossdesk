import Foundation

/// Serialize writes and deletions so an in-flight capture cannot recreate a
/// preview after a clear/revocation has finished.
final class ThumbnailFileStore {
    let directory: URL
    private let queue = DispatchQueue(label: "cn.crossdesk.mobile.thumbnails",
                                     qos: .utility)

    init(directory: URL) {
        self.directory = directory
    }

    func url(for fileName: String) -> URL {
        directory.appendingPathComponent(fileName, isDirectory: false)
    }

    func save(fileName: String, makeData: @escaping () -> Data?,
              completion: @escaping (String?) -> Void) {
        queue.async {
            let saved: String? = autoreleasepool {
                guard let data = makeData() else { return nil }
                do {
                    try FileManager.default.createDirectory(at: self.directory,
                                                            withIntermediateDirectories: true)
                    var directory = self.directory
                    var values = URLResourceValues()
                    values.isExcludedFromBackup = true
                    try directory.setResourceValues(values)
                    #if os(iOS)
                    try data.write(to: self.url(for: fileName),
                                   options: [.atomic, .completeFileProtection])
                    #else
                    try data.write(to: self.url(for: fileName), options: .atomic)
                    #endif
                    return fileName
                } catch {
                    return nil
                }
            }
            completion(saved)
        }
    }

    func remove(fileName: String) {
        queue.async { try? FileManager.default.removeItem(at: self.url(for: fileName)) }
    }

    func removeAll(completion: @escaping (Bool) -> Void) {
        queue.async {
            do {
                if FileManager.default.fileExists(atPath: self.directory.path) {
                    try FileManager.default.removeItem(at: self.directory)
                }
                completion(true)
            } catch {
                completion(false)
            }
        }
    }
}
