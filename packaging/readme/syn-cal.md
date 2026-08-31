# syn-cal

A calendar and schedule planner that syncs both ways with CalDAV, Google
Calendar and Microsoft 365. It keeps its own store, so the month is drawn and
events are made whether or not the network is there, and a sync reconciles
when it comes back.

Three faces over one engine: a window, a terminal UI, and a command line that
answers in records another program can read.

## Setting up an account

```bash
syn-cal account add work https://caldav.example.com/dav/
syn-cal account add-google personal
syn-cal login work                 # password prompt, or the browser
syn-cal discover work              # ask the server which calendars exist
syn-cal calendars work             # list them, and which are switched on
syn-cal enable work Personal
syn-cal sync
```

Signing in, discovering and enabling are three separate steps on purpose:
each can succeed while the next has never run, and `syn-cal calendars` is
where you see which is which.

Google accounts need the **CalDAV API** enabled on the project, which is a
different switch from the Calendar API — a sign-in succeeds either way and
every request afterwards fails without it. syn-cal prints what the server
said rather than a generic failure.

## Looking at it

```bash
syn-cal agenda --days 7      # what is on, across every calendar
syn-cal month                # the month as a grid
syn-cal tui                  # the month, interactive, in this terminal
syn-cal gui                  # the window
syn-cal gui 2026-09-14       # …opening a new event on that day
syn-cal weekstart mon        # which day a week is drawn from
```

## Making events

`new`, `edit` and `delete` take an event from the command line; the window
has a form for the same thing, and double-clicking a day in the desktop's
calendar opens it on that date. All-day events are stored as dates rather
than as midnight in some timezone, which is what keeps them off the day
before when you travel.

Passwords go to the system keyring through libsecret — gnome-keyring or
kwallet — and never into the config file.
