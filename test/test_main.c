// Host (PC) unit tests for the pure-C modules. Build and run: make -C test
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_ring.h"
#include "level_meter.h"
#include "vu_motion.h"
#include "digital_motion.h"
#include "display_scan.h"
#include "lrc.h"
#include "wav.h"
#include "pcm_prepare.h"
#include "image_pixels.h"
#include "recog_retry.h"

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
    do {                                                                         \
        double _a = (a), _b = (b);                                               \
        if (fabs(_a - _b) > (tol)) {                                             \
            printf("FAIL %s:%d: %s = %.3f, expected %.3f +/- %.3f\n", __FILE__,  \
                   __LINE__, #a, _a, _b, (double)(tol));                         \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

#define FS 48000
#define BLOCK (FS / 100) // 10 ms

static void make_sine(int16_t *dst, size_t n, double freq, double amp, size_t phase0)
{
    for (size_t i = 0; i < n; i++) {
        dst[i] = (int16_t)lrint(amp * 32767.0 * sin(2.0 * M_PI * freq * (double)(i + phase0) / FS));
    }
}

// Runs a signal through the meter for 1 s and returns the last block's result (filter settled).
static level_result_t settle(level_meter_t *m, double freq, double amp, int16_t dc)
{
    int16_t blk[BLOCK];
    level_result_t r = {0};
    for (int b = 0; b < 100; b++) {
        make_sine(blk, BLOCK, freq, amp, (size_t)b * BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            int v = blk[i] + dc;
            blk[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        r = level_meter_process(m, blk, BLOCK);
    }
    return r;
}

static void test_level_meter(void)
{
    level_meter_t m;

    level_meter_init(&m, FS);
    level_result_t r = settle(&m, 1000.0, 1.0, 0);
    CHECK_NEAR(r.rms_dbfs, 0.0, 0.1);   // full-scale sine = 0 dBFS (AES17)
    CHECK_NEAR(r.peak_dbfs, 0.0, 0.1);

    level_meter_init(&m, FS);
    r = settle(&m, 1000.0, pow(10.0, -18.0 / 20.0), 0);
    CHECK_NEAR(r.rms_dbfs, -18.0, 0.1); // default vu_ref_dbfs
    CHECK_NEAR(r.peak_dbfs, -18.0, 0.1);

    // DC offset is removed by the 20 Hz high-pass.
    level_meter_init(&m, FS);
    r = settle(&m, 1000.0, 0.1, 8000);
    CHECK_NEAR(r.rms_dbfs, -20.0, 0.2);

    // 20 Hz sits at the corner: about -3 dB. A 10 ms block is shorter than one 20 Hz period,
    // so measure over whole periods (100 ms blocks) after settling.
    level_meter_init(&m, FS);
    {
        static int16_t big[FS / 10];
        for (int b = 0; b < 10; b++) {
            make_sine(big, FS / 10, 20.0, 0.5, (size_t)b * (FS / 10));
            r = level_meter_process(&m, big, FS / 10);
        }
    }
    CHECK_NEAR(r.rms_dbfs, -6.02 - 3.0, 0.5);

    // Silence reports the floor, not -inf/NaN.
    level_meter_init(&m, FS);
    int16_t zeros[BLOCK] = {0};
    r = level_meter_process(&m, zeros, BLOCK);
    CHECK(r.rms_dbfs == LEVEL_FLOOR_DBFS);
    CHECK(r.peak_dbfs == LEVEL_FLOOR_DBFS);
    r = level_meter_process(&m, zeros, 0);
    CHECK(r.rms_dbfs == LEVEL_FLOOR_DBFS);

    CHECK(level_amplitude_to_dbfs(0.0f) == LEVEL_FLOOR_DBFS);
    CHECK_NEAR(level_amplitude_to_dbfs(0.5f), -6.02, 0.01);
}

static void test_audio_ring(void)
{
    enum { CAP = 10 };
    int16_t store[CAP];
    int16_t out[CAP];
    int16_t seq[25];
    for (int i = 0; i < 25; i++) {
        seq[i] = (int16_t)i;
    }
    audio_ring_t r;
    uint64_t idx = 99;

    audio_ring_init(&r, store, CAP);
    CHECK(!audio_ring_read_latest(&r, out, 1, &idx));

    audio_ring_write(&r, seq, 4);
    CHECK(!audio_ring_read_latest(&r, out, 5, &idx));
    CHECK(audio_ring_read_latest(&r, out, 4, &idx));
    CHECK(idx == 0 && out[0] == 0 && out[3] == 3);

    // Wrap around: total 4 + 9 = 13 written, ring holds 3..12.
    audio_ring_write(&r, seq + 4, 9);
    CHECK(audio_ring_read_latest(&r, out, CAP, &idx));
    CHECK(idx == 3);
    for (int i = 0; i < CAP; i++) {
        CHECK(out[i] == 3 + i);
    }
    CHECK(audio_ring_read_latest(&r, out, 3, &idx));
    CHECK(idx == 10 && out[0] == 10 && out[2] == 12);
    CHECK(!audio_ring_read_latest(&r, out, CAP + 1, &idx));

    // Oversized write keeps only the tail and the absolute counter stays correct.
    audio_ring_init(&r, store, CAP);
    audio_ring_write(&r, seq, 25);
    CHECK(r.total_written == 25);
    CHECK(audio_ring_read_latest(&r, out, CAP, &idx));
    CHECK(idx == 15 && out[0] == 15 && out[9] == 24);
}

static void test_vu_motion(void)
{
    CHECK_NEAR(vu_motion_target(-18, -18), 1, 0.00001);
    CHECK_NEAR(vu_motion_target(-38, -18), 0.1, 0.00001);
    CHECK_NEAR(vu_motion_target(0, -18), pow(10, 3.0 / 20), 0.00001);
    CHECK(vu_motion_target(NAN, -18) == 0);
    // Verify timing and overshoot at actual UI frame rates and 1 ms reference steps.
    const int rates[] = {30, 60, 1000};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        vu_motion_t m;
        vu_motion_reset(&m);
        float maximum = 0, rise = 0;
        for (int i = 1; i <= rates[r] * 2; i++) {
            float value = vu_motion_step(&m, 1, 1.0f / rates[r]);
            CHECK(isfinite(value));
            if (!rise && value >= 0.99f) rise = (float)i / rates[r];
            if (value > maximum) maximum = value;
        }
        CHECK(rise >= 0.29f && rise <= 0.334f);
        CHECK(maximum >= 1.01f && maximum <= 1.015f);
        CHECK_NEAR(m.position, 1, 0.0001);
        for (int i = 0; i < rates[r] * 2; i++) vu_motion_step(&m, 0, 1.0f / rates[r]);
        CHECK_NEAR(m.position, 0, 0.0001);
    }
    vu_motion_t a = {0}, b = {0};
    const float jitter[] = {0.011f, 0.023f, 0.041f, 0.007f, 0.218f};
    for (unsigned i = 0; i < 5; i++) vu_motion_step(&a, 1, jitter[i]);
    vu_motion_step(&b, 1, 0.3f);
    CHECK_NEAR(a.position, b.position, 0.00001);
    float before = a.position;
    CHECK(vu_motion_step(&a, 1, NAN) == before);
    CHECK(vu_motion_step(&a, NAN, 1) == before);
    CHECK(vu_motion_step(&a, 1, -1) == before);
    CHECK(isfinite(vu_motion_step(&a, 1, 30)));
}

static void test_display_scan(void)
{
    uint16_t dst[24] = {0};
    const uint16_t src[] = {1, 2, 99, 3, 4, 99, 5, 6, 99};
    CHECK(display_scan_copy(dst, 4, 6, 4, 1, 0, 2, 2, src, 3));
    CHECK(dst[4 * 4 + 1] == 1 && dst[4 * 4 + 2] == 2);
    CHECK(dst[5 * 4 + 1] == 3 && dst[5 * 4 + 2] == 4);
    CHECK(dst[1] == 5 && dst[2] == 6);
    CHECK(dst[0] == 0 && dst[3] == 0 && dst[5] == 0);
    uint16_t before[24];
    memcpy(before, dst, sizeof(dst));
    CHECK(!display_scan_copy(dst, 4, 6, 6, 1, 0, 2, 2, src, 3));
    CHECK(!display_scan_copy(dst, 4, 6, -1, 1, 0, 2, 2, src, 3));
    CHECK(!display_scan_copy(dst, 4, 6, 0, -1, 0, 2, 2, src, 3));
    CHECK(!display_scan_copy(dst, 4, 6, 0, 1, 0, 4, 2, src, 3));
    CHECK(!display_scan_copy(dst, 4, 6, 0, 1, 0, 2, 6, src, 3));
    CHECK(!display_scan_copy(dst, 4, 6, 0, 1, 0, 2, 2, src, 1));
    CHECK(memcmp(before, dst, sizeof(dst)) == 0);
    CHECK(display_scan_copy(dst, 4, 6, 0, 0, 3, 1, 5, src, 3));
    CHECK(dst[12] == 1 && dst[16] == 3 && dst[20] == 5);
}

static void test_digital_motion(void)
{
    digital_motion_t m; digital_motion_init(&m);
    digital_motion_step(&m, 0, 0.05f);
    CHECK_NEAR(m.level_db, 0, 0.01);
    float held = m.peak_db;
    digital_motion_step(&m, -60, 1.0f);
    CHECK_NEAR(m.level_db, -20, 0.01);
    CHECK_NEAR(m.peak_db, held, 0.001);
    digital_motion_step(&m, -60, 0.6f);
    CHECK_NEAR(m.peak_db, held - 2, 0.01);
    CHECK(m.peak_db >= m.level_db);
    float old = m.level_db;
    digital_motion_step(&m, NAN, 1); CHECK(m.level_db == old);
    digital_motion_step(&m, 3, -1); CHECK(m.level_db == old);
}

static int wav_yields;
static void wav_yield_probe(void) { wav_yields++; }

static void test_lrc_wav(void)
{
    static lrc_document_t d;
    CHECK(lrc_parse(&d,"[ar:test]\n[00:04.123][00:01.2]hello\r\n[offset:+200]\n[00:02.00]world"));
    CHECK(d.synced && d.count==3);
    CHECK(d.lines[0].time_ms==1000 && d.lines[1].time_ms==1800 && d.lines[2].time_ms==3923);
    CHECK(lrc_current_line(&d,999)==-1 && lrc_current_line(&d,1000)==0);
    CHECK(strcmp(lrc_line_text(&d,1),"world")==0);
    CHECK(lrc_parse(&d,"one\ntwo") && !d.synced && d.count==2);
    CHECK(!lrc_parse(&d,"[00:99]bad") && d.count==0);
    int16_t input[480];uint8_t output[364];
    for(int i=0;i<480;i++)input[i]=1000;
    CHECK(wav_size(480,48000)==364);
    CHECK(!wav_encode_16k(output,363,input,480,48000));
    CHECK(wav_encode_16k(output,sizeof(output),input,480,48000));
    wav_yields=0;
    CHECK(wav_encode_16k_cooperative(output,sizeof(output),input,480,48000,wav_yield_probe));
    CHECK(wav_yields==1);
    CHECK(memcmp(output,"RIFF",4)==0 && memcmp(output+8,"WAVE",4)==0);
    CHECK((int16_t)(output[44]|output[45]<<8)==1000);
    static int16_t wave[4800]; static uint8_t encoded[3244];
    double energy[2]={0};
    for(int tone=0;tone<2;tone++) {
        make_sine(wave,4800,tone?12000:1000,0.5,0);
        CHECK(wav_encode_16k(encoded,sizeof(encoded),wave,4800,48000));
        for(int i=24;i<1576;i++){double v=(int16_t)(encoded[44+2*i]|encoded[45+2*i]<<8);energy[tone]+=v*v;}
    }
    CHECK(energy[1] < energy[0]*0.0001);
    CHECK(lrc_parse(&d,"[offset:-100]\n[00:01]안녕하세요\n[00:01]다음 줄"));
    CHECK(d.lines[0].time_ms==1100 && strcmp(lrc_line_text(&d,0),"안녕하세요")==0);
    const char *invalid[]={"[0:","[00:","[00:0", "[00:01.","[99999999:00]bad"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)CHECK(!lrc_parse(&d,invalid[i]));
    CHECK(wav_size(480,0)==0);
    CHECK(wav_encode_16k(output,sizeof(output),input,441,44100));
}

static void test_recognition_policy(void)
{
    uint8_t rgb[]={0xf8,0x00,0x07,0xe0,0x00,0x1f,0xaa};
    image_rgb565_native(rgb,sizeof(rgb));
    CHECK(rgb[0]==0 && rgb[1]==0xf8 && rgb[2]==0xe0 && rgb[3]==7 && rgb[4]==0x1f && rgb[5]==0 && rgb[6]==0xaa);
    recog_retry_t retry;
    recog_retry_begin(&retry,1000);
    CHECK(recog_retry_due(&retry,1000));
    CHECK(!recog_retry_expired(&retry,30999));
    CHECK(recog_retry_finish(&retry,2000,false,false));
    CHECK(!recog_retry_due(&retry,9999) && recog_retry_due(&retry,10000));
    CHECK(recog_retry_finish(&retry,11000,false,false));
    CHECK(!recog_retry_finish(&retry,20000,false,false) && !retry.active);
    recog_retry_begin(&retry,30000);CHECK(!recog_retry_finish(&retry,30001,true,false));
    recog_retry_begin(&retry,40000);CHECK(!recog_retry_finish(&retry,40001,false,true));
    recog_retry_begin(&retry,0);CHECK(recog_retry_expired(&retry,30000));
    int16_t pcm[]={4000,-2000,4000,-2000};
    CHECK_NEAR(pcm_prepare_recognition(pcm,4),8,0.001);
    CHECK(pcm[0]==24000 && pcm[1]==-24000);
    int16_t dc[]={3000,3000,3000};pcm_prepare_recognition(dc,3);CHECK(dc[0]==0 && dc[2]==0);
    int16_t hot[]={32767,-32768};pcm_prepare_recognition(hot,2);CHECK(hot[0]<=28000 && hot[1]>=-28000);
    static lrc_document_t doc;
    CHECK(lrc_parse(&doc,"[00:10]last line\n[00:13.5]"));CHECK(lrc_end_ms(&doc,30000)==13500);
    CHECK(lrc_parse(&doc,"[00:10]last line"));CHECK(lrc_end_ms(&doc,30000)==15000);
    CHECK(lrc_end_ms(&doc,12000)==12000);
    CHECK(lrc_parse(&doc,"plain lyrics"));CHECK(lrc_end_ms(&doc,20000)==20000);
}

int main(void)
{
    test_level_meter();
    test_audio_ring();
    test_vu_motion();
    test_digital_motion();
    test_lrc_wav();
    test_recognition_policy();
    test_display_scan();
    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    printf("All host tests passed\n");
    return EXIT_SUCCESS;
}
