import QtQuick 2.3
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2
import "../components"
import Nymea 1.0

WizardPageBase {
    id: root
    title: qsTr("Welcome")
    text: qsTr("This wizard will guide you through the process of setting up a new nymea system.")
    showBackButton: false
    showExtraButton: true
    extraButtonText: qsTr("Demo mode")

    onNext: pageStack.push(connectionSelectionComponent)
    onExtraButtonPressed: {
        var host = nymeaDiscovery.nymeaHosts.createWanHost("Demo server", "nymea://nymea.nymea.io:2222")
        engine.jsonRpcClient.connectToHost(host)
    }

    content: ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Style.margins
        anchors.rightMargin: Style.margins

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Style.hugeIconSize * 2
            ColorIcon {
                anchors.centerIn: parent
                size: Math.min(parent.width, parent.height, Style.hugeIconSize * 2)
                name: "nymea-logo"
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            horizontalAlignment: Text.AlignHCenter
            text: "nymea"
            font: Style.hugeFont
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: Text.WordWrap
            font: Style.smallFont
            text: qsTr("In order to use nymea, you will need to install nymea:core on a computer in your network. This can be a Raspberry Pi or any generic Linux computer.")
            horizontalAlignment: Text.AlignHCenter
        }
        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: Text.WordWrap
            font: Style.smallFont
            text: qsTr("Please follow the installation instructions on %1 to install a nymea system.").arg('<a href="https://nymea.io/documentation/users/installation/core">nymea.io</a>')
            horizontalAlignment: Text.AlignHCenter
            onLinkActivated: Qt.openUrlExternally(link)
        }
    }

    Component {
        id: connectionSelectionComponent
        WizardPageBase {
            title: qsTr("Connectivity")
            text: qsTr("How would you like to connect nymea to your network?")

            nextButtonText: qsTr("Skip")

            onNext: pageStack.push(selectInstanceComponent)
            onBack: pageStack.pop()

            content: ColumnLayout {
                anchors {
                    top: parent.top;
                    bottom: parent.bottom;
                    horizontalCenter: parent.horizontalCenter
                    margins: Style.margins
                }
                width: Math.min(500, parent.width)

                BigTile {
                    Layout.fillWidth: true

                    onClicked: pageStack.push(wiredInstructionsComponent)

                    contentItem: RowLayout {
                        spacing: Style.margins
                        ColorIcon {
                            size: Style.hugeIconSize
                            name: "connections/network-wired"
                            color: Style.accentColor
                        }
                        ColumnLayout {
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Wired network")
                            }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Connect nymea to your network using a network cable. This is recommended for best performance.")
                                font: Style.smallFont
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                BigTile {
                    Layout.fillWidth: true

                    onClicked: pageStack.push(wirelessInstructionsComponent)

                    contentItem: RowLayout {
                        spacing: Style.margins
                        ColorIcon {
                            size: Style.hugeIconSize
                            name: "connections/network-wifi"
                            color: Style.accentColor
                        }
                        ColumnLayout {
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Wireless network")
                            }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Connect nymea to your WiFi network.")
                                font: Style.smallFont
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.margins: Style.smallMargins
                    wrapMode: Text.WordWrap
                    text: qsTr("If your nymea system is already connected to the network you can skip this step.")
                    horizontalAlignment: Qt.AlignHCenter
                    font: Style.smallFont
                }
            }
        }
    }

    Component {
        id: selectInstanceComponent
        WizardPageBase {
            title: qsTr("Connection")
            text: qsTr("Connecting to the nymea system.")
            nextButtonText: qsTr("Manual connection")
            onNext: pageStack.push(manualConnectionComponent)

            onBack: pageStack.pop()

            content: ColumnLayout {
                anchors {
                    top: parent.top
                    bottom: parent.bottom
                    horizontalCenter: parent.horizontalCenter
                }
                width: Math.min(500, parent.width)

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: NymeaHostsFilterModel {
                        id: hostsProxy
                        discovery: nymeaDiscovery
                        showUnreachableBearers: false
                        jsonRpcClient: engine.jsonRpcClient
                        showUnreachableHosts: false
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: hostsProxy.count == 0
                        spacing: Style.margins
                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.margins: Style.margins
                            text: qsTr("Please wait while your nymea system is being discovered.")
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }


                    delegate: NymeaSwipeDelegate {
                        id: nymeaHostDelegate
                        width: parent.width
                        property var nymeaHost: hostsProxy.get(index)
                        property string defaultConnectionIndex: {
                            if (!nymeaHost) {
                                return -1
                            }

                            var bestIndex = -1
                            var bestPriority = 0;
                            for (var i = 0; i < nymeaHost.connections.count; i++) {
                                var connection = nymeaHost.connections.get(i);
                                if (bestIndex === -1 || connection.priority > bestPriority) {
                                    bestIndex = i;
                                    bestPriority = connection.priority;
                                }
                            }
                            return bestIndex;
                        }
                        iconName: {
                            if (!nymeaHost) {
                                return
                            }

                            switch (nymeaHost.connections.get(defaultConnectionIndex).bearerType) {
                            case Connection.BearerTypeLan:
                            case Connection.BearerTypeWan:
                                if (engine.jsonRpcClient.availableBearerTypes & NymeaConnection.BearerTypeEthernet != NymeaConnection.BearerTypeNone) {
                                    return "/ui/images/connections/network-wired.svg"
                                }
                                return "/ui/images/connections/network-wifi.svg";
                            case Connection.BearerTypeBluetooth:
                                return "/ui/images/connections/bluetooth.svg";
                            case Connection.BearerTypeCloud:
                                return "/ui/images/connections/cloud.svg"
                            case Connection.BearerTypeLoopback:
                                return "qrc:/styles/%1/logo.svg".arg(styleController.currentStyle)
                            }
                            return ""
                        }
                        text: model.name
                        subText: nymeaHost ? nymeaHost.connections.get(defaultConnectionIndex).url : ""
                        wrapTexts: false
                        prominentSubText: false
                        progressive: false
                        property bool isSecure: nymeaHost && nymeaHost.connections.get(defaultConnectionIndex).secure
                        property bool isOnline: nymeaHost && nymeaHost.connections.get(defaultConnectionIndex).bearerType !== Connection.BearerTypeWan ? nymeaHost.connections.get(defaultConnectionIndex).online : true
                        tertiaryIconName: isSecure ? "/ui/images/connections/network-secure.svg" : ""
                        secondaryIconName: !isOnline ? "/ui/images/connections/cloud-error.svg" : ""
                        secondaryIconColor: "red"

                        onClicked: {
                            engine.jsonRpcClient.connectToHost(nymeaHostDelegate.nymeaHost)
                        }

                        contextOptions: [
                            {
                                text: qsTr("Info"),
                                icon: Qt.resolvedUrl("/ui/images/info.svg"),
                                callback: function() {
                                    var nymeaHost = hostsProxy.get(index);
                                    var connectionInfoDialog = Qt.createComponent("/ui/components/ConnectionInfoDialog.qml")
                                    var popup = connectionInfoDialog.createObject(app,{nymeaHost: nymeaHost})
                                    popup.open()
                                }
                            }
                        ]
                    }
                }
            }
        }
    }

    Component {
        id: manualConnectionComponent
        WizardPageBase {
            title: qsTr("Manual connection")
            text: qsTr("Please enter the connection information for your nymea system")
            onBack: pageStack.pop()

            onNext: {
                var rpcUrl
                var hostAddress
                var port

                // Set default to placeholder
                if (addressTextInput.text === "") {
                    hostAddress = addressTextInput.placeholderText
                } else {
                    hostAddress = addressTextInput.text
                }

                if (portTextInput.text === "") {
                    port = portTextInput.placeholderText
                } else {
                    port = portTextInput.text
                }

                if (connectionTypeComboBox.currentIndex == 0) {
                    if (secureCheckBox.checked) {
                        rpcUrl = "nymeas://" + hostAddress + ":" + port
                    } else {
                        rpcUrl = "nymea://" + hostAddress + ":" + port
                    }
                } else if (connectionTypeComboBox.currentIndex == 1) {
                    if (secureCheckBox.checked) {
                        rpcUrl = "wss://" + hostAddress + ":" + port
                    } else {
                        rpcUrl = "ws://" + hostAddress + ":" + port
                    }
                }

                print("Try to connect ", rpcUrl)
                var host = nymeaDiscovery.nymeaHosts.createWanHost("Manual connection", rpcUrl);
                engine.jsonRpcClient.connectToHost(host)
            }

            content: ColumnLayout {
                anchors {
                    top: parent.top
                    bottom: parent.bottom
                    horizontalCenter: parent.horizontalCenter
                }
                width: Math.min(500, parent.width - Style.margins * 2)
                GridLayout {
                    columns: 2

                    Label {
                        text: qsTr("Protocol")
                    }

                    ComboBox {
                        id: connectionTypeComboBox
                        Layout.fillWidth: true
                        model: [ qsTr("TCP"), qsTr("Websocket") ]
                    }

                    Label { text: qsTr("Address:") }
                    TextField {
                        id: addressTextInput
                        objectName: "addressTextInput"
                        Layout.fillWidth: true
                        placeholderText: "127.0.0.1"
                    }

                    Label { text: qsTr("Port:") }
                    TextField {
                        id: portTextInput
                        Layout.fillWidth: true
                        placeholderText: connectionTypeComboBox.currentIndex === 0 ? "2222" : "4444"
                        validator: IntValidator{bottom: 1; top: 65535;}
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Encrypted connection:")
                    }
                    CheckBox {
                        id: secureCheckBox
                        checked: true
                    }
                }
            }
        }
    }

    Component {
        id: wiredInstructionsComponent
        WizardPageBase {
            title: qsTr("Wired connection")
            text: qsTr("Connect the nymea system to your network using an ethernet cable and turn it on.")

            onNext: pageStack.push(selectInstanceComponent)
            onBack: pageStack.pop()

            content: Image {
                anchors.fill: parent
                anchors.margins: Style.margins
                fillMode: Image.PreserveAspectFit
                sourceSize.width: width
                source: "/ui/images/setupwizard/wired-connection.svg"
            }
        }
    }

    Component {
        id: wirelessInstructionsComponent
        WizardPageBase {
            title: qsTr("Wireless connection")
            text: qsTr("Turn the nymea system on by connecting the power cable and wait for it to start up.")

            onNext: pageStack.push(wirelessBluetoothDiscoveryComponent)
            onBack: pageStack.pop()

            content: Image {
                anchors.fill: parent
                anchors.margins: Style.margins
                fillMode: Image.PreserveAspectFit
                sourceSize.width: width
                source: "/ui/images/setupwizard/wireless-connection.svg"
            }
        }
    }
    Component {
        id: wirelessBluetoothDiscoveryComponent
        WizardPageBase {
            id: wirelessBluetoothDiscoveryPage
            title: qsTr("Wireless setup")
            text: qsTr("Searching for the nymea system...")
            showNextButton: false
            onBack: pageStack.pop()

            BtWiFiSetup {
                id: wifiSetup

                onBluetoothStatusChanged: {
                    print("status changed", status)
                    switch (status) {
                    case BtWiFiSetup.BluetoothStatusDisconnected:
                        pageStack.pop(wirelessBluetoothDiscoveryPage)
                        break;
                    case BtWiFiSetup.BluetoothStatusConnectingToBluetooth:
                        break;
                    case BtWiFiSetup.BluetoothStatusConnectedToBluetooth:
                        break;
                    case BtWiFiSetup.BluetoothStatusLoaded:
                        if (!wifiSetup.networkingEnabled) {
                            wifiSetup.networkingEnabled = true;
                        }
                        if (!wifiSetup.wirelessEnabled) {
                            wifiSetup.wirelessEnabled = true;
                        }
                        pageStack.pop(wirelessBluetoothDiscoveryPage, StackView.Immediate)
                        pageStack.push(wirelessSelectWifiComponent, {wifiSetup: wifiSetup})
                        break;
                    }
                }
                onBluetoothConnectionError: {
                    pageStack.pop(wirelessBluetoothDiscoveryPage, StackView.Immediate)
                    pageStack.push(wirelessBtErrorComponent)
                }

                onCurrentConnectionChanged: {
                    if (wifiSetup.currentConnection) {
                        print("**** connected!")
                        pageStack.push(wirelessConnectionCompletedComponent, {wifiSetup: wifiSetup})
                    }
                }
                onWirelessStatusChanged: {
                    print("Wireless status changed:", wifiSetup.networkStatus)
                    if (wifiSetup.wirelessStatus === BtWiFiSetup.WirelessStatusFailed) {
                        pageStack.pop()
                    }
                }
            }

            BluetoothDiscovery {
                id: bluetoothDiscovery
                discoveryEnabled: pageStack.currentItem === wirelessBluetoothDiscoveryPage
            }

            content: ListView {
                anchors {
                    top: parent.top
                    bottom: parent.bottom
                    horizontalCenter: parent.horizontalCenter
                }
                width: Math.min(500, parent.width)

                model: BluetoothDeviceInfosProxy {
                    id: deviceInfosProxy
                    model: bluetoothDiscovery.deviceInfos
                    filterForLowEnergy: true
                    filterForServiceUUID: "e081fec0-f757-4449-b9c9-bfa83133f7fc"
                    nameWhitelist: ["BT WLAN setup"]
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    visible: bluetoothDiscovery.discovering && deviceInfosProxy.count == 0
                }

                delegate: NymeaSwipeDelegate {
                    width: parent.width
                    iconName: Qt.resolvedUrl("/ui/images/connections/bluetooth.svg")
                    text: model.name
                    subText: model.address

                    onClicked: {
                        wifiSetup.connectToDevice(deviceInfosProxy.get(index))
                        pageStack.push(wirelessBluetoothConnectingComponent)
                    }
                }

                ColumnLayout {
                    width: parent.width - Style.margins * 2
                    anchors.centerIn: parent
                    spacing: Style.bigMargins
                    visible: !bluetoothDiscovery.bluetoothAvailable || !bluetoothDiscovery.bluetoothEnabled

                    ColorIcon {
                        name: "/ui/images/connections/bluetooth.svg"
                        size: Style.iconSize * 5
                        color: !bluetoothDiscovery.bluetoothAvailable ? Style.red : Style.gray
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: !bluetoothDiscovery.bluetoothAvailable
                              ? qsTr("Bluetooth doesn't seem to be available on this system.")
                              : qsTr("Bluetooth is turned off. Please enable Bluetooth on this device.")
                    }
                }

            }
        }
    }

    Component {
        id: wirelessBluetoothConnectingComponent
        WizardPageBase {
            title: qsTr("Wireless setup")
            text: qsTr("Connecting to the nymea system...")
            showNextButton: false

            content: BusyIndicator {
                anchors.centerIn: parent
            }
        }
    }

    Component {
        id: wirelessSelectWifiComponent
        WizardPageBase {
            title: qsTr("Wireless setup")
            text: qsTr("Select the WiFi you want to use.")
            showNextButton: false

            property var wifiSetup: null

            content: ListView {
                anchors {
                    top: parent.top
                    bottom: parent.bottom
                    horizontalCenter: parent.horizontalCenter
                }
                width: Math.min(500, parent.width)

                model: wifiSetup.accessPoints
                clip: true

                delegate: NymeaItemDelegate {
                    width: parent.width

                    text: model.ssid !== "" ? model.ssid : qsTr("Hidden Network")
                    subText: model.hostAddress

                    iconColor: model.selectedNetwork ? Style.accentColor : "#808080"
                    iconName:  {
                        if (model.protected) {
                            if (model.signalStrength <= 25)
                                return  Qt.resolvedUrl("/ui/images/connections/nm-signal-25-secure.svg")

                            if (model.signalStrength <= 50)
                                return  Qt.resolvedUrl("/ui/images/connections/nm-signal-50-secure.svg")

                            if (model.signalStrength <= 75)
                                return  Qt.resolvedUrl("/ui/images/connections/nm-signal-75-secure.svg")

                            if (model.signalStrength <= 100)
                                return  Qt.resolvedUrl("/ui/images/connections/nm-signal-100-secure.svg")

                        } else {

                            if (model.signalStrength <= 25)
                                return  Qt.resolvedUrl("/ui/images/connections/nm-signal-25.svg")

                            if (model.signalStrength <= 50)
                                return  Qt.resolvedUrl("/ui/images/connections/nm-signal-50.svg")

                            if (model.signalStrength <= 75)
                                return  Qt.resolvedUrl("/ui/images/connections/nm-signal-75.svg")

                            if (model.signalStrength <= 100)
                                return  Qt.resolvedUrl("/ui/images/connections/nm-signal-100.svg")

                        }
                    }

                    onClicked: {
                        print("Connect to ", model.ssid, " --> ", model.macAddress)
                        if (model.selectedNetwork) {
                            pageStack.push(networkInformationPage, { ssid: model.ssid})
                        } else {
                            pageStack.push(wirelessAuthenticationComponent, { wifiSetup: wifiSetup, ssid: model.ssid })
                        }
                    }
                }
            }
        }
    }

    Component {
        id: wirelessAuthenticationComponent
        WizardPageBase {
            title: qsTr("Wireless setup")
            text: qsTr("Enter the password for the WiFi network.")
            showNextButton: passwordTextField.isValidPassword

            onNext: {
                print("connecting to", ssid, passwordTextField.password)
                wifiSetup.connectDeviceToWiFi(ssid, passwordTextField.password)
                pageStack.push(wirelessConnectingWiFiComponent)
            }

            property BtWiFiSetup wifiSetup: null
            property string ssid: ""

            content: ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(500, parent.width - Style.margins * 2)

                Label {
                    Layout.fillWidth: true
                    text: ssid
                }

                PasswordTextField {
                    id: passwordTextField
                    Layout.fillWidth: true
                    signup: false
                    requireLowerCaseLetter: false
                    requireUpperCaseLetter: false
                    requireNumber: false
                    requireSpecialChar: false
                    minPasswordLength: 8
                }
            }
        }
    }

    Component {
        id: wirelessBtErrorComponent
        WizardPageBase {
            title: qsTr("Wireless setup")
            text: qsTr("An error happened in the Bluetooth connection. Please try again.")
            showNextButton: false
            onBack: pageStack.pop()
        }
    }

    Component {
        id: wirelessConnectingWiFiComponent
        WizardPageBase {
            title: qsTr("Wireless setup")
            text: qsTr("Please wait while the nymea system is being connected to the WiFi.")
            showNextButton: false
            onBack: pageStack.pop()

            content: BusyIndicator {
                anchors.centerIn: parent
            }
        }
    }

    Component {
        id: wirelessConnectionCompletedComponent
        WizardPageBase {
            id: wirelessConnectionCompletedPage
            title: qsTr("Wireless setup")
            text: qsTr("The nymea system has been connected successfully.")

            showNextButton: host != null
            showBackButton: false

            onNext: engine.jsonRpcClient.connectToHost(host)

            property BtWiFiSetup wifiSetup: null

            property NymeaHost host: null

            Component.onCompleted: updateNextButton()

            Connections {
                target: nymeaDiscovery.nymeaHosts
                onCountChanged: updateNextButton();
            }

            function updateNextButton() {
                if (!wifiSetup.currentConnection) {
                    wirelessConnectionCompletedPage.host = null;
                    return;
                }

                // FIXME: We should rather look for the UUID here, but nymea-networkmanager doesn't support getting us the nymea uuid (yet)
                for (var i = 0; i < nymeaDiscovery.nymeaHosts.count; i++) {
                    for (var j = 0; j < nymeaDiscovery.nymeaHosts.get(i).connections.count; j++) {
                        if (nymeaDiscovery.nymeaHosts.get(i).connections.get(j).url.toString().indexOf(wifiSetup.currentConnection.hostAddress) >= 0) {
                            wirelessConnectionCompletedPage.host = nymeaDiscovery.nymeaHosts.get(i)
                            return;
                        }
                    }
                    nymeaDiscovery.nymeaHosts.get(i).connections.countChanged.connect(function() {
                        updateNextButton();
                    })
                }
                wirelessConnectionCompletedPage.host = null;
            }

            content: ColumnLayout {
                width: Math.min(500, parent.width - Style.margins * 2)
                anchors.centerIn: parent
                spacing: Style.margins
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("You can now go ahead and configure your nymea system.")
                    visible: wirelessConnectionCompletedPage.host != null
                }
                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    visible: wirelessConnectionCompletedPage.host == null
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: wirelessConnectionCompletedPage.host == null
                    text: qsTr("Waiting for your nymea setup to appear in the network.")
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
