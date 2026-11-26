#include "ui_font.h"
#include "esp_log.h"
extern const uint8_t font_start[] asm("_binary_Pretendard_Regular_ttf_start");
extern const uint8_t font_end[] asm("_binary_Pretendard_Regular_ttf_end");
static lv_font_t *fonts[4];
bool ui_font_init(void)
{
    const int sizes[] = {14, 24, 32, 48};
    const lv_font_t *symbols[] = {&lv_font_montserrat_14, &lv_font_montserrat_24,
                                  &lv_font_montserrat_32, &lv_font_montserrat_48};
    for (int i = 0; i < 4; i++) {
        fonts[i] = lv_tiny_ttf_create_data(font_start, font_end - font_start, sizes[i]);
        if (!fonts[i]) { ESP_LOGE("ui_font", "Unable to allocate Pretendard font %d", sizes[i]); return false; }
        fonts[i]->fallback = symbols[i];
    }
    ESP_LOGI("ui_font", "Pretendard Regular ready: 14/24/32/48px, %u bytes",
             (unsigned)(font_end - font_start));
    return true;
}
const lv_font_t *ui_font(int size)
{
    int i = size <= 14 ? 0 : (size <= 24 ? 1 : (size <= 32 ? 2 : 3));
    return fonts[i] ? fonts[i] : &lv_font_montserrat_24;
}
