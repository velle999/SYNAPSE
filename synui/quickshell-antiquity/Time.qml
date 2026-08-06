pragma Singleton

import Quickshell
import QtQuick

/*
 * The shell's one clock.
 *
 * One SystemClock, not one per consumer: it ticks every second and every extra
 * instance is another wakeup a second for the same answer. `now` is exported so
 * that anything wanting a different format (the control panel wants the time
 * and the long date on separate lines) formats it itself rather than growing
 * another preformatted string here.
 */
Singleton {
    id: root

    readonly property date now: clock.date

    readonly property string time: {
        Qt.formatDateTime(clock.date, " MMM d yyyy | hh:mm");
    }

    SystemClock {
        id: clock
        precision: SystemClock.Seconds
    }
}
