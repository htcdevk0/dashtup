#include "ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_use_color = 0;

static const char *c_reset(void)   { return g_use_color ? "\033[0m" : ""; }
static const char *c_bold(void)    { return g_use_color ? "\033[1m" : ""; }
static const char *c_blue(void)    { return g_use_color ? "\033[34m" : ""; }
static const char *c_cyan(void)    { return g_use_color ? "\033[36m" : ""; }
static const char *c_green(void)   { return g_use_color ? "\033[32m" : ""; }
static const char *c_yellow(void)  { return g_use_color ? "\033[33m" : ""; }
static const char *c_red(void)     { return g_use_color ? "\033[31m" : ""; }
static const char *c_magenta(void) { return g_use_color ? "\033[35m" : ""; }

void ui_init(void) {
    g_use_color = isatty(STDOUT_FILENO);
}

void ui_blank(void) {
    puts("");
}

void ui_banner(void) {
    printf("%s%s========================================\n%s", c_bold(), c_cyan(), c_reset());
    printf("%s%s                DASHTUP                 \n%s", c_bold(), c_cyan(), c_reset());
    printf("%s%s      Dash Programming Language         \n%s", c_bold(), c_cyan(), c_reset());
    printf("%s%s========================================\n%s", c_bold(), c_cyan(), c_reset());
}

void ui_title(const char *text) {
    printf("%s%s%s\n%s", c_bold(), c_magenta(), text, c_reset());
}

void ui_step(int current, int total, const char *text) {
    printf("%s%s[%d/%d]%s %s\n", c_bold(), c_blue(), current, total, c_reset(), text);
}

void ui_info(const char *text) {
    printf("%s[info]%s %s\n", c_cyan(), c_reset(), text);
}

void ui_warn(const char *text) {
    printf("%s[warn]%s %s\n", c_yellow(), c_reset(), text);
}

void ui_error(const char *text) {
    printf("%s[error]%s %s\n", c_red(), c_reset(), text);
}

void ui_success(const char *text) {
    printf("%s[done]%s %s\n", c_green(), c_reset(), text);
}

bool ui_confirm(const char *question, bool default_yes) {
    char buffer[32];

    printf("%s%s %s %s", c_bold(), question, default_yes ? "[Y/n]" : "[y/N]", c_reset());
    fflush(stdout);

    if (!fgets(buffer, sizeof(buffer), stdin)) {
        return default_yes;
    }

    for (size_t i = 0; buffer[i] != '\0'; ++i) {
        if (!isspace((unsigned char)buffer[i])) {
            char c = (char)tolower((unsigned char)buffer[i]);
            if (c == 'y') {
                return true;
            }
            if (c == 'n') {
                return false;
            }
            break;
        }
    }

    return default_yes;
}
