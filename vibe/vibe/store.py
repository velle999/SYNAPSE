"""The companion store: one SQLite file for everything the assistant remembers
about the person rather than about the code.

⛔ ONE DATABASE, AND IT IS NOT THE PROJECT MEMORY. `.vibe/memory.md` is per
directory and belongs to the coding side — it summarises a session about a
checkout. Todos, habits, goals and pomodoros belong to the PERSON and follow
them between projects, so they live under XDG data with everything else the
desktop keeps.

⚠ STDLIB sqlite3, NOT better-sqlite3. This is a port of velle.ai's Node
modules and the schemas are kept identical on purpose — an existing
companion.db can be pointed at directly — but nothing here may add a
dependency the ISO would have to carry.

⚠ Connections are per call and short-lived. A long-lived handle in `vibe
serve` would hold a write lock across a model turn that can take a minute, and
the CLI, the window and the bar's pomodoro indicator all read this file at
once.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

from __future__ import annotations

import os
import sqlite3
from contextlib import contextmanager
from pathlib import Path


def data_dir() -> Path:
    root = os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local" / "share")
    return Path(root) / "vibe"


def db_path() -> Path:
    """The companion database. VIBE_DB overrides it, which is what the tests
    use — every one of them runs against a scratch file, never the desktop's."""
    env = os.environ.get("VIBE_DB")
    if env:
        return Path(env)
    return data_dir() / "companion.db"


SCHEMA = """
CREATE TABLE IF NOT EXISTS todos (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    content TEXT NOT NULL,
    project TEXT DEFAULT 'inbox',
    priority INTEGER DEFAULT 2 CHECK(priority BETWEEN 1 AND 4),
    status TEXT DEFAULT 'todo' CHECK(status IN ('todo','doing','done','cancelled')),
    due_date TEXT,
    tags TEXT,
    created_at DATETIME DEFAULT (datetime('now','localtime')),
    completed_at DATETIME
);
CREATE INDEX IF NOT EXISTS idx_todo_status   ON todos(status);
CREATE INDEX IF NOT EXISTS idx_todo_project  ON todos(project);
CREATE INDEX IF NOT EXISTS idx_todo_priority ON todos(priority);

CREATE TABLE IF NOT EXISTS habits (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    icon TEXT DEFAULT '*',
    frequency TEXT DEFAULT 'daily',
    target INTEGER DEFAULT 1,
    created_at DATETIME DEFAULT (datetime('now','localtime'))
);
CREATE TABLE IF NOT EXISTS habit_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    habit_id INTEGER NOT NULL,
    value INTEGER DEFAULT 1,
    date TEXT DEFAULT (date('now','localtime')),
    FOREIGN KEY (habit_id) REFERENCES habits(id),
    UNIQUE(habit_id, date)
);
CREATE INDEX IF NOT EXISTS idx_hlog_date  ON habit_logs(date);
CREATE INDEX IF NOT EXISTS idx_hlog_habit ON habit_logs(habit_id);

CREATE TABLE IF NOT EXISTS goals (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    description TEXT,
    target_date TEXT,
    progress INTEGER DEFAULT 0 CHECK(progress BETWEEN 0 AND 100),
    status TEXT DEFAULT 'active' CHECK(status IN ('active','completed','abandoned')),
    category TEXT DEFAULT 'general',
    created_at DATETIME DEFAULT (datetime('now','localtime'))
);
CREATE TABLE IF NOT EXISTS milestones (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    goal_id INTEGER NOT NULL,
    title TEXT NOT NULL,
    completed INTEGER DEFAULT 0,
    completed_at DATETIME,
    FOREIGN KEY (goal_id) REFERENCES goals(id)
);

/* ⛔ THE RUNNING TIMER IS A ROW, NOT A VARIABLE. velle.ai held the active
 * pomodoro in a Map keyed by websocket, so it died with the server and only
 * the tab that started it could see it. Here the window, the CLI and the BAR
 * are three processes, and `vibe serve` exists only while the chat window is
 * open — a timer that could not outlive it would stop every time the window
 * was closed. Running == a row with no ended_at. */
CREATE TABLE IF NOT EXISTS pomodoro_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    task TEXT,
    duration INTEGER DEFAULT 25,
    type TEXT DEFAULT 'focus' CHECK(type IN ('focus','short_break','long_break')),
    completed INTEGER DEFAULT 0,
    started_at DATETIME DEFAULT (datetime('now','localtime')),
    ended_at DATETIME
);
CREATE INDEX IF NOT EXISTS idx_pom_started ON pomodoro_sessions(started_at);
"""


@contextmanager
def connect():
    """A connection with the schema in place, committed on the way out.

    ⚠ `row_factory` is sqlite3.Row so every reader gets mapping access — the
    ported code came from better-sqlite3, which hands back objects, and a
    tuple-indexed port of it would be unreviewable."""
    path = db_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(path, timeout=5.0)
    conn.row_factory = sqlite3.Row
    try:
        conn.executescript(SCHEMA)
        yield conn
        conn.commit()
    finally:
        conn.close()


def rows(cur) -> list[dict]:
    return [dict(r) for r in cur.fetchall()]


def one(cur) -> dict | None:
    r = cur.fetchone()
    return dict(r) if r else None
