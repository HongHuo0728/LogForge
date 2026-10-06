import SwiftUI

struct VideoInfoView: View {

    let info: VideoInfo

    var body: some View {

        VStack(alignment: .leading, spacing: 14) {

            Text(info.fileName)
                .font(.headline)
                .lineLimit(2)

            Divider()

            infoRow(
                L10n.text("info.resolution"),
                "\(info.width) × \(info.height)"
            )

            infoRow(
                L10n.text("info.framerate"),
                String(format: "%.4f fps", info.frameRate)
            )

            infoRow(
                L10n.text("info.duration"),
                String(format: "%.2f s", info.duration)
            )

            infoRow(
                L10n.text("info.codec"),
                info.codec
            )

            infoRow(
                L10n.text("info.pixels"),
                info.pixelFormat
            )

            infoRow(
                L10n.text("info.primaries"),
                info.colorPrimaries
            )

            infoRow(
                L10n.text("info.transfer"),
                info.transferFunction
            )

            infoRow(
                L10n.text("info.matrix"),
                info.matrix
            )

            Divider()

            validationRow(
                "ProRes",
                info.isProRes
            )

            validationRow(
                "BT.2020",
                info.isBT2020
            )

            validationRow(
                "HLG",
                info.isHLG
            )
        }
        .padding()
        .clearGlass()
        .clipShape(
            RoundedRectangle(cornerRadius: 18)
        )
    }

    private func infoRow(
        _ title: String,
        _ value: String
    ) -> some View {

        HStack(alignment: .top) {

            Text(title)
                .foregroundStyle(.secondary)

            Spacer()

            Text(value)
                .multilineTextAlignment(.trailing)
        }
        .font(.subheadline)
    }

    private func validationRow(
        _ title: String,
        _ valid: Bool
    ) -> some View {

        HStack {

            Image(
                systemName:
                    valid
                    ? "checkmark.circle.fill"
                    : "xmark.circle.fill"
            )

            Text(title)

            Spacer()

            Text(
                valid
                ? L10n.text("info.detected")
                : L10n.text("info.absent")
            )
            .foregroundStyle(.secondary)
        }
    }
}
