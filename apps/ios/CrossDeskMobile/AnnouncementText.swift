import Foundation

enum AnnouncementText {
    /// Match desktop: preserve plain text and line breaks; recognize only web
    /// URLs and [label](URL), without interpreting arbitrary Markdown or HTML.
    static func format(_ body: String) -> AttributedString {
        let text = Array(body.replacingOccurrences(of: "\r\n", with: "\n"))
        var output = AttributedString()
        var literal = ""
        var position = 0

        func hasScheme(at start: Int) -> Bool {
            let prefix = String(text[start..<min(text.count, start + 8)]).lowercased()
            return prefix.hasPrefix("https://") || prefix.hasPrefix("http://")
        }

        func urlEnd(from start: Int) -> Int {
            let punctuation = Set("<>\"'`\\{}，。；：！？、（）【】《》「」『』“”‘’")
            var parentheses = 0
            var brackets = 0
            var end = start
            while end < text.count {
                let character = text[end]
                if punctuation.contains(character) || character.unicodeScalars.contains(where: {
                    $0.value <= 0x20 || $0.value == 0x7f
                }) { break }
                if character == "(" { parentheses += 1 }
                if character == ")" {
                    if parentheses == 0 { break }
                    parentheses -= 1
                }
                if character == "[" { brackets += 1 }
                if character == "]" {
                    if brackets == 0 { break }
                    brackets -= 1
                }
                end += 1
            }
            return end
        }

        func appendLink(label: String, destination: String) -> Bool {
            guard let url = URL(string: destination), isWebURL(url) else { return false }
            output.append(AttributedString(literal))
            literal = ""
            var link = AttributedString(label)
            link.link = url
            output.append(link)
            return true
        }

        while position < text.count {
            if text[position] == "[",
               let labelEnd = text[(position + 1)...].firstIndex(of: "]"),
               labelEnd > position + 1, labelEnd + 2 < text.count,
               text[labelEnd + 1] == "(",
               !text[(position + 1)..<labelEnd].contains(where: { "[]\r\n".contains($0) }),
               hasScheme(at: labelEnd + 2) {
                let end = urlEnd(from: labelEnd + 2)
                if end < text.count, text[end] == ")",
                   appendLink(label: String(text[(position + 1)..<labelEnd]),
                              destination: String(text[(labelEnd + 2)..<end])) {
                    position = end + 1
                    continue
                }
            }
            if hasScheme(at: position) {
                var end = urlEnd(from: position)
                while end > position, ".,;:!?".contains(text[end - 1]) { end -= 1 }
                let address = String(text[position..<end])
                if appendLink(label: address, destination: address) {
                    position = end
                    continue
                }
            }
            literal.append(text[position])
            position += 1
        }
        output.append(AttributedString(literal))
        return output
    }

    static func isWebURL(_ url: URL) -> Bool {
        guard let scheme = url.scheme?.lowercased(), ["http", "https"].contains(scheme),
              let host = url.host, !host.isEmpty else { return false }
        return !url.absoluteString.unicodeScalars.contains {
            $0.value <= 0x20 || $0.value == 0x7f || "\\<>\"".unicodeScalars.contains($0)
        }
    }
}
