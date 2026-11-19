import QtQuick
import QtQuick.Controls
import AiReader

// Drag handle that lives in the top-left corner of a SplitView pane.
// Press-and-drag horizontally to move the pane to a new position in
// the parent SplitView; a vertical insertion marker shows where it
// will land. The MouseArea grabs the mouse on press, so movement
// outside the 16×16 grip continues to fire onPositionChanged until
// the button is released.
Rectangle {
    id: grip

    // The SplitView child this grip moves. Usually the grip's own
    // SplitView-attached parent — we don't infer it because some
    // panes wrap their root in extra layout items.
    required property Item pane
    // The SplitView that contains `pane`.
    required property SplitView split
    // Vertical insertion marker (a Rectangle living above the
    // SplitView in window coordinates). Made visible during drag.
    required property Item marker

    // Fired after a successful reorder so the parent can persist the
    // new pane order.
    signal reordered()

    width: 18
    height: 18
    radius: 3
    // Theme-aware: the old fixed light-gray hover chip and #444 dots
    // vanished (or glared) on dark panes. Pressed uses the filled
    // primary pair so the dots stay legible on the blue fill.
    color: ma.pressed
           ? Theme.primaryBg
           : (ma.containsMouse ? Theme.buttonHover : "transparent")
    border.color: ma.containsMouse && !ma.pressed ? Theme.border : "transparent"
    border.width: 1
    z: 100

    Text {
        anchors.centerIn: parent
        text: "⋮⋮"   // two vertical ellipses → grip-dots
        color: ma.pressed ? Theme.onPrimary
             : ma.containsMouse ? Theme.text : Theme.dimText
        font.pixelSize: 11
        font.bold: true
    }

    ToolTip.visible: ma.containsMouse && !ma.pressed
    ToolTip.delay: 600
    ToolTip.text: qsTr("Drag to reorder this panel")

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        // The Windows "move window" cursor (four-headed arrow). While
        // dragging, the pointer leaves this 18×18 grip and crosses other
        // panes, whose own cursors would take over — so the drag runs
        // under an application-wide override instead.
        cursorShape: Qt.SizeAllCursor

        // Slot that the pane will land in if released right now.
        // -1 means "no valid drop target" (e.g., dropping where it
        // already is). Used by onReleased; updated by updateMarker().
        property int targetSlot: -1

        onPressed: function(mouse) {
            targetSlot = -1
            cursorUtil.pushOverrideCursor(Qt.SizeAllCursor)
            updateMarker(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            updateMarker(mouse.x, mouse.y)
        }
        onCanceled: {
            // A stolen grab (window deactivated, a popup opening) never
            // reaches onReleased — without this the marker would stay on
            // screen and the override cursor would never be restored.
            grip.marker.visible = false
            targetSlot = -1
            cursorUtil.popOverrideCursor()
        }

        onReleased: function(mouse) {
            grip.marker.visible = false
            cursorUtil.popOverrideCursor()
            const slot = targetSlot
            targetSlot = -1
            if (slot < 0) return

            let curIdx = -1
            for (let i = 0; i < grip.split.count; ++i) {
                if (grip.split.itemAt(i) === grip.pane) {
                    curIdx = i
                    break
                }
            }
            if (curIdx < 0) return

            // Slot is the boundary index in [0..count]. Dropping at
            // slot s means the pane should occupy index s after the
            // move. takeItem shifts later indices down by one, so if
            // we're moving rightward we need to subtract one.
            let dst = slot
            if (dst > curIdx) dst -= 1
            if (dst === curIdx) return
            const item = grip.split.takeItem(curIdx)
            grip.split.insertItem(dst, item)
            grip.reordered()
        }

        // Recompute the nearest boundary in the SplitView for the
        // current cursor position; place the marker there and
        // remember the target slot.
        function updateMarker(mx, my) {
            const sp = grip.mapToItem(grip.split, mx, my)
            const sx = Math.max(0, Math.min(grip.split.width, sp.x))

            // Boundaries: leftEdge of item 0 (slot 0), then rightEdge
            // of each item (slots 1..count). Hidden items (width 0)
            // collapse to a single boundary with their neighbors —
            // that's fine, the user just gets one less drop slot.
            let bestSlot = 0
            const first = grip.split.itemAt(0)
            const firstX = first ? first.x : 0
            let bestDist = Math.abs(sx - firstX)
            let bestX = firstX

            for (let i = 0; i < grip.split.count; ++i) {
                const it = grip.split.itemAt(i)
                if (!it) continue
                const rx = it.x + it.width
                const d = Math.abs(sx - rx)
                if (d < bestDist) {
                    bestDist = d
                    bestSlot = i + 1
                    bestX = rx
                }
            }
            targetSlot = bestSlot

            const m = grip.marker
            const wp = grip.split.mapToItem(m.parent, bestX, 0)
            m.x = Math.round(wp.x) - Math.floor(m.width / 2)
            m.y = Math.round(wp.y)
            m.height = grip.split.height
            m.visible = true
        }
    }
}
