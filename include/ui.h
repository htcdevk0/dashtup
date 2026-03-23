#ifndef DASHTUP_UI_H
#define DASHTUP_UI_H

#include <stdbool.h>

void ui_init(void);
void ui_banner(void);
void ui_blank(void);
void ui_title(const char *text);
void ui_step(int current, int total, const char *text);
void ui_info(const char *text);
void ui_warn(const char *text);
void ui_error(const char *text);
void ui_success(const char *text);
bool ui_confirm(const char *question, bool default_yes);

#endif
