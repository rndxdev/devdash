#pragma once
#include "panel.h"

GtkWidget *battery_create(void);
void       battery_refresh(void);
void       battery_cleanup(void);
