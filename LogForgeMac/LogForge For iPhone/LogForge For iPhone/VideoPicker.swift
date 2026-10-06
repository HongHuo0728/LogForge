import SwiftUI
import UniformTypeIdentifiers

struct VideoPicker: UIViewControllerRepresentable {

    let onPick: (URL) -> Void

    func makeUIViewController(
        context: Context
    ) -> UIDocumentPickerViewController {

        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: [.movie],
            asCopy: true
        )

        picker.delegate = context.coordinator
        picker.allowsMultipleSelection = false

        return picker
    }

    func updateUIViewController(
        _ uiViewController: UIDocumentPickerViewController,
        context: Context
    ) {
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(onPick: onPick)
    }

    final class Coordinator: NSObject, UIDocumentPickerDelegate {

        private let onPick: (URL) -> Void

        init(onPick: @escaping (URL) -> Void) {
            self.onPick = onPick
        }

        func documentPicker(
            _ controller: UIDocumentPickerViewController,
            didPickDocumentsAt urls: [URL]
        ) {
            guard let url = urls.first else {
                return
            }

            onPick(url)
        }
    }
}
