/*
 * synapd — SynapseOS AI Daemon
 * The persistent AI inference core for SynapseOS.
 *
 * Boots as PID-adjacent systemd service.
 * Exposes a Unix domain socket for kernel module (synapse_kmod)
 * and user-space clients (synsh, synguard, etc.).
 *
 * Architecture:
 *   main.c          → startup, signal handling, event loop
 *   inference.c     → llama.cpp bridge, model management
 *   socket_server.c → Unix socket IPC server
 *   context.c       → system context store (rolling window)
 *   scheduler.c     → AI hint feedback to kernel synapse sysfs
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <syslog.h>
#include <pthread.h>
#include <getopt.h>

#include "synapd.h"
#include "inference.h"
#include "socket_server.h"
#include "context.h"
#include "scheduler.h"
#include "log.h"

/* ── Global daemon state ──────────────────────────────────── */
struct synapd_state g_state = {
    .running       = 1,
    .debug         = 0,
    .model_loaded  = 0,
    .socket_fd     = -1,
    .requests_total = 0,
    .config = {
        .socket_path    = SYNAPD_SOCKET_PATH,
        .model_path     = SYNAPD_DEFAULT_MODEL,
        .context_window = 4096,
        .n_threads      = 4,
        .n_gpu_layers   = 0,   /* auto-detect at runtime */
        .log_level      = LOG_INFO,
        .max_clients    = 64,
    }
};

/* ── Signal handling ──────────────────────────────────────── */
static void signal_handler(int sig) {
    switch (sig) {
    case SIGTERM:
    case SIGINT:
        syn_log(LOG_INFO, "synapd: received signal %d, shutting down gracefully", sig);
        g_state.running = 0;
        break;
    case SIGHUP:
        syn_log(LOG_INFO, "synapd: SIGHUP — reloading config");
        synapd_reload_config(&g_state);
        break;
    case SIGPIPE:
        /* ignore — handled per-socket */
        break;
    default:
        break;
    }
}

static void setup_signals(void) {
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/* ── Daemonize ────────────────────────────────────────────── */
static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS); /* parent exits */

    if (setsid() < 0) { perror("setsid"); exit(EXIT_FAILURE); }

    /* Second fork — detach from session leader */
    pid = fork();
    if (pid < 0) { perror("fork2"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0027);
    chdir("/");

    /* Redirect stdio to /dev/null */
    int devnull = open("/dev/null", O_RDWR);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);

    /* Write PID file */
    FILE *pf = fopen(SYNAPD_PID_FILE, "w");
    if (pf) {
        fprintf(pf, "%d\n", getpid());
        fclose(pf);
    }
}

/* ── Runtime directory setup ─────────────────────────────── */
static int setup_runtime_dirs(void) {
    const char *dirs[] = {
        "/run/synapd",
        "/var/lib/synapd",
        "/var/log/synapd",
        "/var/lib/synapd/context",
        NULL
    };
    for (int i = 0; dirs[i]; i++) {
        if (mkdir(dirs[i], 0750) < 0 && errno != EEXIST) {
            fprintf(stderr, "synapd: cannot create %s: %s\n", dirs[i], strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* ── Usage ────────────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "SynapseOS AI Daemon — kernel-native inference core\n"
        "\n"
        "Options:\n"
        "  -m, --model PATH       Path to GGUF model file\n"
        "  -s, --socket PATH      Unix socket path (default: " SYNAPD_SOCKET_PATH ")\n"
        "  -t, --threads N        Inference threads (default: 4)\n"
        "  -g, --gpu-layers N     Layers to offload to GPU (default: auto)\n"
        "  -c, --context N        Context window tokens (default: 4096)\n"
        "  -d, --debug            Debug mode (no daemonize, verbose log)\n"
        "  -f, --foreground       Run in foreground (no daemonize)\n"
        "  -v, --version          Print version\n"
        "  -h, --help             This help\n",
        prog
    );
}

/* ── Main event loop ──────────────────────────────────────── */
static int run_event_loop(void) {
    syn_log(LOG_INFO, "synapd: entering main event loop");

    /*
     * Main loop delegates to the socket server's epoll/poll loop.
     * The socket server runs client connections in a thread pool.
     * This thread handles:
     *   - Periodic context flush to disk
     *   - Kernel synapse sysfs heartbeat
     *   - Stats logging
     */
    while (g_state.running) {
        /* Heartbeat: write AI daemon status to sysfs for kernel module */
        scheduler_heartbeat(&g_state);

        /* Flush context window to disk every 30s */
        static time_t last_flush = 0;
        time_t now = time(NULL);
        if (now - last_flush > 30) {
            context_flush(&g_state);
            last_flush = now;
        }

        /* Log stats every 60s */
        static time_t last_stats = 0;
        if (now - last_stats > 60) {
            syn_log(LOG_INFO,
                "synapd: stats — requests=%lu model=%s ctx_tokens=%u",
                g_state.requests_total,
                g_state.model_loaded ? "loaded" : "unloaded",
                context_used_tokens(&g_state)
            );
            last_stats = now;
        }

        sleep(1);
    }

    return 0;
}

/* ── Entry point ──────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int foreground = 0;

    static struct option long_opts[] = {
        {"model",      required_argument, 0, 'm'},
        {"socket",     required_argument, 0, 's'},
        {"threads",    required_argument, 0, 't'},
        {"gpu-layers", required_argument, 0, 'g'},
        {"context",    required_argument, 0, 'c'},
        {"debug",      no_argument,       0, 'd'},
        {"foreground", no_argument,       0, 'f'},
        {"version",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:s:t:g:c:dfvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'm': g_state.config.model_path     = optarg; break;
        case 's': g_state.config.socket_path    = optarg; break;
        case 't': g_state.config.n_threads      = atoi(optarg); break;
        case 'g': g_state.config.n_gpu_layers   = atoi(optarg); break;
        case 'c': g_state.config.context_window = atoi(optarg); break;
        case 'd': g_state.debug = 1; foreground = 1; break;
        case 'f': foreground = 1; break;
        case 'v':
            printf("synapd %s (SynapseOS — kernel-native AI daemon)\n", SYNAPD_VERSION);
            return 0;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    /* Must run as root for kernel sysfs writes */
    if (geteuid() != 0) {
        fprintf(stderr, "synapd: must run as root (needs /sys/kernel/synapse access)\n");
        return EXIT_FAILURE;
    }

    openlog("synapd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    syn_log_init(g_state.debug ? LOG_DEBUG : g_state.config.log_level);

    syn_log(LOG_INFO, "synapd %s starting up", SYNAPD_VERSION);

    if (setup_runtime_dirs() < 0)
        return EXIT_FAILURE;

    setup_signals();

    /* Initialize subsystems */
    if (context_init(&g_state) < 0) {
        syn_log(LOG_ERR, "synapd: context_init failed");
        return EXIT_FAILURE;
    }

    if (inference_init(&g_state) < 0) {
        syn_log(LOG_WARNING, "synapd: no model loaded — running in shell-assist mode only");
        g_state.inference = NULL;
    }

    if (socket_server_start(&g_state) < 0) {
        syn_log(LOG_ERR, "synapd: socket_server_start failed");
        return EXIT_FAILURE;
    }

    if (scheduler_init(&g_state) < 0) {
        syn_log(LOG_WARNING, "synapd: scheduler_init failed — synapse_kmod not loaded?");
        /* non-fatal: synapd works without the kernel module */
    }

    if (!foreground)
        daemonize();

    syn_log(LOG_INFO, "synapd: ready — socket=%s model=%s",
             g_state.config.socket_path, g_state.config.model_path);

    /* Notify systemd we're ready */
    sd_notify_ready();

    int ret = run_event_loop();

    /* Teardown */
    syn_log(LOG_INFO, "synapd: shutting down");
    socket_server_stop(&g_state);
    inference_destroy(&g_state);
    context_flush(&g_state);
    context_destroy(&g_state);
    scheduler_destroy(&g_state);

    unlink(SYNAPD_PID_FILE);
    closelog();

    return ret;
}
