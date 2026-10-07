#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Compile the production formatter and state helpers, including static ones. */
#include "../../App/bus_scope.c"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#if __GNUC__ >= 15
/* Vendor glyph IDs contain exactly two bytes, without a string terminator. */
#pragma GCC diagnostic ignored "-Wunterminated-string-initialization"
#endif
#include "../../User/lcdfont.h"
#pragma GCC diagnostic pop

static uint32_t test_tick;
static BusScopeOutputStatus output_status;
static uint32_t output_apply_count;
static uint8_t output_defer;
static uint8_t output_change_after_snapshot;
static uint32_t output_snapshot_count;
uint8_t BusScope_OutputInit(void) { return 1U; }
uint8_t BusScope_OutputApply(const BusScopeOutputConfig *config)
{
    assert(BusScope_OutputValidate(config));
    output_apply_count++;
    output_status.requested = *config;
    output_status.pending = output_defer && config->enabled;
    if (!output_status.pending)
    {
        output_status.applied = *config;
        memset(&output_status.timing, 0, sizeof(output_status.timing));
        if (config->enabled) assert(BusScope_OutputTiming(240000000U, config->frequency_hz,
            config->duty_permille, &output_status.timing));
    }
    output_status.revision++;
    return output_status.ready;
}
void BusScope_OutputSnapshot(BusScopeOutputStatus *status)
{
    *status = output_status;
    output_snapshot_count++;
    if (output_change_after_snapshot)
    {
        output_status.ready = 0U;
        output_status.applied.enabled = 0U;
        output_status.timing.actual_millihz = 0U;
    }
}
uint32_t test_critical_depth;
static TestSCB test_scb;
TestSCB *SCB = &test_scb;
static jmp_buf sample_exit;
static uint32_t sample_iterations;
static uint32_t sample_limit;
static uint32_t sample_gap_at;
static uint8_t sample_wave;
static uint8_t sample_consume;
static uint8_t consumed_count;
static BusScopeCaptureFrame consumed_frames[16];
static BusScopeCaptureFrame test_frame;
static uint8_t test_envelope;
static BusScopeAcquisitionStatus acquisition_status;
static uint16_t acquisition_key;
static uint32_t acquisition_starts;
static jmp_buf key_exit;
static uint8_t key_test_active;
static uint32_t key_iterations;
static uint16_t lcd_pixels[LCD_H][LCD_W];
static const char *preview_prefix;
typedef struct
{
    uint16_t x, y, width, height;
    char text[64];
} LcdText;
static LcdText lcd_text[32];
static size_t lcd_text_count;
static size_t lcd_static_pixel_writes;
static size_t lcd_static_fill_count;
static size_t lcd_axis_text_count;
static size_t lcd_plot_pixel_writes;
static size_t lcd_plot_fill_count;
static uint16_t lcd_reference_pixels[LCD_H][LCD_W];
static uint8_t lcd_render_reference;
static uint8_t lcd_audit_plot_writes;

static void record_lcd_write(uint16_t x, uint16_t y, uint16_t color)
{
    if (lcd_render_reference != 0U)
    {
        lcd_reference_pixels[y][x] = color;
        return;
    }
    if (x >= SCOPE_X && x < SCOPE_X + SCOPE_W &&
        y >= SCOPE_Y && y < SCOPE_Y + SCOPE_H)
    {
        lcd_plot_pixel_writes++;
        if (lcd_audit_plot_writes != 0U)
        {
            /* Reject transient erases and writes to any unchanged pixel. */
            assert(color == lcd_reference_pixels[y][x]);
            assert(lcd_pixels[y][x] != color);
        }
    }
    if (y >= 50U && (x < SCOPE_X || x >= SCOPE_X + SCOPE_W ||
                     y < SCOPE_Y || y >= SCOPE_Y + SCOPE_H))
    {
        lcd_static_pixel_writes++;
    }
    lcd_pixels[y][x] = color;
}

static void reset_lcd_capture(void)
{
    lcd_text_count = 0U;
    lcd_static_pixel_writes = 0U;
    lcd_static_fill_count = 0U;
    lcd_axis_text_count = 0U;
    lcd_plot_pixel_writes = 0U;
    lcd_plot_fill_count = 0U;
}

void LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    assert(x1 < x2 && x2 <= LCD_W && y1 < y2 && y2 <= LCD_H);
    if (lcd_render_reference == 0U &&
        x1 < SCOPE_X + SCOPE_W && x2 > SCOPE_X &&
        y1 < SCOPE_Y + SCOPE_H && y2 > SCOPE_Y)
    {
        lcd_plot_fill_count++;
    }
    if (lcd_render_reference == 0U && y2 > 50U &&
        (x1 < SCOPE_X || x2 > SCOPE_X + SCOPE_W ||
                      y1 < SCOPE_Y || y2 > SCOPE_Y + SCOPE_H))
    {
        lcd_static_fill_count++;
    }
    for (uint16_t y = y1; y < y2; y++)
    {
        for (uint16_t x = x1; x < x2; x++)
        {
            record_lcd_write(x, y, color);
        }
    }
}

void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    assert(x < LCD_W && y < LCD_H);
    if (color == GREEN)
    {
        assert(x >= SCOPE_X && x < SCOPE_X + SCOPE_W);
        assert(y >= SCOPE_Y && y < SCOPE_Y + SCOPE_H);
    }
    record_lcd_write(x, y, color);
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int dx = (int)x2 - x1, dy = (int)y2 - y1;
    int ix = (dx > 0) - (dx < 0), iy = (dy > 0) - (dy < 0);
    int x = x1, y = y1, xe = 0, ye = 0, distance;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    distance = (dx > dy) ? dx : dy;
    /* Match the physical driver, including its omitted final line pixel. */
    for (int step = 0; step <= distance; step++)
    {
        LCD_DrawPoint((uint16_t)x, (uint16_t)y, color);
        xe += dx;
        ye += dy;
        if (xe > distance) { xe -= distance; x += ix; }
        if (ye > distance) { ye -= distance; y += iy; }
    }
}

void LCD_ShowString(uint16_t x, uint16_t y, const uint8_t *text,
                    uint16_t foreground, uint16_t background, uint8_t size, uint8_t mode)
{
    size_t length = strlen((const char *)text);
    LcdText *record;
    uint16_t width = size / 2U;
    assert((size == 12U || size == 16U) && mode == 0U);
    assert(lcd_text_count < sizeof(lcd_text) / sizeof(lcd_text[0]));
    assert(length < sizeof(lcd_text[0].text));
    assert(x + length * width <= LCD_W && y + size <= LCD_H);
    if (y >= 50U) lcd_axis_text_count++;
    record = &lcd_text[lcd_text_count++];
    *record = (LcdText){x, y, (uint16_t)(length * width), size, {0}};
    memcpy(record->text, text, length + 1U);
    for (size_t i = 0U; i < length; i++)
    {
        assert(text[i] >= ' ' && text[i] <= '~');
        for (uint16_t row = 0U; row < size; row++)
        {
            uint8_t bits = size == 12U ? ascii_1206[text[i] - ' '][row] : ascii_1608[text[i] - ' '][row];
            for (uint16_t col = 0U; col < width; col++)
            {
                record_lcd_write((uint16_t)(x + i * width + col), (uint16_t)(y + row),
                                 (bits & (1U << col)) ? foreground : background);
            }
        }
    }
}

uint32_t HAL_GetTick(void)
{
    return test_tick;
}

TickType_t xTaskGetTickCount(void)
{
    return test_tick;
}

void osDelay(uint32_t ticks)
{
    if (key_test_active)
    {
        assert(ticks == 20U && test_critical_depth == 0U);
        test_tick += ticks;
        if (++key_iterations == 6U) longjmp(key_exit, 1);
        return;
    }
    assert(!"unexpected startup delay in sampling test");
}

void SCB_InvalidateDCache_by_Addr(uint32_t *address, int32_t size)
{
    (void)address;
    (void)size;
    assert(!"sampling tests run with D-cache disabled");
}

void BusScope_AcquisitionInit(void) { memset(&acquisition_status, 0, sizeof(acquisition_status)); }
uint8_t BusScope_AcquisitionStart(void)
{
    acquisition_starts++;
    acquisition_status.running = 1U;
    return 1U;
}
void BusScope_AcquisitionStop(void) { acquisition_status.running = 0U; }
uint16_t BusScope_AcquisitionKeyRaw(void) { return acquisition_key; }
void BusScope_AcquisitionSnapshot(BusScopeAcquisitionStatus *status) { *status = acquisition_status; }
uint8_t BusScope_AcquisitionReadBlock(uint16_t samples[BUS_SCOPE_ADC_BLOCK_SAMPLES], uint8_t *gap)
{
    assert(test_critical_depth == 0U && acquisition_status.running);
    *gap = sample_iterations == 0U || sample_iterations == sample_gap_at;
    if (sample_iterations == sample_gap_at) acquisition_status.lost_blocks++;
    for (uint32_t i = 0U; i < BUS_SCOPE_ADC_BLOCK_SAMPLES; i++)
    {
        uint32_t index = sample_iterations * BUS_SCOPE_ADC_BLOCK_SAMPLES + i;
        samples[i] = sample_wave ? ((index % 100U) < 50U ? 0U : UINT16_MAX) : (uint16_t)index;
    }
    acquisition_status.blocks++;
    return 1U;
}

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout)
{
    assert(test_critical_depth == 0U && clear == pdTRUE);
    assert(timeout == pdMS_TO_TICKS(10U));
    if (sample_consume != 0U && s_scope_ready)
    {
        assert(consumed_count < sizeof(consumed_frames) / sizeof(consumed_frames[0]));
        consumed_frames[consumed_count] = s_scope_frame;
        consumed_count++;
        s_scope_ready = 0U;
    }
    sample_iterations++;
    if (sample_iterations == sample_limit) longjmp(sample_exit, 1);
    test_tick += 5U;
    return 1U;
}

static void reset_state(void)
{
    memset(s_can, 0, sizeof(s_can));
    memset((void *)s_can_lost_events, 0, sizeof(s_can_lost_events));
    test_tick = 0U;
    test_critical_depth = 0U;
    s_adc_ready = 0U;
    memset(&output_status, 0, sizeof(output_status));
    BusScope_OutputDefault(&output_status.requested);
    output_status.applied = output_status.requested;
    output_status.timing = (BusScopeOutputTiming){2399U, 9999U, 5000U, 10000U};
    output_defer = 0U;
    output_change_after_snapshot = 0U;
    output_snapshot_count = 0U;
    output_apply_count = 0U;
    s_output_field = OUTPUT_FIELD_FREQ;
    s_output_revision = 0U;
    s_scope_dropped = 0U;
    s_scope_ready = 0U;
    s_scope_axes_valid = 0U;
    s_app_ready = 0U;
    memset(&s_scope_frame, 0, sizeof(s_scope_frame));
    memset(&s_scope_display, 0, sizeof(s_scope_display));
    memset(&test_frame, 0, sizeof(test_frame));
    test_frame.period_us = 50U;
    test_envelope = 0U;
    s_scope_display_valid = 0U;
    s_scope_config = (BusScopeCaptureConfig){5U, 1650U, 0U, BUS_SCOPE_CAPTURE_AUTO};
    s_scope_field = SCOPE_FIELD_TIME;
    s_scope_revision = 0U;
    s_scope_applied_revision = UINT32_MAX;
    s_scope_frame_revision = 0U;
    s_scope_display_revision = 0U;
    s_scope_stopped = 0U;
    s_scope_waiting = 1U;
    s_scope_period_us = 50U;
    s_scope_y_scale_pending_x10 = SCOPE_Y_SCALE_X10_INIT;
    s_scope_y_offset_pending_mv = 0;
    memset(s_scope_drawn_lines, 0, sizeof(s_scope_drawn_lines));
    BusScopeCapture_Init(&s_capture);
    memset(&acquisition_status, 0, sizeof(acquisition_status));
    acquisition_status.running = 1U;
    acquisition_key = UINT16_MAX;
    acquisition_starts = 0U;
    key_test_active = 0U;
    key_iterations = 0U;
    test_scb.CCR = 0U;
    sample_iterations = 0U;
    sample_limit = 0U;
    sample_gap_at = UINT32_MAX;
    sample_wave = 0U;
    sample_consume = 0U;
    consumed_count = 0U;
}

static void test_dlc_and_short_frame(void)
{
    const uint8_t lengths[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
                               8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U};
    uint8_t payload[64];
    FDCAN_RxHeaderTypeDef header = {0};

    reset_state();
    for (uint32_t dlc = 0U; dlc < 16U; dlc++)
    {
        assert(fdcan_dlc_to_len(dlc << 16) == lengths[dlc]);
    }
    assert(fdcan_dlc_to_len(UINT32_MAX) == 0U);
    memset(payload, 0xa5, sizeof(payload));
    header.DataLength = FDCAN_DLC_BYTES_64;
    header.Identifier = 0x12345678U;
    header.FDFormat = FDCAN_FD_CAN;
    header.BitRateSwitch = FDCAN_BRS_ON;
    header.IdType = FDCAN_EXTENDED_ID;
    can_store(0U, &header, payload);
    assert(s_can[0].last_len == 64U);
    assert(memcmp(s_can[0].last_data, payload, sizeof(payload)) == 0);

    header.DataLength = FDCAN_DLC_BYTES_2;
    payload[0] = 0x12U;
    payload[1] = 0x34U;
    can_store(0U, &header, payload);
    assert(s_can[0].count == 2U);
    assert(s_can[0].last_len == 2U);
    assert(s_can[0].last_data[0] == 0x12U);
    assert(s_can[0].last_data[1] == 0x34U);
    for (uint32_t i = 2U; i < sizeof(payload); i++)
    {
        assert(s_can[0].last_data[i] == 0U);
    }

    header.DataLength = FDCAN_DLC_BYTES_0;
    can_store(0U, &header, payload);
    assert(s_can[0].last_len == 0U);
    for (uint32_t i = 0U; i < sizeof(payload); i++)
    {
        assert(s_can[0].last_data[i] == 0U);
    }
    can_store(CAN_PORTS, &header, payload);
    assert(s_can[0].count == 3U);

    header.DataLength = FDCAN_DLC_BYTES_64;
    header.FDFormat = FDCAN_CLASSIC_CAN;
    can_store(0U, &header, payload);
    assert(s_can[0].last_len == 8U);
    assert(memcmp(s_can[0].last_data, payload, 8U) == 0);
    for (uint32_t i = 8U; i < sizeof(payload); i++)
    {
        assert(s_can[0].last_data[i] == 0U);
    }
}

static void populate_largest_report(void)
{
    uint8_t payload[64];
    FDCAN_RxHeaderTypeDef header = {0};
    header.DataLength = FDCAN_DLC_BYTES_64;
    header.Identifier = 0x1fffffffU;
    header.FDFormat = FDCAN_FD_CAN;
    header.BitRateSwitch = FDCAN_BRS_ON;
    header.IdType = FDCAN_EXTENDED_ID;
    for (uint32_t i = 0U; i < sizeof(payload); i++) payload[i] = (uint8_t)i;
    for (uint8_t i = 0U; i < CAN_PORTS; i++)
    {
        can_store(i, &header, payload);
        s_can[i].count = UINT32_MAX;
        s_can[i].fps = UINT32_MAX;
        s_can[i].rx_errors = UINT32_MAX;
        s_can_lost_events[i] = UINT32_MAX;
        s_can[i].initialized = 1U;
        s_can[i].bus_off = 1U;
    }
    test_tick = UINT32_MAX;
    s_adc_ready = 1U;
    output_status.ready = 1U;
    s_scope_dropped = UINT32_MAX;
    acquisition_status.lost_blocks = UINT32_MAX;
    acquisition_status.blocks = UINT32_MAX;
    acquisition_status.errors = UINT32_MAX;
    acquisition_status.restarts = UINT32_MAX;
    acquisition_status.last_error = UINT32_MAX;
    s_scope_frame.sequence = UINT32_MAX;
    s_scope_frame.period_us = 2000U;
    s_scope_frame.stats = (BusScopeCaptureStats){3300U, 3300U, 3300U, 3300U, 3300U,
        1000U, UINT32_MAX, 1U, 0U};
}

static void test_full_can_fd_report(void)
{
    char report[USB_REPORT_SIZE];
    char expected_line[320];
    char *line;
    uint16_t length;

    reset_state();
    populate_largest_report();
    length = usb_format_status(report, sizeof(report));
    assert(length != 0U);
    assert(length == strlen(report));
    assert(length < USB_REPORT_SIZE);
    assert(test_critical_depth == 0U);
    line = report;
    for (uint8_t port = 1U; port <= CAN_PORTS; port++)
    {
        size_t used = (size_t)snprintf(expected_line, sizeof(expected_line),
            "CAN%u,4294967295,4294967295,0x1FFFFFFF,64,FD,BRS", (unsigned)port);
        for (uint8_t byte = 0U; byte < 64U; byte++)
        {
            used += (size_t)snprintf(expected_line + used, sizeof(expected_line) - used,
                                     ",%02X", (unsigned)byte);
        }
        used += (size_t)snprintf(expected_line + used, sizeof(expected_line) - used, "\r\n");
        assert(strncmp(line, expected_line, used) == 0);
        line += used;
        used = (size_t)snprintf(expected_line, sizeof(expected_line),
            "STAT%u,4294967295,4294967295,4294967295,1,1,EXT\r\n", (unsigned)port);
        assert(strncmp(line, expected_line, used) == 0);
        line += used;
    }
    assert(strcmp(line, "SYS,1,1,4294967295,4294967295\r\n"
                        "SCOPE,4294967295,100000,2000,0,0,1650,1,0,3300,3300,3300,3300,3300,4294967295,1000,1,0,1\r\n"
                        "ADCSTAT,4294967295,4294967295,4294967295,4294967295,4294967295\r\n"
                        "OUT,PWM,10,500,1,1,10000\r\n"
                        "OUTSTAT,0,10,500,1,2399,9999,5000,0,0\r\n") == 0);

    /* Every insufficient capacity must fail without writing beyond its bound. */
    for (size_t capacity = 0U; capacity <= length; capacity++)
    {
        unsigned char guarded[USB_REPORT_SIZE + 2U];
        memset(guarded, 0xa5, sizeof(guarded));
        assert(usb_format_status((char *)guarded + 1U, capacity) == 0U);
        assert(guarded[0] == 0xa5U);
        for (size_t i = capacity + 1U; i < sizeof(guarded); i++)
        {
            assert(guarded[i] == 0xa5U);
        }
        assert(test_critical_depth == 0U);
    }
    assert(usb_format_status(report, (size_t)length + 1U) == length);
}

static void test_fps_elapsed_and_wrap(void)
{
    reset_state();
    s_can[0].count = 300U;
    s_can[0].last_tick = 100U;
    test_tick = 2600U;
    can_update_fps();
    assert(s_can[0].fps == 120U);
    assert(s_can[0].last_count == 300U);
    assert(s_can[0].last_tick == 2600U);
    assert(s_can[0].active == 1U);

    test_tick = 3000U;
    can_update_fps();
    assert(s_can[0].fps == 120U);
    assert(s_can[0].last_tick == 2600U);
    test_tick = 3600U;
    can_update_fps();
    assert(s_can[0].fps == 0U);
    assert(s_can[0].active == 0U);

    s_can[0].last_count = UINT32_MAX - 9U;
    s_can[0].count = 10U;
    s_can[0].last_tick = UINT32_MAX - 999U;
    test_tick = 1000U;
    can_update_fps();
    assert(s_can[0].fps == 10U);
    assert(s_can[0].last_count == 10U);
    assert(s_can[0].last_tick == 1000U);
    assert(s_can[0].active == 1U);
}

static void run_sample_task(uint32_t iterations)
{
    s_app_ready = 1U;
    s_adc_ready = 1U;
    sample_limit = sample_iterations + iterations;
    if (setjmp(sample_exit) == 0)
    {
        BusScope_SampleTask(NULL);
        assert(!"sampling task unexpectedly returned");
    }
    assert(sample_iterations == sample_limit);
    assert(test_critical_depth == 0U);
}

static void test_sampling_buffer_ownership(void)
{
    reset_state();
    s_scope_config.decimation = 1U;
    /* AUTO publishes every 544 raw samples when no trigger edge exists. */
    run_sample_task(4U);
    assert(s_scope_ready && s_scope_frame.sequence == 3U);
    assert(s_scope_dropped == 2U && acquisition_status.blocks == 4U);
    assert(acquisition_starts == 1U && s_scope_frame.period_us == 10U);
    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++)
    {
        assert(s_scope_frame.samples[i] == 1360U + i);
        assert(s_scope_frame.min[i] == s_scope_frame.samples[i]);
        assert(s_scope_frame.max[i] == s_scope_frame.samples[i]);
    }

    reset_state();
    s_scope_config.decimation = 1U;
    sample_consume = 1U;
    run_sample_task(4U);
    assert(consumed_count == 3U && !s_scope_ready);
    assert(s_scope_dropped == 0U);
    for (uint8_t frame = 0U; frame < consumed_count; frame++)
    {
        for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++)
            assert(consumed_frames[frame].samples[i] == 272U + frame * 544U + i);
    }
}

static void test_sampling_gap_and_tick_wrap(void)
{
    reset_state();
    s_scope_config.decimation = 1U;
    sample_gap_at = 1U;
    run_sample_task(2U);
    assert(!s_scope_ready && acquisition_status.lost_blocks == 1U);
    run_sample_task(1U);
    assert(s_scope_ready && s_scope_dropped == 0U);
    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++)
        assert(s_scope_frame.samples[i] == 784U + i);

    reset_state();
    s_scope_config.decimation = 1U;
    test_tick = UINT32_MAX - 4U;
    run_sample_task(2U);
    assert(s_scope_ready && test_tick == 0U);
    assert(s_scope_dropped == 0U);
    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++)
        assert(s_scope_frame.samples[i] == 272U + i);
}

static const LcdText *find_lcd_text(const char *text, uint16_t y)
{
    for (size_t i = 0U; i < lcd_text_count; i++)
    {
        if (lcd_text[i].y == y && strcmp(lcd_text[i].text, text) == 0) return &lcd_text[i];
    }
    assert(!"expected LCD label missing");
    return NULL;
}

static void assert_label_layout(uint8_t scope)
{
    for (size_t i = 0U; i < lcd_text_count; i++)
    {
        const LcdText *a = &lcd_text[i];
        assert(a->x + a->width <= LCD_W && a->y + a->height <= LCD_H);
        if (scope) assert(a->x + a->width <= SCOPE_X || a->x >= SCOPE_X + SCOPE_W ||
                          a->y + a->height <= SCOPE_Y || a->y >= SCOPE_Y + SCOPE_H);
        for (size_t j = i + 1U; j < lcd_text_count; j++)
        {
            const LcdText *b = &lcd_text[j];
            assert(a->x + a->width <= b->x || b->x + b->width <= a->x ||
                   a->y + a->height <= b->y || b->y + b->height <= a->y);
        }
    }
}

static void assert_lcd_labels_fit(void) { assert_label_layout(1U); }

static void assert_voltage_labels(const char *const expected[5])
{
    for (uint16_t i = 0U; i < 5U; i++)
    {
        const LcdText *label = find_lcd_text(expected[i], (uint16_t)(50U + i * 40U));
        assert(label->x + label->width == 38U);
    }
    assert_lcd_labels_fit();
}

static void bmp_u32(uint8_t *bytes, uint32_t value)
{
    for (uint8_t i = 0U; i < 4U; i++) bytes[i] = (uint8_t)(value >> (i * 8U));
}

static void save_scope_preview(const char *name)
{
    static const uint16_t rgb565[] = {0x0000, 0xf800, 0x07e0, 0xffff,
                                      0x7fff, 0x5458, 0x01cf, 0xffe0};
    uint8_t header[54] = {'B', 'M'};
    char path[512];
    FILE *file;
    if (preview_prefix == NULL) return;
    assert(snprintf(path, sizeof(path), "%s-%s.bmp", preview_prefix, name) < (int)sizeof(path));
    file = fopen(path, "wb");
    assert(file != NULL);
    bmp_u32(header + 2U, 54U + LCD_W * LCD_H * 3U);
    bmp_u32(header + 10U, 54U);
    bmp_u32(header + 14U, 40U);
    bmp_u32(header + 18U, LCD_W);
    bmp_u32(header + 22U, LCD_H);
    header[26] = 1U;
    header[28] = 24U;
    assert(fwrite(header, sizeof(header), 1U, file) == 1U);
    for (int y = LCD_H - 1; y >= 0; y--)
    {
        for (uint16_t x = 0U; x < LCD_W; x++)
        {
            uint16_t color = rgb565[lcd_pixels[y][x]];
            uint8_t bgr[] = {(uint8_t)((color & 31U) * 255U / 31U),
                             (uint8_t)(((color >> 5U) & 63U) * 255U / 63U),
                             (uint8_t)((color >> 11U) * 255U / 31U)};
            assert(fwrite(bgr, sizeof(bgr), 1U, file) == 1U);
        }
    }
    assert(fclose(file) == 0);
}

static void render_reference_plot(uint8_t scale, int16_t offset)
{
    static const uint16_t grid_x[] = {44U, 101U, 159U, 217U, 275U};
    static const uint16_t grid_y[] = {56U, 96U, 136U, 176U, 216U};
    uint16_t zero_y;
    uint16_t previous_y;
    uint16_t previous_x = SCOPE_X;
    uint8_t previous_valid;

    /* Render a fresh reference with the physical driver's line rasterizer. */
    lcd_render_reference = 1U;
    LCD_Fill(SCOPE_X, SCOPE_Y, SCOPE_X + SCOPE_W, SCOPE_Y + SCOPE_H, BLACK);
    for (size_t i = 0U; i < sizeof(grid_x) / sizeof(grid_x[0]); i++)
    {
        LCD_Fill(grid_x[i], SCOPE_Y, grid_x[i] + 1U, SCOPE_Y + SCOPE_H, DARKBLUE);
    }
    for (size_t i = 0U; i < sizeof(grid_y) / sizeof(grid_y[0]); i++)
    {
        LCD_Fill(SCOPE_X, grid_y[i], SCOPE_X + SCOPE_W, grid_y[i] + 1U, DARKBLUE);
    }
    if (scope_mv_to_y(0, scale, offset, &zero_y) != 0U)
    {
        LCD_Fill(SCOPE_X, zero_y, SCOPE_X + SCOPE_W, zero_y + 1U, GRAYBLUE);
    }

    previous_valid = scope_raw_to_y(test_frame.samples[0], scale, offset, &previous_y);
    for (uint16_t i = 1U; i < SCOPE_SAMPLES; i++)
    {
        uint16_t y;
        uint16_t x = (uint16_t)(44U + ((uint32_t)i * 462U + 271U) / 542U);
        uint8_t valid = scope_raw_to_y(test_frame.samples[i], scale, offset, &y);
        if (previous_valid != 0U || valid != 0U || previous_y != y)
        {
            LCD_DrawLine(previous_x, previous_y, x, y, GREEN);
        }
        previous_x = x;
        previous_y = y;
        previous_valid = valid;
    }
    if (previous_valid != 0U) LCD_DrawPoint(previous_x, previous_y, GREEN);
    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++)
    {
        uint16_t low_y, high_y;
        uint16_t x = (uint16_t)(44U + ((uint32_t)i * 462U + 271U) / 542U);
        uint8_t low_valid = scope_raw_to_y(test_frame.min[i], scale, offset, &low_y);
        uint8_t high_valid = scope_raw_to_y(test_frame.max[i], scale, offset, &high_y);
        if (low_valid || high_valid || low_y != high_y) LCD_DrawLine(x, high_y, x, low_y, GREEN);
    }
    lcd_render_reference = 0U;
}

static void publish_scope_frame(uint8_t scale, int16_t offset)
{
    size_t changed_pixels = 0U;
    uint8_t ordinary_frame = (s_scope_axes_valid != 0U &&
                              s_scope_axes_scale_x10 == scale &&
                              s_scope_axes_offset_mv == offset &&
                              s_scope_axes_period_us == test_frame.period_us);
    if (!test_envelope)
    {
        memcpy(test_frame.min, test_frame.samples, sizeof(test_frame.samples));
        memcpy(test_frame.max, test_frame.samples, sizeof(test_frame.samples));
    }
    test_frame.stats = (BusScopeCaptureStats){0U, 3300U, 1650U, 2333U, 3300U, 500U, 1000000U, 1U, 0U};
    render_reference_plot(scale, offset);
    for (uint16_t y = SCOPE_Y; y < SCOPE_Y + SCOPE_H; y++)
    {
        for (uint16_t x = SCOPE_X; x < SCOPE_X + SCOPE_W; x++)
        {
            if (lcd_pixels[y][x] != lcd_reference_pixels[y][x]) changed_pixels++;
        }
    }
    reset_lcd_capture();
    s_scope_config.decimation = (uint16_t)(test_frame.period_us / BUS_SCOPE_ADC_PERIOD_US);
    s_scope_frame = test_frame;
    s_scope_frame_revision = s_scope_revision;
    s_scope_ready = 1U;
    s_scope_y_scale_pending_x10 = scale;
    s_scope_y_offset_pending_mv = offset;
    lcd_audit_plot_writes = ordinary_frame;
    draw_scope();
    lcd_audit_plot_writes = 0U;
    assert(!s_scope_ready && test_critical_depth == 0U);
    assert_lcd_labels_fit();
    if (ordinary_frame != 0U)
    {
        assert(lcd_plot_fill_count == 0U);
        assert(lcd_plot_pixel_writes == changed_pixels);
    }
    for (uint16_t y = SCOPE_Y; y < SCOPE_Y + SCOPE_H; y++)
    {
        for (uint16_t x = SCOPE_X; x < SCOPE_X + SCOPE_W; x++)
        {
            assert(lcd_pixels[y][x] == lcd_reference_pixels[y][x]);
        }
    }
}

static void test_scope_axis_labels(void)
{
    static const struct
    {
        uint8_t scale;
        int16_t offset;
        const char *labels[5];
    } cases[] = {
        {10U, 0, {"+3.30", "+1.65", "0.00", "-1.65", "-3.30"}},
        {10U, 1000, {"+4.30", "+2.65", "+1.00", "-0.65", "-2.30"}},
        {1U, 10000, {"+43.00", "+26.50", "+10.00", "-6.50", "-23.00"}},
        {1U, -10000, {"+23.00", "+6.50", "-10.00", "-26.50", "-43.00"}},
        {80U, 0, {"+0.41", "+0.21", "0.00", "-0.21", "-0.41"}}
    };
    static const uint16_t time_x[] = {44U, 101U, 159U, 217U, 275U};
    static const char *time_text[] = {"0.00", "3.35", "6.75", "10.15", "13.55"};
    reset_state();
    for (size_t c = 0U; c < sizeof(cases) / sizeof(cases[0]); c++)
    {
        reset_lcd_capture();
        s_scope_y_scale_pending_x10 = cases[c].scale;
        s_scope_y_offset_pending_mv = cases[c].offset;
        draw_scope_page_full();
        assert(test_critical_depth == 0U);
        assert_voltage_labels(cases[c].labels);
        assert(find_lcd_text("t(ms)", 225U)->x == 2U);
        for (uint16_t t = 0U; t < 5U; t++)
        {
            const LcdText *label = find_lcd_text(time_text[t], 225U);
            if (t == 0U) assert(label->x == SCOPE_X);
            else if (t == 4U) assert(label->x + label->width == LCD_W);
            else assert(label->x + label->width / 2U == time_x[t]);
            assert(lcd_pixels[66U][time_x[t]] == DARKBLUE);
            assert(lcd_pixels[220U][time_x[t]] == GRAYBLUE);
        }
        for (uint16_t tick = 0U; tick < 5U; tick++)
        {
            assert(lcd_pixels[56U + tick * 40U][40U] == GRAYBLUE);
        }
        if (c == 0U) assert(lcd_pixels[136U][60U] == GRAYBLUE);
        if (c == 1U)
        {
            assert(lcd_pixels[160U][60U] == GRAYBLUE);
            assert(lcd_pixels[136U][60U] == DARKBLUE);
        }
    }
    assert(scope_sample_to_x(0U) == 44U);
    assert(scope_sample_to_x(271U) == 275U);
}

static void test_scope_complete_frame_and_clipping(void)
{
    reset_state();
    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++) test_frame.samples[i] = (i % 2U) ? UINT16_MAX : 0U;
    publish_scope_frame(10U, 0);
    assert(lcd_pixels[56U][275U] == GREEN);

    memset(test_frame.samples, 0, sizeof(test_frame.samples));
    test_frame.samples[250] = UINT16_MAX;
    test_frame.samples[271] = UINT16_MAX;
    publish_scope_frame(10U, 0);
    assert(lcd_pixels[56U][257U] == GREEN);
    assert(lcd_pixels[56U][275U] == GREEN);
    assert(s_scope_display.stats.vpp_mv == 3300U);

    publish_scope_frame(80U, 1000);
    publish_scope_frame(80U, 10000);
    for (uint16_t y = SCOPE_Y; y < SCOPE_Y + SCOPE_H; y++)
    {
        for (uint16_t x = SCOPE_X; x < SCOPE_X + SCOPE_W; x++) assert(lcd_pixels[y][x] != GREEN);
    }
    publish_scope_frame(80U, -10000);
    for (uint16_t y = 0U; y < LCD_H; y++)
    {
        for (uint16_t x = 0U; x < LCD_W; x++) assert(lcd_pixels[y][x] != GREEN);
    }
}

static void assert_scope_axes_untouched(void)
{
    assert(lcd_axis_text_count == 0U);
    assert(lcd_static_fill_count == 0U);
    assert(lcd_static_pixel_writes == 0U);
}

static void assert_scope_axes_repainted(void)
{
    assert(lcd_axis_text_count > 0U);
    assert(lcd_static_fill_count > 0U);
    assert(lcd_static_pixel_writes > 0U);
}

static void test_scope_axis_refresh(void)
{
    static uint16_t expected[LCD_H][LCD_W];
    static uint16_t flat_frame[LCD_H][LCD_W];
    reset_state();
    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++) test_frame.samples[i] = ((i / 25U) % 2U) ? UINT16_MAX : 0U;
    publish_scope_frame(10U, 0);
    assert_scope_axes_repainted();
    memcpy(expected, lcd_pixels, sizeof(expected));
    save_scope_preview("default");

    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    assert(lcd_plot_pixel_writes == 0U);
    assert(memcmp(expected, lcd_pixels, sizeof(expected)) == 0);

    /* Changing the trace must remove old pixels without touching the axes. */
    memset(test_frame.samples, 0, sizeof(test_frame.samples));
    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    for (uint16_t y = 34U; y < LCD_H; y++)
    {
        for (uint16_t x = 0U; x < LCD_W; x++)
        {
            if (x < SCOPE_X || x >= SCOPE_X + SCOPE_W ||
                y < SCOPE_Y || y >= SCOPE_Y + SCOPE_H)
            {
                assert(lcd_pixels[y][x] == expected[y][x]);
            }
            if (y != 136U) assert(lcd_pixels[y][x] != GREEN);
        }
    }
    memcpy(flat_frame, lcd_pixels, sizeof(flat_frame));

    /* Returning from another page must restore the same cached scale. */
    LCD_Fill(0U, 0U, LCD_W, LCD_H, RED);
    reset_lcd_capture();
    draw_scope_page_full();
    assert_scope_axes_repainted();
    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    assert(memcmp(flat_frame, lcd_pixels, sizeof(flat_frame)) == 0);

    publish_scope_frame(20U, 0);
    assert_scope_axes_repainted();
    publish_scope_frame(20U, 0);
    assert_scope_axes_untouched();
    publish_scope_frame(20U, 1000);
    assert_scope_axes_repainted();
    publish_scope_frame(20U, 1000);
    assert_scope_axes_untouched();

    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++) test_frame.samples[i] = ((i / 25U) % 2U) ? UINT16_MAX : 0U;
    publish_scope_frame(1U, -10000);
    assert_scope_axes_repainted();
    save_scope_preview("wide-negative");
    publish_scope_frame(1U, 10000);
    assert_scope_axes_repainted();
    save_scope_preview("wide-positive");
    publish_scope_frame(80U, 0);
    assert_scope_axes_repainted();
    save_scope_preview("zoomed");
    publish_scope_frame(10U, 0);
    assert_scope_axes_repainted();
    assert(memcmp(expected, lcd_pixels, sizeof(expected)) == 0);
    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    assert(memcmp(expected, lcd_pixels, sizeof(expected)) == 0);
    reset_lcd_capture();
    draw_scope();
    assert_scope_axes_untouched();
    assert(lcd_text_count == 0U && lcd_plot_pixel_writes == 0U);
    assert(memcmp(expected, lcd_pixels, sizeof(expected)) == 0);
}

static void test_scope_grid_preservation(void)
{
    static const struct
    {
        uint8_t scale;
        int16_t offset;
    } cases[] = {{10U, 0}, {20U, 1000}, {80U, 10000}, {1U, -10000}};
    uint32_t random_state = 0x6c1d27a5U;

    reset_state();
    publish_scope_frame(10U, 0);
    for (uint16_t x = SCOPE_X; x < SCOPE_X + SCOPE_W; x++)
    {
        assert(lcd_pixels[136U][x] == GREEN);
    }
    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++) test_frame.samples[i] = UINT16_MAX;
    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    for (uint16_t x = SCOPE_X; x < SCOPE_X + SCOPE_W; x++)
    {
        assert(lcd_pixels[136U][x] == GRAYBLUE);
        assert(lcd_pixels[56U][x] == GREEN);
    }
    assert(lcd_pixels[96U][101U] == DARKBLUE);
    assert(lcd_pixels[97U][101U] == DARKBLUE);
    assert(lcd_pixels[96U][102U] == DARKBLUE);
    assert(lcd_pixels[97U][102U] == BLACK);

    memset(test_frame.samples, 0, sizeof(test_frame.samples));
    publish_scope_frame(10U, 0);
    for (uint16_t x = SCOPE_X; x < SCOPE_X + SCOPE_W; x++)
    {
        assert(lcd_pixels[56U][x] == DARKBLUE);
    }
    /* ADC changes within one screen pixel must not redraw the plot either. */
    for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++) test_frame.samples[i] = 1U;
    publish_scope_frame(10U, 0);
    assert(lcd_plot_pixel_writes == 0U);

    /* Exercise overlapping steep traces, clipping, and scale cache resets. */
    for (size_t c = 0U; c < sizeof(cases) / sizeof(cases[0]); c++)
    {
        for (uint8_t frame = 0U; frame < 12U; frame++)
        {
            for (uint16_t i = 0U; i < SCOPE_SAMPLES; i++)
            {
                random_state = random_state * 1664525U + 1013904223U;
                test_frame.samples[i] = (uint16_t)(random_state >> 16U);
            }
            publish_scope_frame(cases[c].scale, cases[c].offset);
            publish_scope_frame(cases[c].scale, cases[c].offset);
            assert_scope_axes_untouched();
            assert(lcd_plot_pixel_writes == 0U);
        }
    }
}

static void test_output_page_and_keys(void)
{
    char report[USB_REPORT_SIZE];
    static uint16_t expected[LCD_H][LCD_W];
    reset_state();
    output_status.ready = 1U;
    reset_lcd_capture();
    draw_output_page(1U);
    assert_label_layout(0U);
    assert(find_lcd_text("> FREQ: 10 Hz", 36U) != NULL);
    assert(find_lcd_text("ACT:10.000Hz D:50.000%", 158U) != NULL);
    save_scope_preview("pwm-default");
    reset_lcd_capture();
    draw_output_page(0U);
    assert(lcd_text_count == 0U && lcd_plot_fill_count == 0U && lcd_static_fill_count == 0U);
    output_key(KEY_UP);
    assert(s_output_field == OUTPUT_FIELD_ENABLE);
    output_key(KEY_LEFT);
    assert(output_status.requested.enabled == 0U && output_apply_count == 1U);
    output_key(KEY_RIGHT);
    assert(output_status.requested.enabled == 1U && output_apply_count == 2U);
    output_key(KEY_RIGHT);
    assert(output_apply_count == 2U);
    output_key(KEY_DOWN);
    assert(s_output_field == OUTPUT_FIELD_FREQ);
    output_key(KEY_RIGHT);
    assert(output_status.requested.frequency_hz == 20U);
    output_key(KEY_DOWN);
    assert(s_output_field == OUTPUT_FIELD_STEP);
    output_key(KEY_LEFT);
    assert(output_status.requested.frequency_step_hz == 1U);
    output_key(KEY_UP);
    output_key(KEY_RIGHT);
    assert(output_status.requested.frequency_hz == 21U);
    output_key(KEY_DOWN);
    output_key(KEY_DOWN);
    output_key(KEY_RIGHT);
    assert(output_status.requested.duty_permille == 501U);
    reset_lcd_capture();
    draw_output_page(0U);
    assert_label_layout(0U);
    assert(find_lcd_text("> DUTY: 50.1%", 92U) != NULL);
    memcpy(expected, lcd_pixels, sizeof(expected));
    reset_lcd_capture();
    draw_output_page(1U);
    assert(memcmp(expected, lcd_pixels, sizeof(expected)) == 0);

    output_defer = 1U;
    output_key(KEY_RIGHT);
    reset_lcd_capture();
    draw_output_page(0U);
    assert(find_lcd_text("  OUTPUT: ON (PENDING)", 120U) != NULL);
    assert(output_status.applied.duty_permille == 501U && output_status.requested.duty_permille == 502U);
    assert(usb_format_status(report, sizeof(report)) != 0U);
    assert(strstr(report, "OUTSTAT,1,21,501,1,") != NULL);
    save_scope_preview("pwm-pending");

    output_snapshot_count = 0U;
    output_change_after_snapshot = 1U;
    assert(usb_format_status(report, sizeof(report)) != 0U);
    assert(output_snapshot_count == 1U);
    assert(strstr(report, "OUT,PWM,21,502,1,1,") != NULL);
    assert(strstr(report, "OUTSTAT,1,21,501,1,") != NULL);
    output_status.error = OUTPUT_ERROR_START;
    output_status.failures = UINT32_MAX;
    output_status.pending = 0U;
    reset_lcd_capture();
    draw_output_page(0U);
    assert_label_layout(0U);
    assert(find_lcd_text("  OUTPUT: ERROR", 120U) != NULL);
    assert(find_lcd_text("ACT:0.000Hz D:0.000%", 158U) != NULL);
    assert(usb_format_status(report, sizeof(report)) != 0U);
    assert(strstr(report, "OUT,PWM,21,502,1,0,0\r\n") != NULL);
    save_scope_preview("pwm-error");
    output_change_after_snapshot = 0U;
    output_defer = 0U;
    output_status.ready = 1U;
    output_status.error = OUTPUT_ERROR_NONE;
    output_status.requested.frequency_hz = OUTPUT_PWM_MAX_HZ;
    output_status.requested.duty_permille = 1000U;
    (void)BusScope_OutputApply(&output_status.requested);
    reset_lcd_capture();
    draw_output_page(1U);
    assert_label_layout(0U);
    assert(find_lcd_text("ACT:100000.000Hz D:100.000%", 158U) != NULL);
    save_scope_preview("pwm-maximum");
    output_status.requested.frequency_hz = 1U;
    (void)BusScope_OutputApply(&output_status.requested);
    reset_lcd_capture();
    draw_output_page(0U);
    assert_label_layout(0U);
    assert(find_lcd_text("ACT:1.000Hz D:100.000%", 158U) != NULL);
    assert(test_critical_depth == 0U);
}

static void test_scope_envelope_and_timebase(void)
{
    reset_state();
    test_envelope = 1U;
    publish_scope_frame(10U, 0);
    test_frame.max[250] = UINT16_MAX;
    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    assert(lcd_pixels[56U][257U] == GREEN);
    assert(lcd_pixels[100U][257U] == GREEN);
    assert(lcd_pixels[136U][257U] == GREEN);
    save_scope_preview("scope-envelope");
    publish_scope_frame(10U, 0);
    assert(lcd_plot_pixel_writes == 0U);
    test_frame.max[250] = 0U;
    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    assert(lcd_pixels[56U][257U] == DARKBLUE && lcd_pixels[100U][257U] == BLACK);

    test_frame.period_us = 10U;
    publish_scope_frame(10U, 0);
    assert_scope_axes_repainted();
    assert(find_lcd_text("2.71", 225U) != NULL);
    save_scope_preview("scope-fast");
    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    test_frame.period_us = 2000U;
    publish_scope_frame(10U, 0);
    assert(find_lcd_text("542.00", 225U) != NULL);
    assert(find_lcd_text("134.00", 225U) != NULL);
    assert_lcd_labels_fit();
    save_scope_preview("scope-slow");
}

static void assert_scope_report_valid(uint8_t valid)
{
    char report[USB_REPORT_SIZE];
    char *line, *end;
    assert(usb_format_status(report, sizeof(report)) != 0U);
    line = strstr(report, "SCOPE,");
    assert(line != NULL);
    end = strstr(line, "\r\n");
    assert(end != NULL && end[-2] == ',' && end[-1] == (char)('0' + valid));
}

static void test_scope_marker_and_stale_frame(void)
{
    uint16_t marker_x;
    reset_state();
    test_frame.sequence = 7U;
    test_frame.triggered = 1U;
    test_frame.trigger_index = BUS_SCOPE_CAPTURE_PRETRIGGER;
    publish_scope_frame(10U, 0);
    marker_x = scope_sample_to_x(BUS_SCOPE_CAPTURE_PRETRIGGER);
    assert(lcd_pixels[51U][marker_x] == YELLOW);
    assert_scope_report_valid(1U);
    save_scope_preview("scope-triggered");
    publish_scope_frame(10U, 0);
    assert_scope_axes_untouched();
    assert(lcd_plot_pixel_writes == 0U);

    test_frame.trigger_index = 200U;
    publish_scope_frame(10U, 0);
    assert(lcd_pixels[51U][marker_x] == BLACK);
    marker_x = scope_sample_to_x(200U);
    assert(lcd_pixels[51U][marker_x] == YELLOW);
    test_frame.triggered = 0U;
    publish_scope_frame(10U, 0);
    assert(lcd_pixels[51U][marker_x] == BLACK);

    s_scope_config.mode = BUS_SCOPE_CAPTURE_HOLD;
    s_scope_config.decimation = 200U;
    s_scope_revision++;
    reset_lcd_capture();
    draw_scope();
    assert(s_scope_display_valid && s_scope_period_us == 50U);
    assert_scope_report_valid(1U);
    assert_lcd_labels_fit();
    save_scope_preview("scope-hold");

    s_scope_config.mode = BUS_SCOPE_CAPTURE_NORMAL;
    s_scope_revision++;
    reset_lcd_capture();
    draw_scope();
    assert(!s_scope_display_valid && s_scope_period_us == 2000U);
    assert_scope_report_valid(0U);
    assert(find_lcd_text("PP:--mV RMS:--mV AVG:--mV", 22U) != NULL);
    for (uint16_t y = SCOPE_Y; y < SCOPE_Y + SCOPE_H; y++)
        for (uint16_t x = SCOPE_X; x < SCOPE_X + SCOPE_W; x++)
            assert(lcd_pixels[y][x] != GREEN);
    reset_lcd_capture();
    draw_scope();
    assert(lcd_text_count == 0U && lcd_plot_pixel_writes == 0U);
    assert_scope_axes_untouched();

    /* HOLD restores the most recent completed frame after a stale-view clear. */
    s_scope_config.mode = BUS_SCOPE_CAPTURE_HOLD;
    s_scope_revision++;
    reset_lcd_capture();
    draw_scope();
    assert(s_scope_display_valid && s_scope_display.sequence == 7U && s_scope_period_us == 50U);
    assert(lcd_pixels[136U][100U] == GREEN);
    assert_scope_report_valid(1U);
    assert_lcd_labels_fit();
}

static void test_scope_keys_and_single_rearm(void)
{
    uint32_t revision;
    reset_state();
    scope_key(KEY_UP);
    assert(s_scope_field == SCOPE_FIELD_ARM);
    scope_key(KEY_DOWN);
    assert(s_scope_field == SCOPE_FIELD_TIME);
    scope_key(KEY_LEFT);
    assert(s_scope_config.decimation == 2U);
    for (unsigned i = 0U; i < 12U; i++) scope_key(KEY_LEFT);
    assert(s_scope_config.decimation == 1U);
    for (unsigned i = 0U; i < 12U; i++) scope_key(KEY_RIGHT);
    assert(s_scope_config.decimation == 200U);
    scope_key(KEY_DOWN);
    assert(s_scope_field == SCOPE_FIELD_MODE);
    scope_key(KEY_RIGHT);
    assert(s_scope_config.mode == BUS_SCOPE_CAPTURE_NORMAL);
    scope_key(KEY_RIGHT);
    assert(s_scope_config.mode == BUS_SCOPE_CAPTURE_SINGLE);
    scope_key(KEY_RIGHT);
    assert(s_scope_config.mode == BUS_SCOPE_CAPTURE_HOLD);
    scope_key(KEY_RIGHT);
    assert(s_scope_config.mode == BUS_SCOPE_CAPTURE_AUTO);
    scope_key(KEY_DOWN);
    scope_key(KEY_LEFT);
    assert(s_scope_config.falling);
    scope_key(KEY_RIGHT);
    assert(!s_scope_config.falling);
    scope_key(KEY_DOWN);
    assert(s_scope_field == SCOPE_FIELD_LEVEL);
    for (unsigned i = 0U; i < 80U; i++) scope_key(KEY_LEFT);
    assert(s_scope_config.trigger_mv == 50U);
    for (unsigned i = 0U; i < 80U; i++) scope_key(KEY_RIGHT);
    assert(s_scope_config.trigger_mv == 3250U);
    scope_key(KEY_DOWN);
    revision = s_scope_revision;
    for (unsigned i = 0U; i < 100U; i++) scope_key(KEY_RIGHT);
    assert(s_scope_y_scale_pending_x10 == 80U);
    for (unsigned i = 0U; i < 100U; i++) scope_key(KEY_LEFT);
    assert(s_scope_y_scale_pending_x10 == 1U && s_scope_revision == revision);
    scope_key(KEY_DOWN);
    for (unsigned i = 0U; i < 250U; i++) scope_key(KEY_RIGHT);
    assert(s_scope_y_offset_pending_mv == 10000);
    for (unsigned i = 0U; i < 250U; i++) scope_key(KEY_LEFT);
    assert(s_scope_y_offset_pending_mv == -10000 && s_scope_revision == revision);
    scope_key(KEY_DOWN);
    scope_key(KEY_LEFT);
    assert(s_scope_config.mode == BUS_SCOPE_CAPTURE_HOLD);
    scope_key(KEY_RIGHT);
    assert(s_scope_config.mode == BUS_SCOPE_CAPTURE_SINGLE);

    reset_state();
    s_scope_config.decimation = 1U;
    s_scope_config.mode = BUS_SCOPE_CAPTURE_SINGLE;
    sample_wave = 1U;
    run_sample_task(2U);
    assert(s_scope_stopped && s_scope_frame.sequence == 1U && s_scope_frame.triggered);
    assert(s_scope_frame.stats.frequency_valid && s_scope_frame.stats.freq_millihz == 1000000U);
    reset_lcd_capture();
    draw_scope();
    assert(find_lcd_text("DONE 100k F:1000.0Hz D:50.0%", 4U) != NULL);
    assert_lcd_labels_fit();
    save_scope_preview("scope-live");
    s_scope_ready = 0U;
    sample_gap_at = sample_iterations;
    run_sample_task(1U);
    assert(s_scope_stopped && !s_scope_ready && s_scope_frame.sequence == 1U);
    s_scope_field = SCOPE_FIELD_ARM;
    scope_key(KEY_RIGHT);
    run_sample_task(1U);
    assert(s_scope_stopped && s_scope_ready && s_scope_frame.sequence == 2U);
    assert(s_scope_frame_revision == s_scope_revision);
    scope_key(KEY_LEFT);
    s_scope_ready = 0U;
    run_sample_task(1U);
    assert(s_scope_stopped && !s_scope_ready && s_scope_frame.sequence == 2U);
}

static void test_short_ok_page_cycle(void)
{
    reset_state();
    s_app_ready = 1U;
    s_adc_ready = 1U;
    s_page = PAGE_CAN;
    acquisition_key = KEY_SELECT_ADC;
    key_test_active = 1U;
    for (unsigned press = 1U; press <= 3U; press++)
    {
        key_iterations = 0U;
        if (setjmp(key_exit) == 0) BusScope_KeyTask(NULL);
        assert(s_page == (DisplayPage)(press % 3U));
    }
    key_test_active = 0U;
    assert(test_critical_depth == 0U);
}

static void test_scope_auto_status(void)
{
    reset_state();
    s_scope_config.decimation = 1U;
    run_sample_task(2U);
    assert(s_scope_waiting && !s_scope_frame.triggered);
    reset_lcd_capture();
    draw_scope();
    assert(find_lcd_text("AUTO 100k F:-- D:--", 4U) != NULL);
    assert_lcd_labels_fit();
    save_scope_preview("scope-auto");
}

int main(int argc, char **argv)
{
    preview_prefix = (argc > 1) ? argv[1] : NULL;
    test_dlc_and_short_frame();
    test_full_can_fd_report();
    test_fps_elapsed_and_wrap();
    test_sampling_buffer_ownership();
    test_sampling_gap_and_tick_wrap();
    test_scope_axis_labels();
    test_scope_complete_frame_and_clipping();
    test_scope_axis_refresh();
    test_scope_grid_preservation();
    test_scope_envelope_and_timebase();
    test_scope_marker_and_stale_frame();
    test_scope_keys_and_single_rearm();
    test_short_ok_page_cycle();
    test_scope_auto_status();
    test_output_page_and_keys();
    puts("bus_scope_app: all tests passed");
    return 0;
}
