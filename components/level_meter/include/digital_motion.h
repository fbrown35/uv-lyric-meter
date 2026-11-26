// Pure-C peak envelope and 1.5 s peak hold (SPEC 6.2, 7.4).
#pragma once
#define DIGITAL_FLOOR_DB (-60.0f)
typedef struct { float level_db, peak_db, hold_seconds; } digital_motion_t;
void digital_motion_init(digital_motion_t *m);
void digital_motion_step(digital_motion_t *m, float target_db, float dt);
