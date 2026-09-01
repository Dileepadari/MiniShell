/*
 * main.c - argument handling and entry point.
 */
#include "minishell.h"
#include "shell.h"
#include "util.h"

static void print_usage(FILE *out, const char *program)
{
    fprintf(out,
        "Usage: %s [options] [script]\n"
        "\n"
        "  -c <command>   run <command> and exit\n"
        "  -h, --help     show this message\n"
        "  -v, --version  show the version\n"
        "\n"
        "With no arguments the shell reads commands interactively.\n",
        program);
}

int main(int argc, char **argv)
{
    const char *command = NULL;
    const char *script  = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "minishell: -c needs a command\n");
                return 2;
            }
            command = argv[++i];
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("minishell %s\n", MINISHELL_VERSION);
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "minishell: unknown option `%s`\n", argv[i]);
            print_usage(stderr, argv[0]);
            return 2;
        } else {
            script = argv[i];
            break;
        }
    }

    shell_init(&g_shell);

    int status;
    if (command) {
        status = shell_run_line(&g_shell, command);
    } else if (script) {
        FILE *in = fopen(script, "r");
        if (!in) {
            fprintf(stderr, "minishell: %s: %s\n", script, strerror(errno));
            shell_shutdown(&g_shell);
            return 1;
        }
        status = shell_run_stream(&g_shell, in, 0);
        fclose(in);
    } else if (g_shell.interactive) {
        status = shell_run_interactive(&g_shell);
    } else {
        status = shell_run_stream(&g_shell, stdin, 1);
    }

    shell_shutdown(&g_shell);
    return status;
}
