#ifndef UI_SIDEBAR_H
#define UI_SIDEBAR_H

#define WIDGET_SIDEBAR_WIDTH 270.0f

void widget_sidebar_render();

// Animated slide-out drawer helpers
bool widget_sidebar_is_open();
void widget_sidebar_set_open(bool open);
void widget_sidebar_toggle();
float widget_sidebar_current_width();
void widget_sidebar_begin_layout();
void widget_sidebar_end_layout();
void widget_sidebar_toggle_button();

#endif
