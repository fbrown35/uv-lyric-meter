#include "vu_motion.h"
#include <math.h>

void vu_motion_reset(vu_motion_t *m)
{
    if (m) *m = (vu_motion_t){0};
}

float vu_motion_target(float rms_dbfs, float reference_dbfs)
{
    if (!isfinite(rms_dbfs) || !isfinite(reference_dbfs)) return 0;
    float db = rms_dbfs - reference_dbfs;
    if (db < -80) return 0;
    if (db > 3) db = 3;
    return powf(10.0f, db / 20.0f);
}

float vu_motion_step(vu_motion_t *m, float target, float dt)
{
    if (!m) return 0;
    if (!isfinite(dt) || dt <= 0 || !isfinite(target)) return m->position;
    target = fmaxf(0, fminf(target, 1.412538f));
    const float a = VU_MOTION_ZETA * VU_MOTION_OMEGA;
    const float w = VU_MOTION_OMEGA * sqrtf(1 - VU_MOTION_ZETA * VU_MOTION_ZETA);
    float y = m->position - target;
    float b = (m->velocity + a * y) / w;
    float decay = expf(-a * dt);
    float cs = cosf(w * dt), sn = sinf(w * dt);
    m->position = target + decay * (y * cs + b * sn);
    m->velocity = decay * ((-a * y + w * b) * cs + (-a * b - w * y) * sn);
    return m->position;
}
