import QtQuick 2.0
import QtCharts 2.3
import QtQuick.Layouts 1.2
import QtQuick.Controls 2.3
import Nymea 1.0
import "qrc:/ui/components"

Item {
    id: root

    property PowerBalanceLogs energyLogs: PowerBalanceLogs {
        id: powerBalanceLogs
        engine: _engine
        startTime: dateTimeAxis.min
    }

    Component.onCompleted: {
        for (var i = 0; i < powerBalanceLogs.count; i++) {
            var entry = energyLogs.powerBalanceLogs.get(i);
            productionSeries.addEntry(entry)
            selfConsumptionSeries.addEntry(entry)
            storageSeries.addEntry(entry)
            acquisitionSeries.addEntry(entry)
        }
    }

    Connections {
        target: powerBalanceLogs
        onEntryAdded: {
            productionSeries.addEntry(entry)
            selfConsumptionSeries.addEntry(entry)
            storageSeries.addEntry(entry)
            acquisitionSeries.addEntry(entry)

            if (dateTimeAxis.now < entry.timestamp) {
                dateTimeAxis.now = entry.timestamp
                zeroSeries.update(entry.timestamp)
            }
        }
    }

    Timer {
        interval: 60000
        repeat: true
        onTriggered: {
            var now = new Date()
            if (dateTimeAxis.now < now) {
                dateTimeAxis.now = now
                zeroSeries.update(now)
            }
        }
    }

    ChartView {
        id: chartView
        anchors.fill: parent

        backgroundColor: "transparent"
        margins.left: 0
        margins.right: 0
        margins.bottom: 0
        margins.top: 0

        title: qsTr("My production history")
        titleColor: Style.foregroundColor

        legend.alignment: Qt.AlignBottom
        legend.labelColor: Style.foregroundColor
        legend.font: Style.extraSmallFont


        ValueAxis {
            id: valueAxis
            min: 0
            max: Math.ceil(-powerBalanceLogs.minValue / 1000) * 1000
            labelFormat: ""
            gridLineColor: Style.tileOverlayColor
            labelsVisible: false
            lineVisible: false
            titleVisible: false
            shadesVisible: false
        }

        Item {
            id: labelsLayout
            x: Style.smallMargins
            y: chartView.plotArea.y
            height: chartView.plotArea.height
            width: chartView.plotArea.x - x
            Repeater {
                model: valueAxis.tickCount
                delegate: Label {
                    y: parent.height / (valueAxis.tickCount - 1) * index - font.pixelSize / 2
                    width: parent.width - Style.smallMargins
                    horizontalAlignment: Text.AlignRight
                    text: ((valueAxis.max - (index * valueAxis.max / (valueAxis.tickCount - 1))) / 1000).toFixed(2) + "kW"
                    verticalAlignment: Text.AlignTop
                    font: Style.extraSmallFont
                }
            }
        }

        DateTimeAxis {
            id: dateTimeAxis
            property date now: new Date()
            min: {
                var date = new Date(now);
                date.setTime(date.getTime() - (1000 * 60 * 60 * 24) + 2000);
                return date;
            }
            max: {
                var date = new Date(now);
                date.setTime(date.getTime() + 2000)
                return date;
            }
            format: "hh:mm"
            labelsFont: Style.extraSmallFont
            gridVisible: false
            minorGridVisible: false
            lineVisible: false
            shadesVisible: false
            labelsColor: Style.foregroundColor
        }

        // For debugging, to see if the other maths line up with the plain production graph
        AreaSeries {
            id: productionSeries
            axisX: dateTimeAxis
            axisY: valueAxis
            color: "blue"
            borderWidth: 0
            borderColor: color
            opacity: .5
            name: "Total production"
            visible: false

            function calculateValue(entry) {
                return Math.abs(Math.min(0, entry.production))
            }
            function addEntry(entry) {
                productionUpperSeries.append(entry.timestamp.getTime(), calculateValue(entry))
            }

            lowerSeries: zeroSeries
            upperSeries: LineSeries {
                id: productionUpperSeries
            }
        }

        AreaSeries {
            id: selfConsumptionSeries
            axisX: dateTimeAxis
            axisY: valueAxis
            color: Style.red
            borderWidth: 0
            borderColor: color
            name: qsTr("Consumed")
    //        visible: false

            function calculateValue(entry) {
                return Math.abs(Math.min(0, entry.production)) - Math.abs(Math.min(0, entry.acquisition)) - Math.max(0, entry.storage)
            }

            function addEntry(entry) {
                selfConsumptionUpperSeries.append(entry.timestamp.getTime(), calculateValue(entry))
            }

            lowerSeries: LineSeries {
                id: zeroSeries
                XYPoint { x: dateTimeAxis.min.getTime(); y: 0 }
                XYPoint { x: dateTimeAxis.max.getTime(); y: 0 }
                function update(timestamp) {
                    append(timestamp, 0);
                    removePoints(1,1);
                }
            }

            upperSeries: LineSeries {
                id: selfConsumptionUpperSeries
            }
        }

        AreaSeries {
            id: storageSeries
            axisX: dateTimeAxis
            axisY: valueAxis
            color: Style.orange
            borderWidth: 0
            borderColor: color
    //        visible: false
            name: qsTr("To battery")


            function calculateValue(entry) {
                return selfConsumptionSeries.calculateValue(entry) + Math.abs(Math.max(0, entry.storage));
            }

            function addEntry(entry) {
                storageUpperSeries.append(entry.timestamp.getTime(), calculateValue(entry))
            }

            lowerSeries: selfConsumptionUpperSeries
            upperSeries: LineSeries {
                id: storageUpperSeries
            }
        }


        AreaSeries {
            id: acquisitionSeries
            axisX: dateTimeAxis
            axisY: valueAxis
            color: Style.green
            borderWidth: 0
            borderColor: color
            name: qsTr("To grid")
    //        visible: false

            function calculateValue(entry) {
                return storageSeries.calculateValue(entry) + Math.abs(Math.min(0, entry.acquisition))
            }
            function addEntry(entry) {
                acquisitionUpperSeries.append(entry.timestamp.getTime(), calculateValue(entry))
            }

            lowerSeries: storageUpperSeries
            upperSeries: LineSeries {
                id: acquisitionUpperSeries
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        anchors.leftMargin: chartView.plotArea.x
        anchors.topMargin: chartView.plotArea.y
        anchors.rightMargin: chartView.width - chartView.plotArea.width - chartView.plotArea.x
        anchors.bottomMargin: chartView.height - chartView.plotArea.height - chartView.plotArea.y

        hoverEnabled: true

        Timer {
            interval: 300
            running: mouseArea.pressed
            onTriggered: mouseArea.preventStealing = true
        }
        onReleased: mouseArea.preventStealing = false

        Rectangle {
            height: parent.height
            width: 1
            color: Style.foregroundColor
            x: Math.min(mouseArea.width, Math.max(0, mouseArea.mouseX))
            visible: mouseArea.containsMouse || mouseArea.preventStealing
        }

        NymeaToolTip {
            id: toolTip
            visible: mouseArea.containsMouse || mouseArea.preventStealing

            backgroundItem: chartView
            backgroundRect: Qt.rect(mouseArea.x + toolTip.x, mouseArea.y + toolTip.y, toolTip.width, toolTip.height)

            property int idx: productionUpperSeries.count - Math.floor(mouseArea.mouseX * productionUpperSeries.count / mouseArea.width)
            property int seriesIndex: Math.min(productionUpperSeries.count - 1, Math.max(0, productionUpperSeries.count - idx))

            property int xOnRight: Math.max(0, mouseArea.mouseX) + Style.smallMargins
            property int xOnLeft: Math.min(mouseArea.mouseX, mouseArea.width) - Style.smallMargins - width
            x: xOnRight + width < mouseArea.width ? xOnRight : xOnLeft
            property double maxValue: productionUpperSeries.at(seriesIndex).y
            y: Math.min(Math.max(mouseArea.height - (maxValue * mouseArea.height / valueAxis.max) - height - Style.margins, 0), mouseArea.height - height)

            width: tooltipLayout.implicitWidth + Style.smallMargins * 2
            height: tooltipLayout.implicitHeight + Style.smallMargins * 2

            ColumnLayout {
                id: tooltipLayout
                anchors {
                    left: parent.left
                    top: parent.top
                    margins: Style.smallMargins
                }
                Label {
                    text: new Date(selfConsumptionUpperSeries.at(toolTip.seriesIndex).x).toLocaleString(Qt.locale(), Locale.ShortFormat)
                    font: Style.smallFont
                }

                RowLayout {
                    Rectangle {
                        width: Style.extraSmallFont.pixelSize
                        height: width
                        color: Style.green
                    }

                    Label {
                        text: qsTr("Consumed: %1 kW").arg(selfConsumptionUpperSeries.at(toolTip.seriesIndex).y.toFixed(2))
                        font: Style.extraSmallFont
                    }
                }
                RowLayout {
                    Rectangle {
                        width: Style.extraSmallFont.pixelSize
                        height: width
                        color: Style.orange
                    }

                    Label {
                        text: qsTr("To battery: %1 kW").arg(storageUpperSeries.at(toolTip.seriesIndex).y.toFixed(2))
                        font: Style.extraSmallFont
                    }
                }
                RowLayout {
                    Rectangle {
                        width: Style.extraSmallFont.pixelSize
                        height: width
                        color: Style.red
                    }

                    Label {
                        text: qsTr("To grid: %1 kW").arg(acquisitionUpperSeries.at(toolTip.seriesIndex).y.toFixed(2))
                        font: Style.extraSmallFont
                    }
                }
            }
        }
    }
}



