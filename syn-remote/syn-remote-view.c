/*
 * syn-remote-view — the window a saved connection opens in.
 *
 * ⛔ THIS EXISTS BECAUSE NO PACKAGED VNC CLIENT CAN BE HANDED A PASSWORD.
 *
 * That is the whole justification for a viewer in a distro that would much
 * rather ship somebody else's, so it is worth writing down precisely. wayvnc
 * authenticates over VeNCrypt: a username and a password carried INSIDE the
 * TLS session (RFB security type X509Plain). Against that,
 *
 *   - TigerVNC's `vncviewer -passwd FILE` is the obfuscated file used by
 *     classic VncAuth, security type 2. It is not consulted for VeNCrypt
 *     Plain, and vncviewer(1) documents no way to pass a username at all.
 *   - gtk-vnc's own gvncviewer example builds a GtkDialog inside its
 *     credential callback and asks a human.
 *
 * So a connection manager wrapping either would store a password it could
 * never use and prompt anyway. gtk-vnc's credential API is the seam: the
 * server asks for a credential through `vnc-auth-credential`, and whatever
 * answers that signal decides whether a person types. Here it is answered from
 * what syn-remote already remembers, and the dialog is only what happens when
 * there is nothing remembered to answer with.
 *
 * ⚠ THE STORE IS NOT IN THIS FILE. Where a password lives — a keyring if one
 * is running, a 0600 file if not, and the read-back that tells those apart —
 * is decided in syn-remote itself, in one place, because two implementations
 * of "where did we put it" is how a credential ends up saved in one and looked
 * for in the other. This process is handed the password on stdin and shells
 * back out to `syn-remote saved <name> set` when somebody ticks Remember.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <gtk/gtk.h>
#include <vncdisplay.h>
#include <string.h>
#include <unistd.h>

/*
 * ⚠ THE PASSWORD ARRIVES ON stdin AND NOWHERE ELSE. An argument is world
 * readable in `ps` for as long as this runs; an environment variable is
 * readable through /proc/PID/environ for the same window. A pipe is read once,
 * at startup, and the fd is closed behind it.
 */
#define PW_MAX 1024

typedef struct {
    GtkWidget  *window;
    GtkWidget  *vnc;
    GtkWidget  *status;      /* shown until the first frame arrives */
    GtkWidget  *stack;

    char       *name;        /* the saved connection's name, for the title */
    char       *host;
    char       *port;
    char       *user;
    char       *password;    /* from stdin; may be NULL */
    char       *cacert;      /* PEM of the pinned server certificate */

    gboolean    connected;
    /*
     * ⚠ SET THE FIRST TIME THE SERVER REJECTS WHAT WE SENT. Without it a
     * wrong saved password is an infinite loop: gtk-vnc asks, we answer from
     * the store, the server refuses, gtk-vnc asks again. After a failure the
     * stored answer is not offered a second time and the dialog opens instead.
     */
    gboolean    creds_refused;
} View;

/* ── The status card ──────────────────────────────────────────────────── */

static void status_set(View *v, const char *markup)
{
    gtk_label_set_markup(GTK_LABEL(v->status), markup);
    gtk_stack_set_visible_child_name(GTK_STACK(v->stack), "status");
}

static void show_display(View *v)
{
    gtk_stack_set_visible_child_name(GTK_STACK(v->stack), "vnc");
}

/* ── Remembering, by asking the one thing that knows how ──────────────── */

/*
 * ⛔ SPAWNED, NOT REIMPLEMENTED. `syn-remote saved <name> set` is the only code
 * that decides between a keyring and a file and verifies the write by reading
 * it back — secret-tool exits 0 with no keyring running, so an unverified
 * write is a password that silently went nowhere. Duplicating that decision
 * here would give this desktop two answers to "where is my password".
 */
static void remember_password(View *v, const char *pw)
{
    gint     in_fd = -1;
    GPid     pid;
    GError  *err = NULL;
    char    *argv[] = { (char *)"syn-remote", (char *)"saved", v->name,
                        (char *)"set", NULL };

    if (!v->name || !*v->name)
        return;

    if (!g_spawn_async_with_pipes(NULL, argv, NULL,
                                  G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                  NULL, NULL, &pid, &in_fd, NULL, NULL, &err)) {
        g_warning("could not run syn-remote to save the password: %s",
                  err ? err->message : "(no reason given)");
        g_clear_error(&err);
        return;
    }

    /* `read -rs` wants a line, so the newline is part of the message. */
    if (in_fd >= 0) {
        char *line = g_strdup_printf("%s\n", pw);
        ssize_t left = (ssize_t)strlen(line), off = 0;
        while (left > 0) {
            ssize_t n = write(in_fd, line + off, (size_t)left);
            if (n <= 0)
                break;
            off += n; left -= n;
        }
        /* Wiped before it is freed: this buffer held the password. */
        memset(line, 0, strlen(line));
        g_free(line);
        close(in_fd);
    }
    g_spawn_close_pid(pid);
}

/* ── The credential dialog: only when there is nothing stored ─────────── */

typedef struct {
    gboolean want_user;
    gboolean want_pass;
    char    *user;
    char    *pass;
    gboolean remember;
    gboolean ok;
} Ask;

static gboolean ask_credentials(View *v, Ask *a)
{
    GtkWidget *dlg, *box, *grid, *e_user = NULL, *e_pass = NULL, *check = NULL;
    char      *heading;
    int        row = 0;

    dlg = gtk_dialog_new_with_buttons("Sign in",
                                      GTK_WINDOW(v->window),
                                      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                      "_Cancel",  GTK_RESPONSE_CANCEL,
                                      "_Connect", GTK_RESPONSE_OK,
                                      NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);

    heading = v->creds_refused
        ? g_strdup_printf("<b>%s did not accept that password.</b>",
                          v->name && *v->name ? v->name : v->host)
        : g_strdup_printf("<b>%s</b> is asking who you are.",
                          v->name && *v->name ? v->name : v->host);
    {
        GtkWidget *lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), heading);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    }
    g_free(heading);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

    if (a->want_user) {
        GtkWidget *l = gtk_label_new("User");
        gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
        e_user = gtk_entry_new();
        if (v->user)
            gtk_entry_set_text(GTK_ENTRY(e_user), v->user);
        gtk_entry_set_activates_default(GTK_ENTRY(e_user), TRUE);
        gtk_grid_attach(GTK_GRID(grid), l,      0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), e_user, 1, row, 1, 1);
        row++;
    }
    if (a->want_pass) {
        GtkWidget *l = gtk_label_new("Password");
        gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
        e_pass = gtk_entry_new();
        gtk_entry_set_visibility(GTK_ENTRY(e_pass), FALSE);
        gtk_entry_set_input_purpose(GTK_ENTRY(e_pass), GTK_INPUT_PURPOSE_PASSWORD);
        gtk_entry_set_activates_default(GTK_ENTRY(e_pass), TRUE);
        gtk_grid_attach(GTK_GRID(grid), l,      0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), e_pass, 1, row, 1, 1);
        row++;

        /* Only offered for a connection that HAS a name to save it under —
         * a one-off `--host` with no entry in the list has nowhere to put it. */
        if (v->name && *v->name) {
            check = gtk_check_button_new_with_label("Remember this password");
            gtk_grid_attach(GTK_GRID(grid), check, 1, row, 1, 1);
        }
    }

    gtk_widget_show_all(dlg);
    a->ok = (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK);

    if (a->ok) {
        if (e_user)
            a->user = g_strdup(gtk_entry_get_text(GTK_ENTRY(e_user)));
        if (e_pass)
            a->pass = g_strdup(gtk_entry_get_text(GTK_ENTRY(e_pass)));
        if (check)
            a->remember = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check));
    }
    gtk_widget_destroy(dlg);
    return a->ok;
}

/* ── The signal this whole program is built around ────────────────────── */

/*
 * ⚠ GValueArray IS DEPRECATED AND IS STILL THE SIGNAL'S ARGUMENT. gtk-vnc
 * marshals `vnc-auth-credential` as G_TYPE_VALUE_ARRAY and has not changed it;
 * the deprecation is GLib's, not gtk-vnc's, and the accessor is what the
 * library's own example uses. Compiled with -Wno-deprecated-declarations for
 * exactly this, rather than reaching into the struct by hand.
 */
static void on_credential(GtkWidget *widget, GValueArray *creds, gpointer data)
{
    View     *v = data;
    Ask       ask = { 0 };
    guint     i;
    gboolean  need_dialog = FALSE;

    /* First pass: what is being asked for, and can we answer it already? */
    for (i = 0; i < creds->n_values; i++) {
        GValue *cred = g_value_array_get_nth(creds, i);
        switch (g_value_get_enum(cred)) {
        case VNC_DISPLAY_CREDENTIAL_USERNAME:
            if (!v->user || !*v->user || v->creds_refused) {
                ask.want_user = TRUE;
                need_dialog = TRUE;
            }
            break;
        case VNC_DISPLAY_CREDENTIAL_PASSWORD:
            if (!v->password || !*v->password || v->creds_refused) {
                ask.want_pass = TRUE;
                need_dialog = TRUE;
            }
            break;
        default:
            break;
        }
    }

    if (need_dialog) {
        if (!ask_credentials(v, &ask)) {
            /* Cancelled. Close the connection rather than leaving a window
             * that will sit for ever on "Connecting". */
            vnc_display_close(VNC_DISPLAY(v->vnc));
            gtk_widget_destroy(v->window);
            return;
        }
        if (ask.user) { g_free(v->user);     v->user     = ask.user; }
        if (ask.pass) {
            if (v->password) {
                memset(v->password, 0, strlen(v->password));
                g_free(v->password);
            }
            v->password = ask.pass;
        }
        if (ask.remember && v->password)
            remember_password(v, v->password);
        /* Answered once; a later ask means the new one was refused too. */
        v->creds_refused = FALSE;
    }

    /* Second pass: hand back whatever was asked for. */
    for (i = 0; i < creds->n_values; i++) {
        GValue     *cred = g_value_array_get_nth(creds, i);
        const char *val  = NULL;

        switch (g_value_get_enum(cred)) {
        case VNC_DISPLAY_CREDENTIAL_USERNAME:   val = v->user ? v->user : "";     break;
        case VNC_DISPLAY_CREDENTIAL_PASSWORD:   val = v->password ? v->password : ""; break;
        /*
         * ⚠ CLIENTNAME IS NOT OPTIONAL WHEN IT IS ASKED FOR. A credential left
         * unset stalls the handshake — the server is waiting for an answer
         * that never comes, and the window sits on "Connecting" with nothing
         * wrong that anybody can see.
         */
        case VNC_DISPLAY_CREDENTIAL_CLIENTNAME: val = "syn-remote";               break;
        /*
         * ⛔ THE SERVER'S CERTIFICATE, AND IT IS NOT OPTIONAL HERE. wayvnc
         * offers exactly one security type this viewer can speak — VeNCrypt
         * X509Plain — so gtk-vnc always asks for a CA to validate the peer
         * against, and gnutls then checks the address we dialled against the
         * certificate's names. A self-signed certificate is its own CA, which
         * is why the pinned PEM goes straight in here.
         *
         * Left unset, the connection is simply dropped after the TLS session
         * comes up: no error, no auth failure, and on the server "Client
         * handshake timed out". Measured, both ways round.
         */
        case VNC_DISPLAY_CREDENTIAL_CA_CERT_DATA:
            if (!v->cacert) continue;
            val = v->cacert;
            break;
        default:                                continue;
        }

        /*
         * ⛔ NON-ZERO MEANS FAILURE. vnc_display_set_credential is declared
         * `gboolean` and reads exactly like every other GLib predicate, and it
         * is the opposite: gtk-vnc's own gvncviewer writes
         * `if (vnc_display_set_credential(...)) { "Failed to set credential" }`.
         *
         * This was `if (!...)`, which turned every SUCCESS into an abort — and
         * because CLIENTNAME is the first credential the server asks for, the
         * viewer bailed out before it ever reached the certificate, leaving
         * the handshake half-finished. The server's only symptom was "Client
         * handshake timed out", which reads like a network problem and is not.
         */
        if (vnc_display_set_credential(VNC_DISPLAY(widget),
                                       g_value_get_enum(cred), val)) {
            status_set(v, "<b>Could not send the credentials.</b>");
            vnc_display_close(VNC_DISPLAY(v->vnc));
            return;
        }
    }
}

/* ── The rest of the lifecycle ────────────────────────────────────────── */

static void on_connected(GtkWidget *w G_GNUC_UNUSED, gpointer data)
{
    View *v = data;
    status_set(v, "<b>Connected.</b>\nWaiting for the first frame\xe2\x80\xa6");
}

static void on_initialized(GtkWidget *w G_GNUC_UNUSED, gpointer data)
{
    View *v = data;
    v->connected = TRUE;
    show_display(v);
    gtk_widget_grab_focus(v->vnc);
}

static void on_disconnected(GtkWidget *w G_GNUC_UNUSED, gpointer data)
{
    View *v = data;
    /*
     * ⚠ TWO DIFFERENT EVENTS WEAR THIS ONE SIGNAL. A disconnect AFTER the
     * desktop came up is somebody closing the session, and the window should
     * simply go. A disconnect BEFORE it is a connection that never happened,
     * and closing silently would leave a person clicking Connect and watching
     * nothing occur — so that one stays on screen and says so.
     */
    if (v->connected) {
        gtk_widget_destroy(v->window);
        return;
    }
    {
        char *m = g_markup_printf_escaped(
            "<b>Could not reach %s.</b>\n\n"
            "Nothing answered at %s:%s, or it refused the connection.\n"
            "A SynapseOS desktop has to be switched on first: syn-remote on",
            v->name && *v->name ? v->name : v->host, v->host, v->port);
        status_set(v, m);
        g_free(m);
    }
}

static void on_auth_failure(GtkWidget *w G_GNUC_UNUSED,
                            const char *reason, gpointer data)
{
    View *v = data;
    /* The next credential request must not be answered from the store. */
    v->creds_refused = TRUE;
    g_message("authentication refused%s%s",
              reason ? ": " : "", reason ? reason : "");
}

static void on_auth_unsupported(GtkWidget *w G_GNUC_UNUSED,
                                unsigned int type, gpointer data)
{
    View *v = data;
    char *m = g_strdup_printf(
        "<b>That server wants an authentication this viewer does not have.</b>\n\n"
        "RFB security type %u.", type);
    status_set(v, m);
    g_free(m);
}

static gboolean on_key(GtkWidget *w G_GNUC_UNUSED, GdkEventKey *ev, gpointer data)
{
    View *v = data;
    if (ev->keyval == GDK_KEY_F11) {
        GdkWindow *gw = gtk_widget_get_window(v->window);
        gboolean full = gw && (gdk_window_get_state(gw) & GDK_WINDOW_STATE_FULLSCREEN);
        if (full)
            gtk_window_unfullscreen(GTK_WINDOW(v->window));
        else
            gtk_window_fullscreen(GTK_WINDOW(v->window));
        return TRUE;
    }
    return FALSE;
}

/* ── stdin ────────────────────────────────────────────────────────────── */

static char *read_password_from_stdin(void)
{
    char    buf[PW_MAX];
    size_t  used = 0;

    /*
     * ⚠ AN EMPTY PIPE IS "NOTHING SAVED", NOT "THE PASSWORD IS EMPTY". The
     * caller always pipes; when there is no stored password it pipes nothing,
     * and this returns NULL so the credential handler opens its dialog.
     */
    while (used < sizeof(buf) - 1) {
        ssize_t n = read(STDIN_FILENO, buf + used, sizeof(buf) - 1 - used);
        if (n <= 0)
            break;
        used += (size_t)n;
    }
    buf[used] = '\0';
    while (used > 0 && (buf[used - 1] == '\n' || buf[used - 1] == '\r'))
        buf[--used] = '\0';

    if (used == 0)
        return NULL;
    return g_strdup(buf);
}

int main(int argc, char **argv)
{
    View        v = { 0 };
    char       *host = NULL, *port = NULL, *name = NULL, *user = NULL;
    char       *cacert = NULL;
    GOptionEntry entries[] = {
        { "host", 0, 0, G_OPTION_ARG_STRING, &host, "the machine to reach", "HOST" },
        { "port", 0, 0, G_OPTION_ARG_STRING, &port, "its port (default 5900)", "PORT" },
        { "name", 0, 0, G_OPTION_ARG_STRING, &name, "the saved connection's name", "NAME" },
        { "user", 0, 0, G_OPTION_ARG_STRING, &user, "the user to sign in as", "USER" },
        { "cacert", 0, 0, G_OPTION_ARG_FILENAME, &cacert,
          "PEM of the server certificate this connection has pinned", "FILE" },
        { NULL, 0, 0, 0, NULL, NULL, NULL }
    };
    GOptionContext *ctx;
    GError         *err = NULL;
    GtkWidget      *sw;

    /*
     * ⛔ BEFORE gtk_init, AND IT IS THE WINDOW'S app_id ON WAYLAND. GTK3 takes
     * the xdg-shell app_id from g_get_prgname(), and an app_id that does not
     * match an installed .desktop basename gets no icon and no dock entry —
     * the same rule the quickshell windows follow through QS_APP_ID.
     */
    g_set_prgname("syn-remote");

    ctx = g_option_context_new("- open a saved remote desktop");
    g_option_context_add_main_entries(ctx, entries, NULL);
    /*
     * ⚠ FALSE — DO NOT OPEN THE DISPLAY WHILE PARSING ARGUMENTS. With TRUE,
     * GTK connects during g_option_context_parse, so on a machine with no
     * display every bad command line answers "Cannot open display" instead of
     * saying which argument is wrong. Checking `--host` after that point means
     * never checking it at all where it would help most.
     */
    g_option_context_add_group(ctx, gtk_get_option_group(FALSE));
    if (!g_option_context_parse(ctx, &argc, &argv, &err)) {
        g_printerr("syn-remote-view: %s\n", err ? err->message : "bad arguments");
        return 2;
    }
    g_option_context_free(ctx);

    if (!host || !*host) {
        g_printerr("syn-remote-view: --host is required\n");
        return 2;
    }

    v.host     = host;
    v.port     = (port && *port) ? port : g_strdup("5900");
    v.name     = name ? name : g_strdup("");
    v.user     = user ? user : NULL;
    v.password = read_password_from_stdin();

    /*
     * ⚠ READ HERE, NOT IN THE CALLBACK. The credential handler runs inside
     * gtk-vnc's handshake; a file read that fails there has nowhere useful to
     * report to and would look like a rejected certificate. A missing pin is
     * worth saying out loud, once, before any of it starts.
     */
    if (cacert) {
        GError *e = NULL;
        if (!g_file_get_contents(cacert, &v.cacert, NULL, &e)) {
            g_printerr("syn-remote-view: cannot read the pinned certificate %s: %s\n",
                       cacert, e ? e->message : "unknown error");
            g_clear_error(&e);
            v.cacert = NULL;
        }
    }

    /*
     * ⚠ gtk_init_check, NOT gtk_init. gtk_init prints its own message and calls
     * exit() on a machine with no display — from inside a library, with a
     * status this program never chose. This one is reached whenever somebody
     * runs the viewer over SSH without a session, which is a mistake worth a
     * sentence rather than a stack of GTK noise.
     */
    if (!gtk_init_check(&argc, &argv)) {
        g_printerr("syn-remote-view: no display to open a window on.\n"
                   "This is the viewer; it needs a desktop to draw in.\n");
        return 1;
    }

    v.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    {
        char *title = g_strdup_printf("%s — Remote desktop",
                                      *v.name ? v.name : v.host);
        gtk_window_set_title(GTK_WINDOW(v.window), title);
        g_free(title);
    }
    gtk_window_set_default_size(GTK_WINDOW(v.window), 1280, 800);
    g_signal_connect(v.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(v.window, "key-press-event", G_CALLBACK(on_key), &v);

    v.stack = gtk_stack_new();
    gtk_container_add(GTK_CONTAINER(v.window), v.stack);

    v.status = gtk_label_new(NULL);
    gtk_label_set_justify(GTK_LABEL(v.status), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(v.status), TRUE);
    gtk_widget_set_valign(v.status, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(v.status, GTK_ALIGN_CENTER);
    gtk_stack_add_named(GTK_STACK(v.stack), v.status, "status");

    v.vnc = vnc_display_new();
    /*
     * ⚠ SCALED, OR A DESKTOP BIGGER THAN THIS WINDOW IS SIMPLY CROPPED. The
     * server sends its own resolution and gtk-vnc draws it 1:1 by default, so
     * a 2560-wide desktop in a 1280 window loses half the screen with no
     * scrollbar and no clue that it did.
     */
    vnc_display_set_scaling(VNC_DISPLAY(v.vnc), TRUE);
    vnc_display_set_keyboard_grab(VNC_DISPLAY(v.vnc), TRUE);
    vnc_display_set_pointer_grab(VNC_DISPLAY(v.vnc), TRUE);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), v.vnc);
    gtk_stack_add_named(GTK_STACK(v.stack), sw, "vnc");

    g_signal_connect(v.vnc, "vnc-auth-credential",  G_CALLBACK(on_credential),   &v);
    g_signal_connect(v.vnc, "vnc-connected",        G_CALLBACK(on_connected),    &v);
    g_signal_connect(v.vnc, "vnc-initialized",      G_CALLBACK(on_initialized),  &v);
    g_signal_connect(v.vnc, "vnc-disconnected",     G_CALLBACK(on_disconnected), &v);
    /*
     * ⚠ LOOKED UP RATHER THAN ASSUMED. These two are not in VncDisplayClass's
     * struct, so a gtk-vnc that dropped or renamed either would turn a plain
     * g_signal_connect into a runtime warning on every launch. Asking first
     * costs one call and keeps the window quiet on a library we do not pin.
     */
    if (g_signal_lookup("vnc-auth-failure", VNC_TYPE_DISPLAY))
        g_signal_connect(v.vnc, "vnc-auth-failure", G_CALLBACK(on_auth_failure), &v);
    if (g_signal_lookup("vnc-auth-unsupported", VNC_TYPE_DISPLAY))
        g_signal_connect(v.vnc, "vnc-auth-unsupported",
                         G_CALLBACK(on_auth_unsupported), &v);

    {
        char *m = g_markup_printf_escaped("Connecting to %s\xe2\x80\xa6",
                                          *v.name ? v.name : v.host);
        status_set(&v, m);
        g_free(m);
    }
    gtk_widget_show_all(v.window);
    gtk_stack_set_visible_child_name(GTK_STACK(v.stack), "status");

    if (!vnc_display_open_host(VNC_DISPLAY(v.vnc), v.host, v.port)) {
        char *m = g_markup_printf_escaped("<b>Could not open %s:%s.</b>",
                                          v.host, v.port);
        status_set(&v, m);
        g_free(m);
    }

    gtk_main();

    if (v.password) {
        memset(v.password, 0, strlen(v.password));
        g_free(v.password);
    }
    return 0;
}
