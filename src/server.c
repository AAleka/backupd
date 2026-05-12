#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <libgen.h>
#include <openssl/evp.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define EXIT_CODE_SUCCESS          0
#define EXIT_CODE_FAILURE          1
#define EXIT_CODE_INVALID_ARGS     2
#define EXIT_CODE_DAEMON_FAILURE   3
#define EXIT_CODE_SETUP_FAILURE    4

#define NUM_CONNECTIONS 100

// Log levels DEBUG=0, RELEASE=1
enum log_level_t
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_RELEASE = 1
};

enum log_type_t
{
    LOG_TYPE_DEBUG,
    LOG_TYPE_ERROR,
    LOG_TYPE_WARNING
};

// Function prototypes
int parse_args(int argc, char const* argv[], char* host, uint16_t* port, uint64_t* buffer_size, uint8_t* log_level, uint16_t* max_clients, uint16_t* timeout);
void print_usage(const char* program_name, const char* host, uint16_t port, uint64_t buffer_size, uint8_t log_level, uint16_t max_clients, uint16_t timeout);

void print_log(enum log_type_t type, const char* format, ...);
void handle_signal(int signal);

int init_daemon(void);
int setup_server(const char* host, uint16_t port);

uint8_t  log_level = LOG_LEVEL_RELEASE;
volatile sig_atomic_t running = 1;

int main(int argc, char const* argv[])
{
    char     host[256]     = "localhost";
    uint16_t port          = 8080;
    uint64_t buffer_size   = 1024 * 1024 * 4;
    uint16_t max_clients   = 100;
    uint16_t timeout       = 30;

    openlog("backupd", LOG_PID|LOG_CONS, LOG_DAEMON);  // log into journalctl

    print_log(LOG_TYPE_DEBUG, "Starting backup daemon...");

    if (parse_args(argc, argv, host, &port, &buffer_size, &log_level, &max_clients, &timeout) != EXIT_CODE_SUCCESS)
        return EXIT_CODE_INVALID_ARGS;

    int result = EXIT_CODE_SUCCESS;

    result = init_daemon();
    if (result != EXIT_CODE_SUCCESS)
    {
        print_log(LOG_TYPE_ERROR, "Failed to initialize daemon");

        return EXIT_CODE_FAILURE;
    }

    int listen_fd = setup_server(host, port);
    if (listen_fd < 0)
    {
        print_log(LOG_TYPE_ERROR, "Failed to set up server");

        return EXIT_CODE_FAILURE;
    }

    // TODO: Implement epoll-based event loop to handle client connections and data transfer
    // TODO: Implement object storage, encryption, and backup logic
    // TODO: Implement SQL database integration for metadata management
    // TODO: Implement versioning based on full paths and metadata
    while (running)
    {
        print_log(LOG_TYPE_WARNING, "Server is running...");
        sleep(5);
    }

    closelog();
    close(listen_fd);

    return EXIT_CODE_SUCCESS;
}

int setup_server(const char* host, uint16_t port)
{
    print_log(LOG_TYPE_DEBUG, "Setting up server on %s:%d", host, port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    struct addrinfo* res = NULL;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0)
    {
        print_log(LOG_TYPE_ERROR, "Failed to get address info: %s", gai_strerror(err));
        freeaddrinfo(res);

        return -EXIT_CODE_SETUP_FAILURE;
    }

    int listen_fd = -1;
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next)
    {
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd < 0)
        {
            continue;
        }

        int yes = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0)
        {
            print_log(LOG_TYPE_ERROR, "Failed to set SO_REUSEADDR: %s", strerror(errno));
            close(listen_fd);
            listen_fd = -1;
            continue;
        }

        fcntl(listen_fd, F_SETFL, O_NONBLOCK);
        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) == 0)
        {
            break;
        }

        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(res);

    if (listen_fd < 0)
    {
        print_log(LOG_TYPE_ERROR, "Failed to bind socket: %s", strerror(errno));

        return -EXIT_CODE_SETUP_FAILURE;
    }

    if (listen(listen_fd, NUM_CONNECTIONS) < 0)
    {
        print_log(LOG_TYPE_ERROR, "Failed to listen on socket: %s", strerror(errno));
        close(listen_fd);

        return -EXIT_CODE_SETUP_FAILURE;
    }

    print_log(LOG_TYPE_DEBUG, "Listening on %s:%d", host, port);

    return listen_fd;
}

int init_daemon(void)
{
    pid_t pid = 0, sid = 0;

    if ((pid = fork()) < 0)
    {
        print_log(LOG_TYPE_ERROR, "Failed to fork process: %s", strerror(errno));

        return EXIT_CODE_DAEMON_FAILURE;
    }
    else if (pid > 0)
    {
        print_log(LOG_TYPE_DEBUG, "Daemon process created with PID %d", pid);
        print_log(LOG_TYPE_DEBUG, "Exiting parent process...");

        exit(EXIT_CODE_SUCCESS);
    }

    if ((sid = setsid()) < 0)
    {
        print_log(LOG_TYPE_ERROR, "Failed to create new session: %s", strerror(errno));

        return EXIT_CODE_DAEMON_FAILURE;
    }
    print_log(LOG_TYPE_DEBUG, "New session created with SID %d", sid);

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    if (chdir("/") < 0)
    {
        print_log(LOG_TYPE_ERROR, "Failed to change working directory: %s", strerror(errno));

        return EXIT_CODE_DAEMON_FAILURE;
    }
    print_log(LOG_TYPE_DEBUG, "Changed working directory to /");

    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    print_log(LOG_TYPE_DEBUG, "Daemon fd closed");

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    return EXIT_CODE_SUCCESS;
}

int parse_args(int argc, char const* argv[], char* host, uint16_t* port, uint64_t* buffer_size, uint8_t* log_level, uint16_t* max_clients, uint16_t* timeout)
{
    for (int i = 1; i < argc; i += 2)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0], host, *port, *buffer_size, *log_level, *max_clients, *timeout);

            return -EXIT_CODE_INVALID_ARGS;
        }

        if (i + 1 >= argc)
        {
            fprintf(stderr, "Missing value for argument: %s\n", argv[i]);
            print_usage(argv[0], host, *port, *buffer_size, *log_level, *max_clients, *timeout);

            return -EXIT_CODE_INVALID_ARGS;
        }

        if (strcmp(argv[i], "--host") == 0 || strcmp(argv[i], "-h") == 0)
        {
            strcpy(host, argv[i + 1]);
        }
        else if (strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0)
        {
            *port = (uint16_t)strtoul(argv[i + 1], NULL, 10);
        }
        else if (strcmp(argv[i], "--buffer") == 0 || strcmp(argv[i], "-b") == 0)
        {
            *buffer_size = strtoull(argv[i + 1], NULL, 10);
        }
        else if (strcmp(argv[i], "--log") == 0 || strcmp(argv[i], "-l") == 0)
        {
            if (strcmp(argv[i + 1], "debug") == 0)
            {
                *log_level = LOG_LEVEL_DEBUG;
            }
            else if (strcmp(argv[i + 1], "release") == 0)
            {
                *log_level = LOG_LEVEL_RELEASE;
            }
            else
            {
                fprintf(stderr, "Invalid log level: %s\n", argv[i + 1]);
                print_usage(argv[0], host, *port, *buffer_size, *log_level, *max_clients, *timeout);

                return -EXIT_CODE_INVALID_ARGS;
            }
        }
        else if (strcmp(argv[i], "--clients") == 0 || strcmp(argv[i], "-c") == 0)
        {
            *max_clients = (uint16_t)strtoul(argv[i + 1], NULL, 10);
        }
        else if (strcmp(argv[i], "--timeout") == 0 || strcmp(argv[i], "-t") == 0)
        {
            *timeout = (uint16_t)strtoul(argv[i + 1], NULL, 10);
        }
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0], host, *port, *buffer_size, *log_level, *max_clients, *timeout);

            return -EXIT_CODE_INVALID_ARGS;
        }
    }

    return EXIT_CODE_SUCCESS;
}

void print_usage(const char* program_name, const char* host, uint16_t port, uint64_t buffer_size, uint8_t log_level, uint16_t max_clients, uint16_t timeout)
{
    const char* usage =
        "This is a backup daemon.\n"
        "Usage: %s\n"
        "Arguments:\n"
        "    --help\n"
        "    --host / -h     INT    Host to listen on (default: %s)\n"
        "    --port / -p     INT    Port number to listen on (default: %d)\n"
        "    --buffer / -b   INT    Size of the buffer for receiving data (default: %d bytes)\n"
        "    --log / -l      STR    Log level: debug or release (default: %s)\n"
        "    --clients / -c  INT    Maximum number of concurrent clients (default: %d)\n"
        "    --timeout / -t  INT    Timeout for client connections in seconds (default: %d seconds)\n"
        "Monitor: 'journalctl -t backupd -f'\n";

    printf(usage, program_name, host, port, buffer_size, log_level == LOG_LEVEL_DEBUG ? "debug" : "release", max_clients, timeout);
}

void handle_signal (int signal)
{
    switch (signal)
    {
        case SIGTERM:
        case SIGINT:
            print_log(LOG_TYPE_DEBUG, "Received signal %d, shutting down...", signal);
            running = 0;
            break;
        default:
            print_log(LOG_TYPE_WARNING, "Received unknown signal %d", signal);
    }
}

void print_log(enum log_type_t type, const char* format, ...)
{
    if (type == LOG_TYPE_DEBUG && log_level == LOG_LEVEL_RELEASE)
        return;

    va_list args;
    va_start(args, format);

    char msg[1024];
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    switch (type)
    {
#if LOG_LEVEL == LOG_LEVEL_DEBUG
        case LOG_TYPE_DEBUG:
            syslog(LOG_DEBUG, "%10s %s\n", "[DEBUG]", msg);
            break;
#endif
        case LOG_TYPE_ERROR:
            syslog(LOG_ERR, "%10s %s\n", "[ERROR]", msg);
            break;
        case LOG_TYPE_WARNING:
            syslog(LOG_WARNING, "%10s %s\n", "[WARNING]", msg);
            break;
        default:
            syslog(LOG_WARNING, "%10s %s\n", "WARNING:", "unknown log type.");
    }
}
