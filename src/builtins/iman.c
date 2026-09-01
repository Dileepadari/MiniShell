/*
 * iman.c - show a manual page.
 *
 * The page comes from man.he.net, which serves HTTPS only: a request to port 80
 * is answered with a redirect and nothing else. So the fetch needs TLS, and TLS
 * needs OpenSSL, which is optional here. The Makefile defines HAVE_OPENSSL when
 * the library is present; without it, and whenever the network attempt fails,
 * the command falls back to the man page installed on this machine.
 */
#include "minishell.h"
#include "builtins.h"
#include "util.h"

#include <ctype.h>
#include <netdb.h>
#include <strings.h>
#include <sys/socket.h>

#ifdef HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

#define MAN_HOST "man.he.net"

/* --------------------------------------------------------------- HTML --- */

static const struct { const char *entity; char replacement; } entities[] = {
    { "&lt;",   '<'  }, { "&gt;",   '>'  }, { "&amp;",  '&'  },
    { "&quot;", '"'  }, { "&#39;",  '\'' }, { "&apos;", '\'' },
    { "&nbsp;", ' '  },
};

/* Copy `html` into a new string with tags removed and entities decoded. */
static char *strip_html(const char *html, size_t len)
{
    sbuf_t out;
    sbuf_init(&out);

    for (size_t i = 0; i < len; i++) {
        if (html[i] == '<') {
            while (i < len && html[i] != '>') i++;
            continue;
        }
        if (html[i] == '&') {
            int decoded = 0;
            for (size_t e = 0; e < sizeof(entities) / sizeof(entities[0]); e++) {
                size_t elen = strlen(entities[e].entity);
                if (i + elen <= len && !strncmp(html + i, entities[e].entity, elen)) {
                    sbuf_putc(&out, entities[e].replacement);
                    i += elen - 1;
                    decoded = 1;
                    break;
                }
            }
            if (decoded) continue;
        }
        sbuf_putc(&out, html[i]);
    }
    return sbuf_release(&out);
}

/* Case-insensitive search: the site mixes <PRE> and <pre>. */
static const char *find_ci(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++)
        if (strncasecmp(p, needle, n) == 0) return p;
    return NULL;
}

/* ------------------------------------------------------------ network --- */

static int connect_to_host(const char *host, const char *port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *list = NULL;
    if (getaddrinfo(host, port, &hints, &list) != 0) return -1;

    int fd = -1;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    return fd;
}

static void build_request(char *out, size_t n, const char *topic)
{
    snprintf(out, n,
             "GET /?topic=%s&section=all HTTP/1.1\r\n"
             "Host: " MAN_HOST "\r\n"
             "User-Agent: minishell/" MINISHELL_VERSION "\r\n"
             "Accept: text/html\r\n"
             "Connection: close\r\n\r\n",
             topic);
}

#ifdef HAVE_OPENSSL
/* Fetch over TLS. Returns the whole response, headers included, or NULL. */
static char *fetch_https(const char *topic)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;

    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    int fd = connect_to_host(MAN_HOST, "443");
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return NULL;
    }

    SSL *ssl = SSL_new(ctx);
    char *response = NULL;

    if (ssl && SSL_set_fd(ssl, fd) == 1 &&
        SSL_set_tlsext_host_name(ssl, MAN_HOST) == 1 &&
        SSL_set1_host(ssl, MAN_HOST) == 1 &&
        SSL_connect(ssl) == 1) {

        char request[1024];
        build_request(request, sizeof(request), topic);

        if (SSL_write(ssl, request, (int)strlen(request)) > 0) {
            sbuf_t body;
            sbuf_init(&body);
            char chunk[8192];
            int got;
            while ((got = SSL_read(ssl, chunk, sizeof(chunk))) > 0)
                sbuf_putn(&body, chunk, (size_t)got);
            response = sbuf_release(&body);
        }
        SSL_shutdown(ssl);
    }

    if (ssl) SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    return response;
}
#endif

/*
 * Pull the manual text out of an HTTP response. Returns NULL when the response
 * carries no page, which covers both an unknown command and a redirect.
 */
static char *extract_manual(const char *response)
{
    if (!response) return NULL;

    /* The tag carries attributes, so match the name and skip to its `>`. */
    const char *start = find_ci(response, "<PRE");
    if (!start) return NULL;
    start = strchr(start, '>');
    if (!start) return NULL;
    start++;

    const char *end = find_ci(start, "</PRE");
    if (!end) return NULL;
    char *text = strip_html(start, (size_t)(end - start));
    str_trim(text);

    if (text[0] == '\0') {
        free(text);
        return NULL;
    }
    return text;
}

/* ----------------------------------------------------------- fallback --- */

/* Run the locally installed man(1). Returns its exit status, or -1 if absent. */
static int local_man(const char *topic)
{
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        execlp("man", "man", topic, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) continue;

    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* --------------------------------------------------------------- entry --- */

int builtin_iman(shell_t *sh, int argc, char **argv)
{
    (void)sh;

    if (argc != 2) {
        fprintf(stderr, "iMan: usage: iMan <command>\n");
        return 1;
    }

    /* The topic is pasted into a URL, so allow only characters that are safe
     * there. This is what stops a crafted argument from forging a request. */
    for (const char *c = argv[1]; *c; c++) {
        if (!isalnum((unsigned char)*c) && !strchr("-_.+", *c)) {
            fprintf(stderr, "iMan: `%s` is not a valid command name\n", argv[1]);
            return 1;
        }
    }

    char *manual = NULL;
#ifdef HAVE_OPENSSL
    char *response = fetch_https(argv[1]);
    manual = extract_manual(response);
    free(response);
#endif

    if (manual) {
        printf("%s\n", manual);
        free(manual);
        return 0;
    }

    /* Offline, or built without TLS: use the page installed on this machine. */
    int status = local_man(argv[1]);
    if (status >= 0) return status;

    fprintf(stderr, "iMan: no manual entry for %s\n", argv[1]);
    return 1;
}
