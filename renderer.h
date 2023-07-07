#pragma once

#include "microui.h"

typedef struct HWND__ *HWND;
typedef long long (*WindowsMessageCallback)(HWND hwnd, unsigned int msg, unsigned long long wparam,
                                            long long lparam);

void r_init(WindowsMessageCallback wndproc);
void r_draw_rect(mu_Rect rect, mu_Color color);
void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color);
void r_draw_icon(int id, mu_Rect rect, mu_Color color);
int r_get_text_width(const char *text, int len);
int r_get_text_height(void);
void r_set_clip_rect(mu_Rect rect);
void r_clear(mu_Color color);
void r_present(void);