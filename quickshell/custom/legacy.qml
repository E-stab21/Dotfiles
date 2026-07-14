import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Hyprland
import Quickshell.Networking
import Quickshell.Bluetooth
import Quickshell.Services.UPower

ShellRoot {
    id: root

    property date now: new Date()
    property string openMenu: ""
    property string query: ""
    property int selectedApp: 0
    property string confirmPower: ""
    property int workspaceCount: 10

    readonly property color barBg: "#111111"
    readonly property color menuBg: "#111111"
    readonly property color hoverBg: "#242424"
    readonly property color activeBg: "#333333"
    readonly property color border: "#282828"
    readonly property color text: "#eeeeee"
    readonly property color muted: "#a0a0a0"
    readonly property color danger: "#ff6b6b"
    readonly property string fontName: "Cantarell"
    readonly property string iconFontName: materialSymbols.name.length > 0 ? materialSymbols.name : "Symbols Nerd Font"
    readonly property string materialSymbolsPath: "file:///home/ethan/.local/share/icons/material-design-icons/variablefont/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.ttf"

    readonly property var networkDevices: (typeof Networking !== "undefined" && Networking && Networking.devices) ? Networking.devices.values : []
    readonly property var wifiDevice: networkDevices.find(function(device) {
        return device && device.type === DeviceType.Wifi;
    }) || null
    readonly property bool wifiEnabled: (typeof Networking !== "undefined" && Networking) ? Networking.wifiEnabled : false
    readonly property var wifiNetworks: (wifiDevice && wifiDevice.networks) ? wifiDevice.networks.values : []
    readonly property var connectedWifi: wifiNetworks.find(function(network) {
        return network && network.connected;
    }) || null

    readonly property var bluetoothAdapter: (typeof Bluetooth !== "undefined" && Bluetooth) ? Bluetooth.defaultAdapter : null
    readonly property var bluetoothDevices: (typeof Bluetooth !== "undefined" && Bluetooth && Bluetooth.devices) ? Bluetooth.devices.values : []
    readonly property var connectedBluetoothDevices: bluetoothDevices.filter(function(device) {
        return device && device.connected;
    })

    readonly property var batteryDevice: UPower.displayDevice
    readonly property bool batteryPresent: batteryDevice !== null && batteryDevice.ready && batteryDevice.isLaptopBattery && batteryDevice.isPresent
    readonly property real batteryLevel: batteryPresent ? Math.max(0, Math.min(1, batteryDevice.percentage)) : 0
    readonly property bool batteryCharging: batteryPresent && batteryDevice.state === UPowerDeviceState.Charging

    FontLoader {
        id: materialSymbols
        source: root.materialSymbolsPath
    }

    function batteryIcon() {
        if (batteryCharging)
            return "\ue1a3"; // battery_charging_full
        var pct = batteryLevel * 100;
        if (pct <= 8) return "\uebdc";   // battery_0_bar
        if (pct <= 22) return "\uf09c";  // battery_1_bar
        if (pct <= 36) return "\uf09d";  // battery_2_bar
        if (pct <= 50) return "\uf09e";  // battery_3_bar
        if (pct <= 64) return "\uf09f";  // battery_4_bar
        if (pct <= 78) return "\uf0a0";  // battery_5_bar
        if (pct <= 92) return "\uf0a1";  // battery_6_bar
        return "\ue1a5";                 // battery_full
    }

    readonly property var applications: {
        var apps = DesktopEntries.applications.values;
        var visibleApps = [];
        for (var i = 0; i < apps.length; i++) {
            if (apps[i] && !apps[i].noDisplay)
                visibleApps.push(apps[i]);
        }
        return visibleApps;
    }

    readonly property var appResults: {
        var q = query.toLowerCase().trim();
        var matches = [];

        for (var i = 0; i < applications.length; i++) {
            var app = applications[i];
            var name = String(app.name || "").toLowerCase();
            var generic = String(app.genericName || "").toLowerCase();
            var id = String(app.id || "").toLowerCase();
            var haystack = name + " " + generic + " " + id;

            if (q.length === 0 || haystack.indexOf(q) >= 0)
                matches.push(app);
        }

        matches.sort(function(a, b) {
            return String(a.name || "").localeCompare(String(b.name || ""));
        });

        return matches;
    }

    onQueryChanged: selectedApp = 0

    Component.onCompleted: {
        Hyprland.refreshMonitors();
        Hyprland.refreshWorkspaces();
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.now = new Date()
    }

    Timer {
        id: confirmTimer
        interval: 3500
        onTriggered: root.confirmPower = ""
    }

    IpcHandler {
        target: "custombar"

        function launcher(): void {
            root.openMenu = "apps";
            root.confirmPower = "";
        }

        function toggleLauncher(): void {
            root.toggleMenu("apps");
        }
    }

    function toggleMenu(menu) {
        openMenu = openMenu === menu ? "" : menu;
        confirmPower = "";
    }

    function showMenu(menu) {
        openMenu = menu;
        confirmPower = "";
    }

    function closeMenu() {
        openMenu = "";
        confirmPower = "";
    }

    function launchApp(app) {
        if (!app)
            return;

        app.execute();
        query = "";
        closeMenu();
    }

    function moveAppSelection(delta) {
        if (openMenu !== "apps" || appResults.length === 0)
            return;

        selectedApp = Math.max(0, Math.min(appResults.length - 1, selectedApp + delta));
    }

    function activateSelectedApp() {
        if (openMenu === "apps" && selectedApp >= 0 && selectedApp < appResults.length)
            launchApp(appResults[selectedApp]);
    }

    function focusWorkspace(workspaceId) {
        Hyprland.dispatch("workspace " + workspaceId);
    }

    function runPower(action) {
        var needsConfirm = action === "logout" || action === "reboot" || action === "shutdown";
        if (needsConfirm && confirmPower !== action) {
            confirmPower = action;
            confirmTimer.restart();
            return;
        }

        closeMenu();
        if (action === "lock")
            Quickshell.execDetached([Quickshell.env("HOME") + "/.config/hypr/scripts/lock.sh"]);
        else if (action === "sleep")
            Quickshell.execDetached(["systemctl", "suspend"]);
        else if (action === "logout")
            Hyprland.dispatch("hl.dsp.exit()");
        else if (action === "reboot")
            Quickshell.execDetached(["systemctl", "reboot"]);
        else if (action === "shutdown")
            Quickshell.execDetached(["systemctl", "poweroff"]);
    }

    component BarButton: Rectangle {
        id: button

        property string icon: ""
        property string label: ""
        property bool active: false
        signal clicked()
        signal entered()
        signal hoverChanged(bool hovered)

        implicitWidth: 28
        implicitHeight: 28
        radius: width / 2
        color: active || hover.hovered ? Qt.rgba(0.78, 0.78, 0.78, 0.28) : Qt.rgba(0.78, 0.78, 0.78, 0.14)

        Text {
            anchors.centerIn: parent
            width: parent.width
            height: parent.height
            text: button.icon
            color: root.text
            font.family: root.iconFontName
            font.pixelSize: 16
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }

        HoverHandler {
            id: hover
            onHoveredChanged: {
                button.hoverChanged(hovered);
                if (hovered)
                    button.entered();
            }
        }
    }

    component PowerBar: Item {
        id: meter

        signal clicked()
        signal entered()
        signal hoverChanged(bool hovered)

        implicitWidth: 86
        implicitHeight: 28

        Rectangle {
            anchors.fill: parent
            radius: 14
            color: hover.hovered || root.openMenu === "power" ? root.hoverBg : "transparent"
        }

        Text {
            id: batteryIcon
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.batteryIcon()
            color: root.batteryCharging ? "#c7f59b" : (root.batteryLevel <= 0.2 ? root.danger : root.muted)
            font.family: root.iconFontName
            font.pixelSize: 20
            font.weight: Font.Medium
        }

        Text {
            anchors.left: batteryIcon.right
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(root.batteryLevel * 100) + "%"
            color: root.batteryCharging ? "#c7f59b" : root.text
            font.family: root.fontName
            font.pixelSize: 14
            font.weight: Font.Medium
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: meter.clicked()
        }

        HoverHandler {
            id: hover
            onHoveredChanged: {
                meter.hoverChanged(hovered);
                if (hovered)
                    meter.entered();
            }
        }
    }

    component MenuPanel: Item {
        id: panel

        property real corner: 14
        property real safePad: 0
        signal safeEntered()
        signal safeExited()

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "transparent"
                strokeWidth: 0
                fillColor: root.menuBg
                joinStyle: ShapePath.RoundJoin

                startX: -panel.corner
                startY: 0
                PathLine { x: panel.width + panel.corner; y: 0 }
                PathArc {
                    x: panel.width
                    y: panel.corner
                    radiusX: panel.corner
                    radiusY: panel.corner
                    direction: PathArc.Counterclockwise
                }
                PathLine { x: panel.width; y: panel.height - panel.corner }
                PathQuad { x: panel.width - panel.corner; y: panel.height; controlX: panel.width; controlY: panel.height }
                PathLine { x: panel.corner; y: panel.height }
                PathQuad { x: 0; y: panel.height - panel.corner; controlX: 0; controlY: panel.height }
                PathLine { x: 0; y: panel.corner }
                PathArc {
                    x: -panel.corner
                    y: 0
                    radiusX: panel.corner
                    radiusY: panel.corner
                    direction: PathArc.Counterclockwise
                }
            }

            ShapePath {
                strokeColor: root.border
                strokeWidth: 1
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin

                startX: panel.width + panel.corner
                startY: 0
                PathArc {
                    x: panel.width
                    y: panel.corner
                    radiusX: panel.corner
                    radiusY: panel.corner
                    direction: PathArc.Counterclockwise
                }
                PathLine { x: panel.width; y: panel.height - panel.corner }
                PathQuad { x: panel.width - panel.corner; y: panel.height; controlX: panel.width; controlY: panel.height }
                PathLine { x: panel.corner; y: panel.height }
                PathQuad { x: 0; y: panel.height - panel.corner; controlX: 0; controlY: panel.height }
                PathLine { x: 0; y: panel.corner }
                PathArc {
                    x: -panel.corner
                    y: 0
                    radiusX: panel.corner
                    radiusY: panel.corner
                    direction: PathArc.Counterclockwise
                }
            }
        }

        HoverHandler {
            margin: panel.safePad
            onHoveredChanged: {
                if (hovered)
                    panel.safeEntered();
                else
                    panel.safeExited();
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            propagateComposedEvents: true
            onEntered: panel.safeEntered()
            onExited: panel.safeExited()
        }
    }

    component MenuRow: Rectangle {
        id: row

        property string label: ""
        property string sublabel: ""
        property bool selected: false
        property bool danger: false
        signal clicked()

        implicitHeight: 40
        radius: 10
        color: selected ? root.activeBg : (mouse.containsMouse ? root.hoverBg : "transparent")

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: row.label
                color: row.danger ? root.danger : root.text
                font.family: root.fontName
                font.pixelSize: 14
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            Text {
                visible: row.sublabel.length > 0
                width: parent.width
                text: row.sublabel
                color: root.muted
                font.family: root.fontName
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.clicked()
        }
    }

    component WorkspaceDots: Item {
        id: dots

        property string screenName: ""

        readonly property string activeWorkspaceName: {
            var monitors = Hyprland.monitors.values;
            for (var i = 0; i < monitors.length; i++) {
                if (monitors[i].name === screenName)
                    return monitors[i].activeWorkspace ? monitors[i].activeWorkspace.name : "";
            }
            return Hyprland.focusedWorkspace ? Hyprland.focusedWorkspace.name : "";
        }

        implicitWidth: dotRow.implicitWidth
        implicitHeight: 28

        Row {
            id: dotRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Repeater {
                model: root.workspaceCount

                delegate: Item {
                    id: slot

                    required property int index
                    readonly property int workspaceId: index + 1
                    readonly property bool active: dots.activeWorkspaceName === String(workspaceId)

                    width: active ? 22 : 8
                    height: 28

                    Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width
                        height: 8
                        radius: height / 2
                        color: slot.active ? root.text : root.muted
                        opacity: slot.active ? 1 : (hit.containsMouse ? 0.65 : 0.35)

                        Behavior on opacity { NumberAnimation { duration: 100 } }
                    }

                    MouseArea {
                        id: hit
                        anchors.fill: parent
                        anchors.leftMargin: -2
                        anchors.rightMargin: -2
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.focusWorkspace(slot.workspaceId)
                    }
                }
            }
        }
    }

    Variants {
        model: Quickshell.screens

        PanelWindow {
            id: window

            required property var modelData
            property bool hoverArmed: false
            property bool hoverBar: false
            property bool hoverAppsButton: false
            property bool hoverWifiButton: false
            property bool hoverBluetoothButton: false
            property bool hoverPowerButton: false
            property bool hoverAppsMenu: false
            property bool hoverWifiMenu: false
            property bool hoverBluetoothMenu: false
            property bool hoverPowerMenu: false

            screen: modelData
            color: "transparent"
            exclusionMode: ExclusionMode.Normal
                exclusiveZone: 40
            implicitHeight: 448
            WlrLayershell.layer: WlrLayer.Top
            WlrLayershell.keyboardFocus: root.openMenu === "apps" ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.OnDemand
            WlrLayershell.namespace: "simple-top-bar"

            anchors {
                top: true
                left: true
                right: true
            }

            mask: root.openMenu.length > 0 ? fullRegion : barRegion
            Region {
                id: fullRegion
                width: window.width
                height: window.height
            }
            Region {
                id: barRegion
                width: window.width
                height: 40
            }

            function menuAreaHovered(menu) {
                if (menu === "apps") return hoverBar || hoverAppsButton || hoverAppsMenu;
                if (menu === "wifi") return hoverBar || hoverWifiButton || hoverWifiMenu;
                if (menu === "bluetooth") return hoverBar || hoverBluetoothButton || hoverBluetoothMenu;
                if (menu === "power") return hoverBar || hoverPowerButton || hoverPowerMenu;
                return false;
            }

            function updateMenuCloseTimer() {
                if (root.openMenu.length === 0) {
                    menuCloseTimer.stop();
                    return;
                }

                if (!hoverArmed) {
                    menuCloseTimer.stop();
                    return;
                }

                if (menuAreaHovered(root.openMenu))
                    menuCloseTimer.stop();
                else if (!menuCloseTimer.running)
                    menuCloseTimer.start();
            }

            Timer {
                id: menuCloseTimer
                interval: 180
                repeat: false
                onTriggered: root.closeMenu()
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.openMenu.length > 0
                acceptedButtons: Qt.AllButtons
                hoverEnabled: true
                onPressed: root.closeMenu()
                onExited: {
                    window.hoverBar = false;
                    window.hoverAppsButton = false;
                    window.hoverWifiButton = false;
                    window.hoverBluetoothButton = false;
                    window.hoverPowerButton = false;
                    window.hoverAppsMenu = false;
                    window.hoverWifiMenu = false;
                    window.hoverBluetoothMenu = false;
                    window.hoverPowerMenu = false;
                    window.updateMenuCloseTimer();
                }
            }

            Connections {
                target: root
                function onOpenMenuChanged() {
                    window.hoverArmed = root.openMenu.length > 0 && window.menuAreaHovered(root.openMenu);
                    window.updateMenuCloseTimer();
                }
            }

            Rectangle {
                id: bar
                anchors.top: parent.top
                anchors.topMargin: 2
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 8
                height: 36
                radius: 18
                color: root.barBg
                border.width: 1
                border.color: root.border
                clip: true

                HoverHandler {
                    onHoveredChanged: {
                        window.hoverBar = hovered;
                        window.updateMenuCloseTimer();
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 0
                    color: "transparent"
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    BarButton {
                        icon: "\ue5c3"
                        label: "Apps"
                        active: root.openMenu === "apps"
                        onEntered: root.showMenu("apps")
                        onClicked: root.toggleMenu("apps")
                        onHoverChanged: hovered => {
                            if (hovered) window.hoverArmed = true;
                            window.hoverAppsButton = hovered;
                            window.updateMenuCloseTimer();
                        }
                    }

                    WorkspaceDots {
                        screenName: window.modelData.name
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: Qt.formatDateTime(root.now, "h:mm")
                    color: root.text
                    font.family: root.fontName
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    BarButton {
                        icon: root.wifiEnabled ? "\ue63e" : "\ue648"
                        label: root.wifiEnabled
                            ? (root.connectedWifi ? "Wi-Fi: " + root.connectedWifi.name : "Wi-Fi")
                            : "Wi-Fi off"
                        active: root.openMenu === "wifi"
                        onEntered: root.showMenu("wifi")
                        onClicked: root.toggleMenu("wifi")
                        onHoverChanged: hovered => {
                            if (hovered) window.hoverArmed = true;
                            window.hoverWifiButton = hovered;
                            window.updateMenuCloseTimer();
                        }
                    }

                    BarButton {
                        icon: root.bluetoothAdapter && root.bluetoothAdapter.enabled ? "\ue1a7" : "\ue1a9"
                        label: root.bluetoothAdapter && root.bluetoothAdapter.enabled
                            ? "BT: " + root.connectedBluetoothDevices.length
                            : "BT off"
                        active: root.openMenu === "bluetooth"
                        onEntered: root.showMenu("bluetooth")
                        onClicked: root.toggleMenu("bluetooth")
                        onHoverChanged: hovered => {
                            if (hovered) window.hoverArmed = true;
                            window.hoverBluetoothButton = hovered;
                            window.updateMenuCloseTimer();
                        }
                    }

                    PowerBar {
                        visible: root.batteryPresent
                        onEntered: root.showMenu("power")
                        onClicked: root.toggleMenu("power")
                        onHoverChanged: hovered => {
                            if (hovered) window.hoverArmed = true;
                            window.hoverPowerButton = hovered;
                            window.updateMenuCloseTimer();
                        }
                    }

                    BarButton {
                        icon: "\uf8c7"
                        label: "Power"
                        active: root.openMenu === "power"
                        onEntered: root.showMenu("power")
                        onClicked: root.toggleMenu("power")
                        onHoverChanged: hovered => {
                            if (hovered) window.hoverArmed = true;
                            window.hoverPowerButton = hovered;
                            window.updateMenuCloseTimer();
                        }
                    }
                }
            }

            FocusScope {
                anchors.fill: parent
                focus: root.openMenu.length > 0

                Keys.onEscapePressed: root.closeMenu()
                Keys.onUpPressed: root.moveAppSelection(-1)
                Keys.onDownPressed: root.moveAppSelection(1)
                Keys.onReturnPressed: root.activateSelectedApp()
                Keys.onEnterPressed: root.activateSelectedApp()
            }

            MenuPanel {
                id: appMenu
                readonly property bool open: root.openMenu === "apps"
                property real slideY: open ? 0 : -12
                visible: open || opacity > 0.01
                opacity: open ? 1 : 0
                width: 280
                height: open ? appContent.implicitHeight + 12 : 0
                anchors.top: bar.bottom
                anchors.topMargin: open ? -1 : -9
                anchors.left: parent.left
                anchors.leftMargin: 16
                transform: Translate { y: appMenu.slideY }
                onSafeEntered: {
                    window.hoverArmed = true;
                    window.hoverAppsMenu = true;
                    window.updateMenuCloseTimer();
                }
                onSafeExited: {
                    window.hoverAppsMenu = false;
                    window.updateMenuCloseTimer();
                }

                Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                Behavior on slideY { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on anchors.topMargin { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

                Column {
                    id: appContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    spacing: 5

                    TextField {
                        id: appSearch
                        width: parent.width
                        height: 40
                        text: root.query
                        placeholderText: "Search apps"
                        placeholderTextColor: root.muted
                        color: root.text
                        selectionColor: root.activeBg
                        selectedTextColor: root.text
                        font.family: root.fontName
                        font.pixelSize: 14
                        background: Rectangle {
                            radius: 10
                            color: root.hoverBg
                            border.width: 1
                            border.color: root.border
                        }
                        onTextChanged: root.query = text
                        Keys.onEscapePressed: root.closeMenu()
                        Keys.onUpPressed: root.moveAppSelection(-1)
                        Keys.onDownPressed: root.moveAppSelection(1)
                        Keys.onReturnPressed: root.activateSelectedApp()
                        Keys.onEnterPressed: root.activateSelectedApp()

                        Connections {
                            target: root
                            function onOpenMenuChanged() {
                                if (root.openMenu === "apps")
                                    Qt.callLater(appSearch.forceActiveFocus);
                            }
                        }
                    }

                    ListView {
                        id: appList
                        width: parent.width
                        height: Math.min(contentHeight, 4 * 40)
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: Math.min(root.appResults.length, 4)

                        delegate: MenuRow {
                            required property int index
                            readonly property var app: root.appResults[index]

                            width: appList.width
                            label: app ? app.name : ""
                            sublabel: app ? (app.genericName || app.id || "") : ""
                            selected: index === root.selectedApp
                            onClicked: root.launchApp(app)

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onEntered: root.selectedApp = index
                                onClicked: root.launchApp(app)
                            }
                        }
                    }
                }
            }

            MenuPanel {
                id: wifiMenu
                readonly property bool open: root.openMenu === "wifi"
                property real slideY: open ? 0 : -12
                visible: open || opacity > 0.01
                opacity: open ? 1 : 0
                width: 240
                height: open ? wifiContent.implicitHeight + 12 : 0
                anchors.top: bar.bottom
                anchors.topMargin: open ? -1 : -9
                anchors.right: parent.right
                anchors.rightMargin: 62
                transform: Translate { y: wifiMenu.slideY }
                onSafeEntered: {
                    window.hoverArmed = true;
                    window.hoverWifiMenu = true;
                    window.updateMenuCloseTimer();
                }
                onSafeExited: {
                    window.hoverWifiMenu = false;
                    window.updateMenuCloseTimer();
                }

                Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                Behavior on slideY { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on anchors.topMargin { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

                Column {
                    id: wifiContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    spacing: 4

                    MenuRow {
                        width: parent.width
                        label: root.wifiEnabled ? "Turn Wi-Fi off" : "Turn Wi-Fi on"
                        sublabel: root.connectedWifi ? root.connectedWifi.name : "NetworkManager"
                        onClicked: if (typeof Networking !== "undefined" && Networking) Networking.wifiEnabled = !Networking.wifiEnabled
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: root.border
                        visible: root.wifiEnabled
                    }

                    Repeater {
                        model: root.wifiEnabled ? Math.min(root.wifiNetworks.length, 3) : 0

                        MenuRow {
                            required property int index
                            readonly property var networksSorted: root.wifiNetworks.slice().sort(function(a, b) {
                                return ((b ? b.signalStrength : 0) || 0) - ((a ? a.signalStrength : 0) || 0);
                            })
                            readonly property var network: networksSorted[index]

                            width: wifiContent.width
                            label: network ? network.name : ""
                            sublabel: network && network.connected ? "connected" : (network ? Math.round((network.signalStrength || 0) * 100) + "% signal" : "")
                            selected: network && network.connected
                            onClicked: if (network && typeof network.connect === "function") network.connect()
                        }
                    }
                }
            }

            MenuPanel {
                id: bluetoothMenu
                readonly property bool open: root.openMenu === "bluetooth"
                property real slideY: open ? 0 : -12
                visible: open || opacity > 0.01
                opacity: open ? 1 : 0
                width: 220
                height: open ? bluetoothContent.implicitHeight + 12 : 0
                anchors.top: bar.bottom
                anchors.topMargin: open ? -1 : -9
                anchors.right: parent.right
                anchors.rightMargin: 34
                transform: Translate { y: bluetoothMenu.slideY }
                onSafeEntered: {
                    window.hoverArmed = true;
                    window.hoverBluetoothMenu = true;
                    window.updateMenuCloseTimer();
                }
                onSafeExited: {
                    window.hoverBluetoothMenu = false;
                    window.updateMenuCloseTimer();
                }

                Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                Behavior on slideY { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on anchors.topMargin { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

                Column {
                    id: bluetoothContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    spacing: 4

                    MenuRow {
                        width: parent.width
                        label: root.bluetoothAdapter && root.bluetoothAdapter.enabled ? "Turn Bluetooth off" : "Turn Bluetooth on"
                        sublabel: root.bluetoothAdapter ? "Default adapter" : "No adapter"
                        onClicked: if (root.bluetoothAdapter) root.bluetoothAdapter.enabled = !root.bluetoothAdapter.enabled
                    }

                    Repeater {
                        model: Math.min(root.connectedBluetoothDevices.length, 3)

                        MenuRow {
                            required property int index
                            readonly property var device: root.connectedBluetoothDevices[index]

                            width: bluetoothContent.width
                            label: device ? (device.name || device.address || "Bluetooth device") : ""
                            sublabel: "connected"
                            selected: true
                            onClicked: if (device && typeof device.disconnect === "function") device.disconnect()
                        }
                    }

                    Text {
                        visible: root.bluetoothAdapter && root.bluetoothAdapter.enabled && root.connectedBluetoothDevices.length === 0
                        width: parent.width
                        text: "No connected devices"
                        color: root.muted
                        font.family: root.fontName
                        font.pixelSize: 12
                    }
                }
            }

            MenuPanel {
                id: powerMenu
                readonly property bool open: root.openMenu === "power"
                property real slideY: open ? 0 : -12
                visible: open || opacity > 0.01
                opacity: open ? 1 : 0
                width: 180
                height: open ? powerContent.implicitHeight + 12 : 0
                anchors.top: bar.bottom
                anchors.topMargin: open ? -1 : -9
                anchors.right: parent.right
                anchors.rightMargin: 4
                transform: Translate { y: powerMenu.slideY }
                onSafeEntered: {
                    window.hoverArmed = true;
                    window.hoverPowerMenu = true;
                    window.updateMenuCloseTimer();
                }
                onSafeExited: {
                    window.hoverPowerMenu = false;
                    window.updateMenuCloseTimer();
                }

                Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                Behavior on slideY { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on anchors.topMargin { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

                Column {
                    id: powerContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    spacing: 4

                    MenuRow { width: parent.width; label: "Lock"; onClicked: root.runPower("lock") }
                    MenuRow { width: parent.width; label: "Sleep"; onClicked: root.runPower("sleep") }
                    MenuRow { width: parent.width; label: root.confirmPower === "reboot" ? "Click again" : "Restart"; danger: true; selected: root.confirmPower === "reboot"; onClicked: root.runPower("reboot") }
                    MenuRow { width: parent.width; label: root.confirmPower === "shutdown" ? "Click again" : "Shutdown"; danger: true; selected: root.confirmPower === "shutdown"; onClicked: root.runPower("shutdown") }
                }
            }
        }
    }
}
