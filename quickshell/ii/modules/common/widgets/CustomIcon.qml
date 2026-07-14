import QtQuick
import Quickshell
import Quickshell.Widgets
import Qt5Compat.GraphicalEffects

Item {
    id: root
    
    property bool colorize: false
    property color color
    property string source: ""
    property string iconFolder: Qt.resolvedUrl(Quickshell.shellPath("assets/icons"))  // The folder to check first
    width: 30
    height: 30
    
    IconImage {
        id: iconImage
        anchors.fill: parent
        source: {
            if (!root.source || root.source.length === 0) return ""

            // If `source` is already a path/url, pass through.
            if (root.source.includes("/") || root.source.includes("file:") || root.source.includes("qrc:")) {
                return root.source
            }

            // Otherwise treat it as an icon name under `assets/icons/`.
            // Callers typically pass names like `spark-symbolic` (no extension).
            const base = iconFolder + "/" + root.source
            if (root.source.endsWith(".svg") || root.source.endsWith(".png") || root.source.endsWith(".jpg") || root.source.endsWith(".jpeg") || root.source.endsWith(".webp")) {
                return base
            }
            return base + ".svg"
        }
        implicitSize: root.height
    }

    Loader {
        active: root.colorize
        anchors.fill: iconImage
        sourceComponent: ColorOverlay {
            source: iconImage
            color: root.color
        }
    }
}
