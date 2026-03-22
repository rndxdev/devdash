#pragma once
#include "panel.h"

GtkWidget *sysstat_create(void);
void       sysstat_refresh(void);
void       sysstat_cleanup(void);
