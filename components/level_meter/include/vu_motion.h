// Frame-rate independent VU ballistics (SPEC 7.3), with no ESP-IDF dependencies.
#pragma once

// zeta=0.815 gives 1.205% overshoot; omega=13.6 reaches 99% in 300 ms.
#define VU_MOTION_OMEGA 13.6f
#define VU_MOTION_ZETA 0.815f
#define VU_MOTION_K (VU_MOTION_OMEGA * VU_MOTION_OMEGA)
#define VU_MOTION_C (2.0f * VU_MOTION_ZETA * VU_MOTION_OMEGA)

typedef struct {
    float position;  // Linear amplitude; 1.0 = 0 VU, not a dB fraction.
    float velocity;
} vu_motion_t;

void vu_motion_reset(vu_motion_t *motion);
float vu_motion_target(float rms_dbfs, float reference_dbfs);
// Exact solution of the damped spring for a constant target over dt seconds.
// Invalid input leaves the state unchanged; targets are bounded to +3 VU.
float vu_motion_step(vu_motion_t *motion, float target, float dt);
