pragma Singleton
import QtQuick

// Home / End / PageUp / PageDown for anything that scrolls.
//
// Qt Quick gives a Flickable no keyboard paging of its own, so every pane
// that scrolls has to answer these itself. Rather than four switch cases
// copied into each one, they all call handle() with the flickable they own:
//
//     focus: true
//     Keys.onPressed: (e) => ScrollKeys.handle(e, myFlickable)
//
// Returns true when the key was consumed, and sets event.accepted so the
// key does not travel on to a parent that would scroll something else.
QtObject {
    // How much of the old view stays on screen after a page, so the reader
    // keeps their place instead of losing a line at the seam.
    readonly property int pageOverlap: 48

    // The top is originY, not zero: a ListView that has scrolled shifts its
    // origin as items come and go, and clamping to zero lands short of the
    // real top -- returnToBounds() then snaps back and Home appears to do
    // nothing.
    function topY(flick) {
        return flick.originY
    }

    function maxY(flick) {
        return flick.originY + Math.max(0, flick.contentHeight - flick.height)
    }

    function step(flick) {
        return Math.max(60, flick.height - pageOverlap)
    }

    function scrollTo(flick, y) {
        // Stop any flick already under way, or it fights the jump and the
        // view drifts past where it was put.
        if (flick.moving)
            flick.cancelFlick()
        flick.contentY = Math.max(topY(flick), Math.min(maxY(flick), y))
        flick.returnToBounds()
    }

    function handle(event, flick) {
        if (!flick)
            return false
        switch (event.key) {
        case Qt.Key_PageDown:
            scrollTo(flick, flick.contentY + step(flick))
            break
        case Qt.Key_PageUp:
            scrollTo(flick, flick.contentY - step(flick))
            break
        case Qt.Key_Home:
            scrollTo(flick, topY(flick))
            break
        case Qt.Key_End:
            scrollTo(flick, maxY(flick))
            break
        case Qt.Key_Space:
            // The reading habit: space pages down, shift+space pages back.
            scrollTo(flick, flick.contentY
                     + ((event.modifiers & Qt.ShiftModifier) ? -step(flick)
                                                             : step(flick)))
            break
        default:
            return false
        }
        event.accepted = true
        return true
    }
}
