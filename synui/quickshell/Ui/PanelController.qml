import QtQuick

/*
 * PanelController — one boolean, and the reason it is its own type.
 *
 * A Panel's open state has to live somewhere a widget's own properties cannot
 * collide with. Omarchy separates it so a panel implementation can declare
 * whatever it likes without shadowing the lifecycle, and the type is what the
 * `controller` alias on Panel points at.
 */
QtObject {
    id: root

    property bool open: false

    function toggle() { root.open = !root.open }
    function show()   { if (!root.open) root.open = true }
    function hide()   { root.open = false }
}
