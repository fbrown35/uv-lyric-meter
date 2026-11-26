#include "digital_motion.h"
#include <math.h>
void digital_motion_init(digital_motion_t *m)
{
    if (m) *m = (digital_motion_t){DIGITAL_FLOOR_DB, DIGITAL_FLOOR_DB, 0};
}
void digital_motion_step(digital_motion_t *m, float target, float dt)
{
    if (!m || !isfinite(target) || !isfinite(dt) || dt <= 0) return;
    target = fmaxf(DIGITAL_FLOOR_DB, fminf(3, target));
    if (target > m->level_db) m->level_db = target + (m->level_db - target) * expf(-dt / 0.005f);
    else m->level_db = fmaxf(target, m->level_db - 20 * dt);
    if (m->level_db >= m->peak_db) { m->peak_db = m->level_db; m->hold_seconds = 1.5f; }
    else {
        float decay_time = fmaxf(0, dt - m->hold_seconds);
        m->hold_seconds = fmaxf(0, m->hold_seconds - dt);
        m->peak_db = fmaxf(m->level_db, m->peak_db - 20 * decay_time);
    }
}
