// Interface tactile du client RemoteRig.
// Pensee pour le pouce : le PTT occupe le quart bas de l'ecran, la frequence
// est lisible a bout de bras, et tout le reste vit dans un tiroir.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window
import QtQuick.Layouts
import RemoteRig 1.0

ApplicationWindow {
    id: win
    visible: true
    width: 420
    height: 820
    title: "RemoteRig"
    visibility: Window.FullScreen

    // Quatre palettes, chacune pour une situation d'exploitation reelle.
    // La variante rouge preserve la vision nocturne, la variante contrastee
    // reste lisible en plein soleil.
    readonly property var palettes: [
        { key: "night",    dark: true,  bg: "#1c222c", panel: "#252c38",
          accent: "#f0c674", text: "#e6ebf2", dim: "#8c9aac",
          tx: "#dc4e4e", rx: "#2f4a2f" },
        { key: "red",      dark: true,  bg: "#140a0a", panel: "#241010",
          accent: "#ff6b52", text: "#ffd5cb", dim: "#a86a5e",
          tx: "#b02020", rx: "#3a1414" },
        { key: "contrast", dark: true,  bg: "#000000", panel: "#101010",
          accent: "#ffd400", text: "#ffffff", dim: "#b8b8b8",
          tx: "#ff2d2d", rx: "#0d3f0d" },
        { key: "day",      dark: false, bg: "#eef1f5", panel: "#ffffff",
          accent: "#a06000", text: "#1a1f27", dim: "#5a6572",
          tx: "#c02020", rx: "#cde4cd" }
    ]
    readonly property var pal: palettes[Math.max(0, Math.min(Station.theme, palettes.length - 1))]

    readonly property color bgDark:   pal.bg
    readonly property color panel:    pal.panel
    readonly property color amber:    pal.accent
    readonly property color greenTx:  pal.rx
    readonly property color redTx:    pal.tx
    readonly property color dim:      pal.dim
    readonly property real  gap:      12


    // Les controles Qt Quick suivent la meme palette que nos propres formes.
    Material.theme: pal.dark ? Material.Dark : Material.Light
    Material.accent: pal.accent
    Material.background: pal.panel
    Material.foreground: pal.text

    color: win.bgDark

    // Relief : un degrade du clair vers le sombre, un liseré lumineux en haut
    // et un contour assombri. Rien d'autre que des Rectangle, donc aucun
    // greffon de plus a embarquer — et c'est justement un greffon manquant qui
    // faisait echouer le demarrage.
    component ReliefButton: Rectangle {
        id: relief
        property alias text: reliefLabel.text
        property alias fontSize: reliefLabel.font.pixelSize
        property bool down: reliefArea.pressed
        property color baseColor: win.panel
        property bool active: true
        signal clicked()

        implicitHeight: 44
        radius: 8
        opacity: relief.active ? 1.0 : 0.45

        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: relief.down ? Qt.darker(relief.baseColor, 1.2)
                                   : Qt.lighter(relief.baseColor, 1.25)
            }
            GradientStop {
                position: 1.0
                color: relief.down ? Qt.lighter(relief.baseColor, 1.1)
                                   : Qt.darker(relief.baseColor, 1.25)
            }
        }
        border.color: Qt.darker(relief.baseColor, relief.down ? 1.5 : 1.4)
        border.width: 1

        // Biseau clair sur l'arete superieure.
        Rectangle {
            anchors { top: parent.top; left: parent.left; right: parent.right
                      leftMargin: parent.radius; rightMargin: parent.radius }
            height: 1
            color: "#40ffffff"
            visible: !relief.down
        }

        Label {
            id: reliefLabel
            anchors.centerIn: parent
            anchors.verticalCenterOffset: relief.down ? 1 : 0
            color: win.pal.text
            font.bold: true
            font.pixelSize: 14
        }

        MouseArea {
            id: reliefArea
            anchors.fill: parent
            enabled: relief.active
            onClicked: relief.clicked()
        }
    }

    // Ombre portee sans effet graphique : un rectangle decale derriere.
    // Moins riche qu'un flou, mais disponible partout et gratuit a l'affichage.
    component DropShadow: Rectangle {
        property real offset: 4
        color: "#60000000"
        z: -1
        y: offset
    }

    // Vumetre a maintien de crete. Une simple ProgressBar ne montre que
    // l'instant ; regler un niveau micro demande de voir jusqu'ou il est monte,
    // et de savoir s'il a touche la butee.
    component LevelMeter: Item {
        property real level: 0
        property bool clipped: false

        implicitHeight: 14

        // La crete tient une seconde et demie puis retombe doucement, pour ne
        // pas rester accrochee a un claquement isole.
        property real peak: 0
        onLevelChanged: {
            if (level >= peak) { peak = level; holdTimer.restart() }
        }
        Timer {
            id: holdTimer
            interval: 1500
            onTriggered: fallTimer.start()
        }
        Timer {
            id: fallTimer
            interval: 60
            repeat: true
            onTriggered: {
                peak = Math.max(level, peak - 0.045)
                if (peak <= level) { stop(); if (level >= peak) holdTimer.restart() }
            }
        }

        onClippedChanged: if (clipped) clipTimer.restart()
        property bool clipLatched: clipTimer.running
        Timer { id: clipTimer; interval: 1200 }

        Rectangle {
            id: track
            anchors { left: parent.left; right: clipLed.left; rightMargin: 6
                      verticalCenter: parent.verticalCenter }
            height: parent.height
            radius: 3
            color: Qt.darker(win.panel, 1.4)
            border.width: 1
            border.color: Qt.lighter(win.panel, 1.2)

            // Vert, puis ambre, puis rouge : la zone haute se voit avant
            // d'etre atteinte.
            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom
                          leftMargin: 1; topMargin: 1; bottomMargin: 1 }
                width: Math.max(0, (track.width - 2) * Math.min(level, 1))
                radius: 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0;  color: "#3c9646" }
                    GradientStop { position: 0.55; color: "#3c9646" }
                    GradientStop { position: 0.75; color: "#c8a53c" }
                    GradientStop { position: 1.0;  color: "#c83c32" }
                }
            }

            Rectangle {
                visible: peak > 0.01
                width: 2
                anchors { top: parent.top; bottom: parent.bottom; topMargin: 1; bottomMargin: 1 }
                x: Math.min(track.width - 3, 1 + (track.width - 2) * peak)
                color: win.pal.text
            }
        }

        Rectangle {
            id: clipLed
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: 12
            height: parent.height
            radius: 3
            color: clipLatched ? "#dc2828" : Qt.darker(win.panel, 1.4)
            border.width: 1
            border.color: Qt.lighter(win.panel, 1.2)
        }
    }

    // Point et trait : le pictogramme du morse, dessine comme les autres
    // puisque les polices d'Android n'en ont aucun.
    component MorseGlyph: Item {
        property color glyphColor: win.dim
        implicitWidth: 26
        implicitHeight: 26
        Row {
            anchors.centerIn: parent
            spacing: 5
            Rectangle {
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: parent.parent.glyphColor
            }
            Rectangle {
                width: 17; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: parent.parent.glyphColor
            }
        }
    }

    // Triangle plein, oriente a gauche ou a droite.
    component ArrowGlyph: Canvas {
        property bool pointsRight: true
        property color glyphColor: win.amber
        implicitWidth: 22
        implicitHeight: 22
        onGlyphColorChanged: requestPaint()
        onPointsRightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.fillStyle = glyphColor;
            ctx.beginPath();
            if (pointsRight) {
                ctx.moveTo(width * 0.25, height * 0.12);
                ctx.lineTo(width * 0.80, height * 0.50);
                ctx.lineTo(width * 0.25, height * 0.88);
            } else {
                ctx.moveTo(width * 0.75, height * 0.12);
                ctx.lineTo(width * 0.20, height * 0.50);
                ctx.lineTo(width * 0.75, height * 0.88);
            }
            ctx.closePath();
            ctx.fill();
        }
    }

    // ------------------------------------------------------------- en-tete
    // ------------------------------------------------------ corps principal
    ColumnLayout {
        anchors.fill: parent
        spacing: win.gap

    // Bandeau superieur. Ce n'est volontairement pas le header: de
    // ApplicationWindow : son fond etait pose a une geometrie differente de
    // celle de ses widgets, d'ou le decalage visible sur telephone.
        Rectangle {
            id: headerBar
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: win.panel
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: win.gap
            anchors.rightMargin: win.gap
            spacing: win.gap

            Rectangle {
                Layout.preferredWidth: 76
                Layout.preferredHeight: 34
                radius: 6
                color: (Station.ptt || Station.tuning || Station.cwBusy) ? win.redTx
                                                       : (Station.connected ? win.greenTx : win.panel)
                border.color: Qt.lighter(color, 1.3)
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: Station.tuning ? qsTr("TUNE")
                                         : (Station.cwBusy ? qsTr("CW")
                                                           : (Station.ptt ? "TX" : "RX"))
                    color: Station.ptt ? "#ffffff" : (Station.connected ? win.pal.text : win.dim)
                    font.bold: true
                    font.pixelSize: 16
                }
            }

            Label {
                Layout.fillWidth: true
                text: Station.connected
                          ? (Station.rigName !== "" ? Station.rigName : qsTr("Connected"))
                          : (Station.retrying ? Station.retryText : qsTr("Offline"))
                color: win.dim
                elide: Text.ElideRight
            }

            // Manipulateur : n'apparait que si le poste sait manipuler.
            ToolButton {
                id: cwButton
                visible: Station.hasMorse && Station.hasCat
                enabled: Station.connected
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                onClicked: cwSheet.visible ? cwSheet.close() : cwSheet.open()
                contentItem: MorseGlyph {
                    glyphColor: Station.cwBusy ? win.redTx
                                               : (cwButton.enabled ? win.amber : win.dim)
                }
            }

            // Les polices d'Android ne contiennent ni U+2630 ni les triangles
            // pleins : tout pictogramme est donc dessine, jamais ecrit.
            ToolButton {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                onClicked: drawer.open()
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        spacing: 5
                        Repeater {
                            model: 3
                            delegate: Rectangle {
                                width: 24; height: 2.5; radius: 1.5
                                color: win.dim
                            }
                        }
                    }
                }
            }
        }
        }

        // Corps : marge laterale, pour que seul le bandeau touche les bords.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: win.gap
            Layout.rightMargin: win.gap
            Layout.bottomMargin: win.gap
            spacing: win.gap

        // Frequence. La marge separe nettement la barre du haut de l'afficheur.
        Rectangle {
            id: freqCard
            Layout.fillWidth: true
            Layout.topMargin: win.gap
            Layout.preferredHeight: 110
            radius: 10
            border.color: Qt.lighter(win.panel, 1.15)
            border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.lighter(win.panel, 1.12) }
                GradientStop { position: 1.0; color: Qt.darker(win.panel, 1.10) }
            }

            DropShadow {
                anchors.fill: parent
                radius: parent.radius
            }

            // Un appui ouvre la saisie. Sans CAT il n'y a rien a regler.
            TapHandler {
                enabled: Station.connected && Station.hasCat
                onTapped: {
                    freqField.text = Station.frequencyForEditing()
                    freqDialog.open()
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 2
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: Station.freqText
                    color: Station.hasCat ? win.amber : win.dim
                    font.pixelSize: 42
                    font.bold: true
                    font.family: "monospace"
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: Station.hasCat ? (Station.modeText + "   VFO " + Station.vfoText) : qsTr("PTT only")
                    color: win.dim
                    font.pixelSize: 15
                }
            }
        }

        // S-metre
        RowLayout {
            Layout.fillWidth: true
            spacing: win.gap
            Label { text: qsTr("Signal"); color: win.dim; Layout.preferredWidth: 60 }
            ProgressBar {
                Layout.fillWidth: true
                from: -54; to: 60
                value: Station.sMeterDb
                enabled: Station.hasCat
            }
            Label {
                text: Station.sMeterText
                color: win.amber
                Layout.preferredWidth: 62
                horizontalAlignment: Text.AlignRight
            }
        }

        // ROS, dans le meme gabarit que le S-metre, juste en dessous.
        // N'apparait que si le poste le rapporte.
        RowLayout {
            Layout.fillWidth: true
            spacing: win.gap
            visible: Station.hasSwr && Station.hasCat

            Label { text: qsTr("SWR"); color: win.dim; Layout.preferredWidth: 60 }
            ProgressBar {
                Layout.fillWidth: true
                // L'echelle utile va de 1:1 a 3:1 ; au-dela, la barre est pleine
                // et c'est le chiffre qui renseigne.
                from: 1.0; to: 3.0
                value: Math.max(1.0, Station.swr)
                enabled: Station.swr >= 1.0
            }
            Label {
                text: Station.swrText
                // Au-dela de 2:1 on ne devrait plus emettre longtemps : le
                // chiffre passe au rouge pour que cela saute aux yeux.
                color: Station.swr >= 2.0 ? win.redTx : win.amber
                Layout.preferredWidth: 62
                horizontalAlignment: Text.AlignRight
            }
        }

        // Accord
        RowLayout {
            Layout.fillWidth: true
            spacing: win.gap
            enabled: Station.connected && Station.hasCat

            Button {
                id: downButton
                Layout.preferredWidth: 64
                Layout.preferredHeight: 52
                onClicked: Station.tuneBy(-Station.stepValue(stepBox.currentIndex))
                autoRepeat: true
                contentItem: ArrowGlyph {
                    pointsRight: false
                    glyphColor: downButton.enabled ? win.amber : win.dim
                }
            }
            ComboBox {
                id: stepBox
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                model: Station.stepLabels()
                currentIndex: 2
            }
            Button {
                id: upButton
                Layout.preferredWidth: 64
                Layout.preferredHeight: 52
                onClicked: Station.tuneBy(Station.stepValue(stepBox.currentIndex))
                autoRepeat: true
                contentItem: ArrowGlyph {
                    pointsRight: true
                    glyphColor: upButton.enabled ? win.amber : win.dim
                }
            }
        }

        // Mode et VFO
        RowLayout {
            Layout.fillWidth: true
            spacing: win.gap
            enabled: Station.connected && Station.hasCat

            ComboBox {
                id: modeBox
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                model: Station.modeNames()
                onActivated: Station.setMode(currentText)
                Component.onCompleted: {
                    var i = model.indexOf(Station.modeText)
                    if (i >= 0) currentIndex = i
                }
            }
            Button {
                text: "VFO " + (Station.vfoText === "B" ? "B" : "A")
                font.capitalization: Font.MixedCase
                Layout.preferredWidth: 110
                Layout.preferredHeight: 52
                onClicked: Station.setVfo(Station.vfoText === "B" ? "A" : "B")
            }
        }

        // Bandes
        GridLayout {
            Layout.fillWidth: true
            columns: 4
            rowSpacing: 8
            columnSpacing: 8
            enabled: Station.connected && Station.hasCat

            Repeater {
                model: Station.bands
                delegate: ReliefButton {
                    required property int index
                    required property string modelData
                    text: modelData
                    active: Station.connected && Station.hasCat
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    onClicked: Station.gotoFrequency(Station.bandFrequency(index))
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Niveaux
        RowLayout {
            Layout.fillWidth: true
            spacing: win.gap
            Label { text: qsTr("RX"); color: win.dim; Layout.preferredWidth: 28 }
            LevelMeter {
                Layout.fillWidth: true
                level: Station.rxLevel
                clipped: Station.rxClipped
            }
            Label { text: qsTr("TX"); color: win.dim; Layout.preferredWidth: 28 }
            LevelMeter {
                Layout.fillWidth: true
                level: Station.txLevel
                clipped: Station.txClipped
            }
        }

        Label {
            Layout.fillWidth: true
            text: Station.connected
                  ? qsTr("Round trip %1 ms · buffer %2 ms · %3 lost")
                        .arg(Station.rttMs).arg(Station.jitterMs).arg(Station.lostFrames)
                  : Station.statusText
            color: win.dim
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        // PTT et accord. Le premier prend toute la place restante, le second
        // reste etroit : on ne le cherche pas dans l'urgence.
        RowLayout {
            Layout.fillWidth: true
            spacing: win.gap

        Rectangle {
            Layout.fillWidth: true
            id: pttButton
            Layout.preferredHeight: 130
            radius: 16
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: Station.ptt ? "#ff4d4d"
                                       : Qt.lighter(Station.connected ? win.greenTx : win.panel, 1.3)
                }
                GradientStop {
                    position: 1.0
                    color: Station.ptt ? "#990000"
                                       : Qt.darker(Station.connected ? win.greenTx : win.panel, 1.3)
                }
            }
            border.color: Station.ptt ? "#ff9f9f" : Qt.darker(win.panel, 1.6)
            border.width: 2

            DropShadow {
                anchors.fill: parent
                radius: parent.radius
                offset: 6
                color: Station.ptt ? "#80ff0000" : "#70000000"
            }

            Label {
                anchors.centerIn: parent
                // « PTT » et « TX » se passent de traduction et tiennent dans
                // le bouton quelle que soit la langue.
                text: Station.ptt ? "TX" : (Station.txAllowed ? "PTT" : qsTr("OUT OF BAND"))
                color: Station.connected ? (Station.ptt ? "#ffffff" : win.pal.text) : win.dim
                font.pixelSize: 34
                font.bold: true
            }

            MultiPointTouchArea {
                anchors.fill: parent
                enabled: Station.connected && !Station.tuning && !Station.cwBusy
                         && Station.txAllowed
                // Maintien franc : l'emission suit le doigt, sans bascule.
                onPressed: Station.setPtt(true)
                onReleased: Station.setPtt(false)
                onCanceled: Station.setPtt(false)
            }
        }

        // Meme relief que le reste, et plus large : l'accord se declenche
        // souvent sans quitter le poste des yeux.
        ReliefButton {
            // hasTune vient des capacites declarees par le backend Hamlib, qui
            // restent valables meme si le poste ne repond plus. On exige donc
            // aussi le CAT vivant : sans lui, rien a accorder.
            visible: Station.hasTune && Station.hasCat
            active: Station.connected && !Station.ptt && !Station.tuning
            Layout.preferredWidth: 112
            Layout.preferredHeight: 130
            radius: 16
            fontSize: 17
            text: Station.tuning ? qsTr("Tuning…") : qsTr("Tune")
            baseColor: Station.tuning ? win.redTx : win.panel
            onClicked: Station.startTune()

            DropShadow {
                anchors.fill: parent
                radius: parent.radius
                offset: 6
            }
        }
        }
    }
    }


    // ------------------------------------------------- saisie de la frequence
    Dialog {
        id: freqDialog
        anchors.centerIn: parent
        width: Math.min(win.width - 2 * win.gap, 380)
        modal: true
        title: qsTr("Frequency")
        standardButtons: Dialog.Ok | Dialog.Cancel

        // Fond explicite : celui du style Material passe par un effet d'ombre
        // que tous les moteurs de rendu ne dessinent pas.
        background: Rectangle {
            color: win.panel
            radius: 8
            border.width: 1
            border.color: Qt.lighter(win.panel, 1.4)
        }

        onAccepted: Station.setFrequencyFromText(freqField.text)
        onOpened: { freqField.forceActiveFocus(); freqField.selectAll() }

        // En contentItem, et non en simple enfant : le Dialog reprend alors la
        // hauteur du contenu, sinon son fond ne couvre que le titre.
        contentItem: ColumnLayout {
            spacing: win.gap

            Label {
                Layout.fillWidth: true
                text: qsTr("MHz, kHz or Hz — 14.074, 14074 and 14074000 all work.")
                color: win.dim
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            TextField {
                id: freqField
                Layout.fillWidth: true
                font.pixelSize: 26
                horizontalAlignment: Text.AlignHCenter
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onAccepted: freqDialog.accept()
            }
        }
    }

    // ------------------------------------------------------ manipulateur CW
    Drawer {
        id: cwSheet
        // Par le haut : en bas, le panneau tombait sur le PTT et sur les
        // commandes du telephone, ou l'on appuie par megarde.
        edge: Qt.TopEdge
        width: win.width
        height: Math.min(win.height * 0.72, 520)
        dim: true
        // Ouverture par le bouton seulement : aucun balayage accidentel.
        interactive: false

        background: Rectangle {
            color: win.panel
            border.width: 1
            border.color: Qt.lighter(win.panel, 1.4)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: win.gap
            spacing: win.gap

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Keyer")
                    color: win.amber
                    font.bold: true
                    font.pixelSize: 18
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("%1 WPM").arg(Station.wpm)
                    color: win.pal.text
                    font.pixelSize: 16
                    font.bold: true
                }
            }

            Slider {
                Layout.fillWidth: true
                from: Station.wpmMin
                to: Station.wpmMax
                stepSize: 1
                value: Station.wpm
                onMoved: Station.wpm = Math.round(value)
            }

            // Memoires : un appui envoie, le texte se regle dans les reglages.
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 8
                columnSpacing: 8

                Repeater {
                    model: Station.cwMacros
                    delegate: ReliefButton {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        text: Station.expandMacro(modelData)
                        active: Station.connected && Station.hasMorse
                        onClicked: Station.sendCw(modelData)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: cwField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Text to send")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onAccepted: if (text.length) { Station.sendCw(text); text = "" }
                }

                ReliefButton {
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 48
                    text: qsTr("Send")
                    baseColor: win.amber
                    active: Station.connected && cwField.text.length > 0
                    onClicked: { Station.sendCw(cwField.text); cwField.text = "" }
                }
            }

            Item { Layout.fillHeight: true }

            // Toujours atteignable : une memoire lancee par erreur doit pouvoir
            // etre coupee sans chercher.
            ReliefButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                text: Station.cwBusy ? qsTr("Stop sending") : qsTr("Stop")
                baseColor: Station.cwBusy ? win.redTx : win.panel
                active: Station.connected
                onClicked: Station.stopCw()
            }
        }
    }

    // ---------------------------------------------------------- tiroir
    Drawer {
        id: drawer
        width: Math.min(win.width * 0.92, 420)
        height: win.height
        edge: Qt.RightEdge

        background: Rectangle { color: win.panel }

        Flickable {
            anchors.fill: parent
            anchors.margins: win.gap
            contentHeight: settingsColumn.implicitHeight
            clip: true

            ColumnLayout {
                id: settingsColumn
                width: parent.width
                spacing: win.gap

                Label { text: qsTr("Station"); color: win.amber; font.bold: true; font.pixelSize: 17 }

                Label { text: qsTr("Host"); color: win.dim; font.pixelSize: 12 }
                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Host")
                    text: Station.host
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    onEditingFinished: Station.host = text
                }
                RowLayout {
                    Layout.fillWidth: true
                    SpinBox {
                        Layout.fillWidth: true
                        from: 1; to: 65535
                        value: Station.port
                        editable: true
                        onValueModified: Station.port = value
                        // Un numero de port n'a pas de separateur de milliers :
                        // sans cela le 7300 s'affiche « 7,300 ».
                        textFromValue: function(value, locale) { return value.toString() }
                        valueFromText: function(text, locale) { return parseInt(text, 10) }
                    }
                    CheckBox {
                        text: qsTr("Encrypt")
                        checked: Station.encrypt
                        onToggled: Station.encrypt = checked
                    }
                }

                Switch {
                    Layout.fillWidth: true
                    text: qsTr("Auto-reconnect")
                    checked: Station.autoReconnect
                    onToggled: Station.autoReconnect = checked
                }

                Label { text: qsTr("UDP audio port, 0 to follow the server"); color: win.dim; font.pixelSize: 12 }
                SpinBox {
                    Layout.fillWidth: true
                    from: 0; to: 65535
                    value: Station.udpPort
                    editable: true
                    onValueModified: Station.udpPort = value
                    textFromValue: function(value, locale) { return value === 0 ? qsTr("auto") : value.toString() }
                    valueFromText: function(text, locale) { return parseInt(text, 10) || 0 }
                }
                Label { text: qsTr("Password"); color: win.dim; font.pixelSize: 12 }
                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Password")
                    echoMode: TextInput.Password
                    text: Station.password
                    onEditingFinished: Station.password = text
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    font.capitalization: Font.MixedCase
                    text: Station.connected ? qsTr("Disconnect")
                                            : (Station.retrying ? qsTr("Cancel") : qsTr("Connect"))
                    onClicked: {
                        if (Station.connected || Station.retrying) Station.disconnectFromStation()
                        else Station.connectToStation()
                        drawer.close()
                    }
                }

                MenuSeparator { Layout.fillWidth: true }
                Label { text: qsTr("Audio"); color: win.amber; font.bold: true; font.pixelSize: 17 }

                Label { text: Station.audioSummary(); color: win.dim; wrapMode: Text.Wrap; Layout.fillWidth: true }

                Label { text: qsTr("Microphone"); color: win.dim; font.pixelSize: 12 }
                ComboBox {
                    id: inputBox
                    Layout.fillWidth: true
                    model: Station.inputDevices
                    textRole: "name"
                    valueRole: "id"
                    onActivated: Station.inputDevice = currentValue
                    function sync() {
                        for (var i = 0; i < count; ++i)
                            if (valueAt(i) === Station.inputDevice) { currentIndex = i; return }
                        currentIndex = 0
                    }
                    Component.onCompleted: sync()
                    onModelChanged: sync()
                }

                Label { text: qsTr("Playback"); color: win.dim; font.pixelSize: 12 }
                ComboBox {
                    id: outputBox
                    Layout.fillWidth: true
                    model: Station.outputDevices
                    textRole: "name"
                    valueRole: "id"
                    onActivated: Station.outputDevice = currentValue
                    function sync() {
                        for (var i = 0; i < count; ++i)
                            if (valueAt(i) === Station.outputDevice) { currentIndex = i; return }
                        currentIndex = 0
                    }
                    Component.onCompleted: sync()
                    onModelChanged: sync()
                }

                Button {
                    Layout.fillWidth: true
                    font.capitalization: Font.MixedCase
                    text: qsTr("Rescan audio devices")
                    onClicked: Station.refreshDevices()
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Codec"); color: win.dim; Layout.preferredWidth: 90 }
                    ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("Opus low latency"), qsTr("16-bit PCM")]
                        currentIndex: Station.codec === "pcm" ? 1 : 0
                        onActivated: Station.codec = (currentIndex === 1 ? "pcm" : "opus")
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Jitter"); color: win.dim; Layout.preferredWidth: 90 }
                    Slider {
                        Layout.fillWidth: true
                        from: 20; to: 200; stepSize: 10
                        value: Station.jitterTarget
                        onMoved: Station.jitterTarget = value
                    }
                    Label { text: Station.jitterTarget + " ms"; color: win.dim; Layout.preferredWidth: 56 }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Volume"); color: win.dim; Layout.preferredWidth: 90 }
                    Slider {
                        Layout.fillWidth: true
                        from: 0.2; to: 6.0
                        value: Station.rxGain
                        onMoved: Station.rxGain = value
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Mic level"); color: win.dim; Layout.preferredWidth: 90 }
                    Slider {
                        Layout.fillWidth: true
                        from: 0.2; to: 6.0
                        value: Station.txGain
                        onMoved: Station.txGain = value
                    }
                }

                MenuSeparator { Layout.fillWidth: true }
                Label { text: qsTr("Microphone shaping"); color: win.amber; font.bold: true; font.pixelSize: 17 }

                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("None — required for data modes"),
                            qsTr("Headset boom microphone"),
                            qsTr("Phone microphone")]
                    currentIndex: Station.speechPreset
                    onActivated: Station.speechPreset = currentIndex
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Reduction"); color: win.dim; Layout.preferredWidth: 90 }
                    ProgressBar { Layout.fillWidth: true; from: 0; to: 20; value: Station.gainReductionDb }
                    Label { text: Math.round(Station.gainReductionDb) + " dB"; color: win.dim; Layout.preferredWidth: 56 }
                }

                MenuSeparator { Layout.fillWidth: true }
                Label { text: qsTr("CW"); color: win.amber; font.bold: true; font.pixelSize: 17 }

                Label { text: qsTr("My callsign"); color: win.dim; font.pixelSize: 12 }
                TextField {
                    Layout.fillWidth: true
                    text: Station.myCall
                    placeholderText: qsTr("callsign")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onEditingFinished: Station.myCall = text
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Memories — %c stands for your callsign.")
                    color: win.dim
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: Station.cwMacros
                    delegate: TextField {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        onEditingFinished: Station.setCwMacro(index, text)
                    }
                }

                MenuSeparator { Layout.fillWidth: true }
                Label { text: qsTr("Controls"); color: win.amber; font.bold: true; font.pixelSize: 17 }

                Switch {
                    Layout.fillWidth: true
                    text: qsTr("PTT on volume-down key")
                    checked: Station.pttOnVolumeKey
                    onToggled: Station.pttOnVolumeKey = checked
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("The key no longer changes the volume while this is on.")
                    color: win.dim
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                Switch {
                    Layout.fillWidth: true
                    text: qsTr("Publish a rigctld interface")
                    checked: Station.rigctldEnabled
                    onToggled: Station.rigctldEnabled = checked
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Data-mode software on this device can then drive the remote radio: Hamlib NET rigctl, 127.0.0.1:%1.").arg(Station.rigctldPort)
                    color: win.dim
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                MenuSeparator { Layout.fillWidth: true }
                Label { text: qsTr("Appearance"); color: win.amber; font.bold: true; font.pixelSize: 17 }

                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Dark"), qsTr("Red"), qsTr("Contrast"), qsTr("Light")]
                    currentIndex: Station.theme
                    onActivated: Station.theme = currentIndex
                }

                MenuSeparator { Layout.fillWidth: true }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Build of %1").arg(Station.buildStamp)
                    color: win.dim
                    font.pixelSize: 12
                }

                MenuSeparator { Layout.fillWidth: true }
                Label { text: qsTr("Log"); color: win.amber; font.bold: true; font.pixelSize: 17 }
                Label {
                    Layout.fillWidth: true
                    text: Station.logText
                    color: win.dim
                    font.pixelSize: 11
                    font.family: "monospace"
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
