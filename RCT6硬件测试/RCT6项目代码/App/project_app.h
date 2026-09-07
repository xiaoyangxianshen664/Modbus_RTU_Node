#ifndef PROJECT_APP_H
#define PROJECT_APP_H

#include "FreeRTOS.h"

void project_diagnostics_init(void);
void project_log_storage_mount(void);
void project_app_start(void);
void project_modbus_notify_from_isr(void);

#endif /* PROJECT_APP_H */
