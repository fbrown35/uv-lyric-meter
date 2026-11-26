// Board bring-up for GUITION JC8012P4A1. Sequence follows the vendor LVGL 9 demo BSP:
// LDO for D-PHY -> DSI bus -> DBI IO -> JD9365 panel -> DPI, then GSL3680 touch on I2C.
#include "board.h"
#include "display_scan.h"
#include "esp_heap_caps.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_idf_version.h"
#include "esp_lcd_jd9365.h"
#include "jc8012_panel_init.h"

#if CONFIG_BOARD_PANEL_INIT_DEMO
#define PANEL_INIT_CMDS jc8012_panel_init_cmds_demo
#else
#define PANEL_INIT_CMDS jc8012_panel_init_cmds_factory
#endif
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gsl3680.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "sdkconfig.h"
#if CONFIG_BOARD_DISPLAY_FIFO_PROBE
#include "soc/mipi_dsi_bridge_struct.h"
#endif

static const char *TAG = "board";

#define BL_LEDC_TIMER   LEDC_TIMER_1
#define BL_LEDC_CHANNEL LEDC_CHANNEL_1
#define BL_LEDC_RES     LEDC_TIMER_10_BIT
#define BL_LEDC_MAX     ((1 << 10) - 1)

#define DSI_LANES 2

#ifdef CONFIG_BOARD_PANEL_FLIP_ROWS
#define PANEL_FLIP_ROWS true
#else
#define PANEL_FLIP_ROWS false
#endif
#ifdef CONFIG_BOARD_PANEL_FLIP_COLS
#define PANEL_FLIP_COLS true
#else
#define PANEL_FLIP_COLS false
#endif

#ifdef CONFIG_BOARD_TOUCH_MIRROR_X
#define TOUCH_MIRROR_X 1
#else
#define TOUCH_MIRROR_X 0
#endif
#ifdef CONFIG_BOARD_TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y 1
#else
#define TOUCH_MIRROR_Y 0
#endif

static i2c_master_bus_handle_t s_i2c_bus;

esp_err_t board_i2c_init(i2c_master_bus_handle_t *out_bus)
{
    if (!s_i2c_bus) {
        const i2c_master_bus_config_t cfg = {
            .i2c_port = CONFIG_BOARD_I2C_PORT,
            .sda_io_num = CONFIG_BOARD_I2C_SDA,
            .scl_io_num = CONFIG_BOARD_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = 1,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_i2c_bus), TAG, "i2c bus");
    }
    if (out_bus) {
        *out_bus = s_i2c_bus;
    }
    return ESP_OK;
}

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = CONFIG_BOARD_BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "bl timer");
    const ledc_channel_config_t ch = {
        .gpio_num = CONFIG_BOARD_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
    };
    return ledc_channel_config(&ch);
}

esp_err_t board_backlight_set(int percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, (BL_LEDC_MAX * percent) / 100),
                        TAG, "bl duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

static esp_err_t dphy_power_on(void)
{
#if CONFIG_BOARD_DSI_PHY_LDO_CHAN > 0
    static esp_ldo_channel_handle_t phy_ldo;
    if (phy_ldo) {
        return ESP_OK;
    }
    const esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = CONFIG_BOARD_DSI_PHY_LDO_CHAN,
        .voltage_mv = CONFIG_BOARD_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo), TAG, "dphy ldo");
#endif
    return ESP_OK;
}

static esp_err_t panel_init(int lane_mbps, bool clk_force_hs, esp_lcd_dsi_bus_handle_t *out_bus,
                            esp_lcd_panel_io_handle_t *out_io, esp_lcd_panel_handle_t *out_panel)
{
    ESP_RETURN_ON_ERROR(dphy_power_on(), TAG, "dphy power");

    esp_lcd_dsi_bus_handle_t dsi_bus;
    const esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = DSI_LANES,
        // 0 = let IDF pick the PLL ref clock for the silicon revision. The legacy
        // MIPI_DSI_PHY_CLK_SRC_DEFAULT (PLL_F20M) aborts on ESP32-P4 v3.x.
        .phy_clk_src = 0,
        .lane_bit_rate_mbps = lane_mbps,
        .flags.clock_lane_force_hs = clk_force_hs,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), TAG, "dsi bus");
    if (out_bus) {
        *out_bus = dsi_bus;
    }

    const esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, out_io), TAG, "dbi io");

    // Timings from the vendor demo. RGB565 halves PSRAM bandwidth versus RGB888.
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = CONFIG_BOARD_DPI_CLOCK_MHZ,
#if CONFIG_BOARD_DISPLAY_RAW_RGB888
        .in_color_format = LCD_COLOR_FMT_RGB888,
#else
        .in_color_format = LCD_COLOR_FMT_RGB565,
#endif
#if CONFIG_BOARD_DSI_OUTPUT_RGB888
        .out_color_format = LCD_COLOR_FMT_RGB888,
#endif
        .num_fbs = 1,
        .video_timing = {
            .h_size = BOARD_LCD_NATIVE_W,
            .v_size = BOARD_LCD_NATIVE_H,
            .hsync_back_porch = 20,
            .hsync_pulse_width = 20,
            .hsync_front_porch = CONFIG_BOARD_DPI_H_FRONT_PORCH,
            .vsync_back_porch = CONFIG_BOARD_DPI_V_BACK_PORCH,
            .vsync_pulse_width = 4,
            .vsync_front_porch = CONFIG_BOARD_DPI_V_FRONT_PORCH,
        },
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        .flags.use_dma2d = true,
#endif
    };
    ESP_LOGI(TAG, "DPI timing: clock=%dMHz H=20/20/%d V=4/%d/%d lane=%dMbps",
             CONFIG_BOARD_DPI_CLOCK_MHZ, CONFIG_BOARD_DPI_H_FRONT_PORCH,
             CONFIG_BOARD_DPI_V_BACK_PORCH, CONFIG_BOARD_DPI_V_FRONT_PORCH,
             lane_mbps);
    jd9365_vendor_config_t vendor_cfg = {
        .init_cmds = PANEL_INIT_CMDS,
        .init_cmds_size = sizeof(PANEL_INIT_CMDS) / sizeof(PANEL_INIT_CMDS[0]),
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num = DSI_LANES,
        },
    };
    const esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = CONFIG_BOARD_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#if CONFIG_BOARD_DISPLAY_RAW_RGB888 || CONFIG_BOARD_DSI_OUTPUT_RGB888
        .bits_per_pixel = 24,
#else
        .bits_per_pixel = 16,
#endif
        .vendor_config = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(*out_io, &dev_cfg, out_panel), TAG, "jd9365");
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    // IDF 6 moved DMA2D from a config flag to an explicit call (esp_lcd_jd9365 2.0.2 changelog).
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(*out_panel), TAG, "dma2d");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "panel init");
    // esp_lcd_jd9365 maps mirror_x -> GS (gate/rows) and mirror_y -> SS (source/columns).
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(*out_panel, PANEL_FLIP_ROWS, PANEL_FLIP_COLS), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*out_panel, true), TAG, "panel on");
    return ESP_OK;
}

static esp_err_t touch_init(esp_lcd_touch_handle_t *out_tp)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(board_i2c_init(&bus), TAG, "i2c");

    // Coordinates are reported in panel-native (portrait) space; LVGL applies display rotation.
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_NATIVE_W,
        .y_max = BOARD_LCD_NATIVE_H,
        .rst_gpio_num = CONFIG_BOARD_TOUCH_RST,
        .int_gpio_num = CONFIG_BOARD_TOUCH_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {
            .swap_xy = 0,
            .mirror_x = TOUCH_MIRROR_X,
            .mirror_y = TOUCH_MIRROR_Y,
        },
    };
    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
    io_cfg.scl_speed_hz = CONFIG_BOARD_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &tp_io), TAG, "touch io");
    return esp_lcd_touch_new_i2c_gsl3680(tp_io, &tp_cfg, out_tp);
}

#if CONFIG_BOARD_DISPLAY_SELFTEST
// Bring-up diagnostic: try several DSI link settings, each showing the DSI host's internal
// colour-bar pattern (no framebuffer/PSRAM involved) for 6 s, dark for 1 s in between.
static void display_link_sweep(void)
{
    static const struct { int mbps; bool force_hs; } cases[] = {
        // Forced-HS clock lane was tried too and hung in esp_lcd_new_dsi_bus (lanes never stop).
        {1500, false}, {1000, false},
    };
    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        esp_lcd_dsi_bus_handle_t bus = NULL;
        esp_lcd_panel_io_handle_t io = NULL;
        esp_lcd_panel_handle_t panel = NULL;
        ESP_LOGW(TAG, "SWEEP %d/2: %d Mbps, clock lane %s", i + 1, cases[i].mbps,
                 cases[i].force_hs ? "forced HS" : "auto");
        esp_err_t err = panel_init(cases[i].mbps, cases[i].force_hs, &bus, &io, &panel);
        if (err == ESP_OK) {
            esp_lcd_dpi_panel_set_pattern(panel, MIPI_DSI_PATTERN_BAR_HORIZONTAL);
            board_backlight_set(100);
            vTaskDelay(pdMS_TO_TICKS(6000));
            board_backlight_set(0);
        } else {
            ESP_LOGE(TAG, "SWEEP %d init failed: %s", i + 1, esp_err_to_name(err));
        }
        if (panel) {
            esp_lcd_panel_del(panel);
        }
        if (io) {
            esp_lcd_panel_io_del(io);
        }
        if (bus) {
            esp_lcd_del_dsi_bus(bus);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Framebuffer path check: 4 RGB565 bands written straight into the DPI framebuffer in PSRAM.
static void display_selftest(esp_lcd_panel_handle_t panel)
{
    esp_err_t err;
    board_backlight_set(100);
    ESP_LOGW(TAG, "SELFTEST: framebuffer bands red/green/blue/white (5 s)");
    void *fb = NULL;
    err = esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb);
    if (err == ESP_OK && fb) {
        uint16_t *px = fb;
        const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
        const int band = BOARD_LCD_NATIVE_H / 4;
        for (int y = 0; y < BOARD_LCD_NATIVE_H; y++) {
            uint16_t c = colors[y / band];
            for (int x = 0; x < BOARD_LCD_NATIVE_W; x++) {
                px[y * BOARD_LCD_NATIVE_W + x] = c;
            }
        }
        size_t len = (size_t)BOARD_LCD_NATIVE_W * BOARD_LCD_NATIVE_H * 2;
        err = esp_cache_msync(fb, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        ESP_LOGI(TAG, "framebuffer %p filled, cache sync %s", fb, esp_err_to_name(err));
    } else {
        ESP_LOGE(TAG, "get_frame_buffer failed: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
    board_backlight_set(0);
    ESP_LOGW(TAG, "SELFTEST done, starting LVGL");
}
#endif

#if CONFIG_BOARD_PANEL_SCAN_OFFSET > 0
static uint16_t *s_scan_framebuffer;
static uint8_t *s_scan_rotation_buffer;

// Board-specific synchronous transport. A single flush-ready notification follows
// both wrapped copies, so LVGL cannot reuse a source buffer between the two spans.
static void display_scan_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    lv_area_t native = *area;
    int w = lv_area_get_width(area), h = lv_area_get_height(area);
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    lv_color_format_t format = lv_display_get_color_format(disp);
    uint32_t stride = lv_draw_buf_width_to_stride(w, format);
    if (rotation != LV_DISPLAY_ROTATION_0) {
        int output_width = (rotation == LV_DISPLAY_ROTATION_180) ? w : h;
        uint32_t output_stride = lv_draw_buf_width_to_stride(output_width, format);
        lv_draw_sw_rotate(pixels, s_scan_rotation_buffer, w, h, stride, output_stride, rotation, format);
        pixels = s_scan_rotation_buffer;
        stride = output_stride;
        lv_display_rotate_area(disp, &native);
    }
    bool copied = display_scan_copy(s_scan_framebuffer, BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H,
                                   CONFIG_BOARD_PANEL_SCAN_OFFSET,
                                   native.x1, native.y1, native.x2, native.y2,
                                   (const uint16_t *)pixels, stride / sizeof(uint16_t));
    if (copied) {
        int first = (native.y1 + CONFIG_BOARD_PANEL_SCAN_OFFSET) % BOARD_LCD_NATIVE_H;
        int rows = lv_area_get_height(&native);
        int tail = rows < BOARD_LCD_NATIVE_H - first ? rows : BOARD_LCD_NATIVE_H - first;
        esp_err_t err = esp_cache_msync(s_scan_framebuffer + first * BOARD_LCD_NATIVE_W,
                                       tail * BOARD_LCD_NATIVE_W * sizeof(uint16_t),
                                       ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        if (err == ESP_OK && rows > tail) {
            err = esp_cache_msync(s_scan_framebuffer, (rows - tail) * BOARD_LCD_NATIVE_W * sizeof(uint16_t),
                                 ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        }
        if (err != ESP_OK) ESP_LOGE(TAG, "scan cache sync: %s", esp_err_to_name(err));
    } else {
        ESP_LOGE(TAG, "Rejected scan rectangle (%d,%d)-(%d,%d)",
                 (int)native.x1, (int)native.y1, (int)native.x2, (int)native.y2);
    }
    lv_display_flush_ready(disp);
}
#endif

#if CONFIG_BOARD_DISPLAY_RAW_SCAN_TEST
static esp_err_t display_raw_scan_test(esp_lcd_panel_handle_t panel)
{
#if CONFIG_BOARD_DISPLAY_HOST_PATTERN
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_set_pattern(panel, MIPI_DSI_PATTERN_BAR_HORIZONTAL),
                        TAG, "DSI host pattern");
    ESP_LOGW(TAG, "HOST PATTERN: hardware bars; no framebuffer pixel input or software rotation");
    return ESP_OK;
#endif
    void *buffer = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &buffer), TAG, "raw framebuffer");
    uint16_t *pixels = buffer;
    const uint16_t colors[8] = {0xF800, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xFFFF, 0x8410};
    // Match the existing 270-degree LVGL mapping without calling LVGL or draw_bitmap.
    for (int y = 0; y < BOARD_LCD_NATIVE_H; y++) {
        for (int x = 0; x < BOARD_LCD_NATIVE_W; x++) {
            uint16_t color = colors[y / (BOARD_LCD_NATIVE_H / 8)];
            if (y < 64 && x >= BOARD_LCD_NATIVE_W - 64) color = 0xFFFF;
            if (y >= BOARD_LCD_NATIVE_H - 64 && x < 64) color = 0;
            int row = (y + CONFIG_BOARD_PANEL_SCAN_OFFSET) % BOARD_LCD_NATIVE_H;
#if CONFIG_BOARD_DISPLAY_RAW_RGB888
            uint8_t *p = (uint8_t *)buffer + ((size_t)row * BOARD_LCD_NATIVE_W + x) * 3;
            p[0] = (uint8_t)((color & 31) * 255 / 31);
            p[1] = (uint8_t)(((color >> 5) & 63) * 255 / 63);
            p[2] = (uint8_t)(((color >> 11) & 31) * 255 / 31);
#else
            pixels[row * BOARD_LCD_NATIVE_W + x] = color;
#endif
        }
    }
    size_t bytes_per_pixel = 2;
#if CONFIG_BOARD_DISPLAY_RAW_RGB888
    bytes_per_pixel = 3;
    ESP_LOGW(TAG, "RAW SCAN TEST: RGB888 framebuffer and panel format");
    (void)pixels;
#endif
    ESP_RETURN_ON_ERROR(esp_cache_msync(buffer, BOARD_LCD_NATIVE_W * BOARD_LCD_NATIVE_H * bytes_per_pixel,
                                      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED), TAG, "raw cache sync");
    ESP_LOGW(TAG, "RAW SCAN TEST: direct framebuffer; red/yellow/green/cyan/blue/magenta/white/gray");
    ESP_LOGW(TAG, "RAW SCAN TEST: white square top-left, black square bottom-right; no LVGL display registered");
#if CONFIG_BOARD_DISPLAY_FIFO_PROBE
    // Rev3 register definitions document a 30-bit fallback pixel and an early-line
    // underrun interrupt mask. All-one fallback data is white in every RGB format.
    ESP_LOGW(TAG, "FIFO PROBE: fallback before=%08lx discard_before=%lu",
             (unsigned long)MIPI_DSI_BRIDGE.dpi_rsv_dpi_data.dpi_rsv_data,
             (unsigned long)MIPI_DSI_BRIDGE.dpi_misc_config.fifo_underrun_discard_vcnt);
    MIPI_DSI_BRIDGE.dpi_rsv_dpi_data.dpi_rsv_data = 0x3fffffff;
    MIPI_DSI_BRIDGE.dpi_misc_config.fifo_underrun_discard_vcnt = 0;
    MIPI_DSI_BRIDGE.dpi_config_update.dpi_config_update = 1;
    size_t unexpected_black = 0;
    for (int y = 0; y < BOARD_LCD_NATIVE_H; y++) {
        int row = (y + CONFIG_BOARD_PANEL_SCAN_OFFSET) % BOARD_LCD_NATIVE_H;
        for (int x = 0; x < BOARD_LCD_NATIVE_W; x++) {
            if (y >= BOARD_LCD_NATIVE_H - 64 && x < 64) continue;
            if (pixels[row * BOARD_LCD_NATIVE_W + x] == 0) unexpected_black++;
        }
    }
    ESP_LOGW(TAG, "FIFO PROBE: white fallback enabled; unexpected black FB pixels=%u",
             (unsigned)unexpected_black);
#endif
    return ESP_OK;
}
#endif

esp_err_t board_display_start(board_display_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "out");
    *out = (board_display_t){0};

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_affinity = 0;         // SPEC 12.1: lvgl_task on core 0
    port_cfg.task_stack = 8 * 1024;
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    board_backlight_set(0);

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
#if CONFIG_BOARD_DISPLAY_SELFTEST
    display_link_sweep();
#endif
    ESP_RETURN_ON_ERROR(panel_init(CONFIG_BOARD_DSI_LANE_MBPS, false, NULL, &io, &panel), TAG, "panel");
#if CONFIG_BOARD_DISPLAY_SELFTEST
    display_selftest(panel);
#endif

#if CONFIG_BOARD_DISPLAY_RAW_SCAN_TEST
    // Return no LVGL display so the application cannot overwrite the diagnostic.
    return display_raw_scan_test(panel);
#endif

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_NATIVE_W * CONFIG_BOARD_LVGL_BUF_LINES,
        .double_buffer = true,
        .hres = BOARD_LCD_NATIVE_W,
        .vres = BOARD_LCD_NATIVE_H,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {.swap_xy = false, .mirror_x = false, .mirror_y = false},
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,   // DPI panels have no HW rotation; see SPEC 4 (PPA comparison pending)
        },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {.flags.avoid_tearing = false};
    out->disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    ESP_RETURN_ON_FALSE(out->disp, ESP_FAIL, TAG, "add disp");

#if CONFIG_BOARD_PANEL_SCAN_OFFSET > 0
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, (void **)&s_scan_framebuffer), TAG, "scan framebuffer");
    // Allow for row padding after rotation of a narrow partial rectangle.
    size_t rotation_bytes = disp_cfg.buffer_size * sizeof(uint16_t) + BOARD_UI_W * LV_DRAW_BUF_STRIDE_ALIGN;
    s_scan_rotation_buffer = heap_caps_malloc(rotation_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_scan_rotation_buffer, ESP_ERR_NO_MEM, TAG, "scan rotation buffer");
    ESP_LOGW(TAG, "Software scan-origin calibration: %d native rows; synchronous CPU copy",
             CONFIG_BOARD_PANEL_SCAN_OFFSET);
#endif

    lvgl_port_lock(0);
#if CONFIG_BOARD_PANEL_SCAN_OFFSET > 0
    lv_display_set_flush_cb(out->disp, display_scan_flush);
#endif
#if CONFIG_BOARD_ROTATION_0
    // Diagnostic: no rotation, UI appears in portrait.
#elif CONFIG_BOARD_ROTATION_90
    lv_display_set_rotation(out->disp, LV_DISPLAY_ROTATION_90);
#else
    lv_display_set_rotation(out->disp, LV_DISPLAY_ROTATION_270);
#endif
    lvgl_port_unlock();

    esp_lcd_touch_handle_t tp = NULL;
    esp_err_t err = touch_init(&tp);
    if (err == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = {.disp = out->disp, .handle = tp};
        out->touch = lvgl_port_add_touch(&touch_cfg);
    } else {
        ESP_LOGE(TAG, "Touch init failed (%s); continuing without touch", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Display %dx%d (native %dx%d), touch %s", BOARD_UI_W, BOARD_UI_H,
             BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H, out->touch ? "ok" : "missing");
    return ESP_OK;
}
