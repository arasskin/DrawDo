import Foundation
import Vision
let directory = URL(fileURLWithPath: CommandLine.arguments[1])
print("frame,tag")
for file in try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil).filter({$0.lastPathComponent.hasPrefix("frame-") && $0.pathExtension == "png"}).sorted(by: {$0.lastPathComponent < $1.lastPathComponent}) {
 let request = VNRecognizeTextRequest()
 request.recognitionLevel = .accurate
 request.usesLanguageCorrection = false
 try VNImageRequestHandler(url: file).perform([request])
 let text = (request.results ?? []).compactMap {$0.topCandidates(1).first?.string}.joined(separator: " ")
 let expression = try NSRegularExpression(pattern: "LB([0-9]{6})")
 if let match = expression.firstMatch(in:text,range:NSRange(text.startIndex...,in:text)), let range = Range(match.range(at:1),in:text) {
  print("\(Int(file.deletingPathExtension().lastPathComponent.dropFirst(6))!),\(text[range])")
 } else { fputs("No tag: \(file.lastPathComponent): \(text)\n",stderr) }
}
