#include "minishell.h"
#include "prompt.h"
#include "util.h"

#include <pwd.h>

#define C_USER  "\033[1;32m"
#define C_PATH  "\033[1;34m"
#define C_TIME  "\033[1;33m"
#define C_RESET "\033[0m"

/* getlogin() fails when there is no utmp entry, which is common under a
 * container or a bare `script` session, so fall back through the environment. */
static const char *current_user(void)
{
    const char *user = getlogin();
    if (user && *user) return user;

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name && *pw->pw_name) return pw->pw_name;

    user = getenv("USER");
    return (user && *user) ? user : "user";
}

void prompt_build(shell_t *sh, char *out, size_t n, int color, size_t *width)
{
    char host[256];
    if (gethostname(host, sizeof(host)) != 0) strcpy(host, "localhost");
    host[sizeof(host) - 1] = '\0';

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");

    char shown[PATH_MAX];
    path_abbreviate(shown, sizeof(shown), cwd, sh->home);

    const char *user = current_user();

    char timing[128] = "";
    if (sh->slow_cmd[0])
        snprintf(timing, sizeof(timing), " %s : %ds", sh->slow_cmd, sh->slow_secs);

    if (width)
        *width = strlen(user) + 1 + strlen(host) + 1 + strlen(shown) +
                 strlen(timing) + 3; /* '<', '>', trailing space */

    if (color)
        snprintf(out, n, "<" C_USER "%s@%s" C_RESET ":" C_PATH "%s" C_RESET
                         C_TIME "%s" C_RESET "> ",
                 user, host, shown, timing);
    else
        snprintf(out, n, "<%s@%s:%s%s> ", user, host, shown, timing);
}
