import QtQuick
import qs.Commons

/*
 * CursorSurface — the highlight under a panel row that both the keyboard and
 * the mouse can be pointing at.
 *
 * ⛔ THE CONTRACT IS THAT A ROW MUST NOT COLOUR ITSELF FROM `containsMouse`,
 * and it is worth stating because it is the whole reason this type exists. A
 * panel has one cursor. If each row painted its own hover, moving the mouse
 * over a list while arrowing through it would light two rows at once and
 * neither would be the one Enter acts on. So hover updates the PANEL's cursor
 * index at the root, and every row derives its look from `hasCursor` — one
 * highlight on screen, whichever device moved it.
 *
 * `current` is a different thing from `hasCursor`: the row that is SELECTED —
 * the network you are on, the sink that is playing — which stays lit while the
 * cursor moves over other rows.
 */
BorderSurface {
    id: root

    property bool hasCursor: false
    property bool current: false
    /* Kept because callers still set it. It asked for a border-only row; rows
     * take the same cursor fill as everything else now, and a caller that sets
     * it gets the consistent look rather than an error. */
    property bool outline: false
    property bool bordered: false

    property color foreground: Color.foreground
    property color accent: Color.accent
    property color fill:        Style.hoverFillFor(root.foreground, root.accent, null)
    property color currentFill: Style.selectedFillFor(root.foreground, root.accent, null)

    radius: Style.cornerRadius

    color: root.hasCursor ? root.fill : (root.current ? root.currentFill : "transparent")

    borderSpec: root.hasCursor
        ? Border.controlSpec("hover-cursor", root.foreground, root.accent, null)
        : (root.current
           ? Border.controlSpec("selected", root.foreground, root.accent, null)
           : (root.bordered
              ? Border.controlSpec("normal", root.foreground, root.accent, null)
              : Border.none()))

    /* Short, because a cursor that eases is a cursor that lags behind the arrow
     * key that moved it. */
    Behavior on color { ColorAnimation { duration: 60 } }
}
