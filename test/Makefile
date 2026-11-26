# Host unit tests for pure-C modules (no ESP-IDF needed).
CC ?= cc
CFLAGS += -std=c11 -O2 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I../components/level_meter/include -I../components/audio_in/include -I../components/board/include -I../components/lyrics/include -I../components/recognizer/include -I../components/music_service/include
SRCS = test_main.c ../components/music_service/image_pixels.c ../components/lyrics/lrc.c ../components/recognizer/wav.c ../components/recognizer/pcm_prepare.c ../components/recognizer/recog_retry.c ../components/board/display_scan.c ../components/level_meter/level_meter.c ../components/level_meter/vu_motion.c ../components/level_meter/digital_motion.c ../components/audio_in/audio_ring.c

run: host_tests
	./host_tests

host_tests: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm

clean:
	rm -f host_tests

.PHONY: run clean
