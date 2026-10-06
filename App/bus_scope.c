#include "bus_scope.h"
#include "bus_scope_signal.h"
#include "bus_scope_output.h"

#include "adc.h"
#include "fdcan.h"
#include "lcd.h"
#include "tim.h"
#include "usbd_cdc_if.h"
#include "cmsis_os.h"
#include "task.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ADC_CH_SCOPE        0U
#define ADC_CH_KEY          1U
#define ADC_DMA_CHANNELS    2U
#define CAN_PORTS           3U
#define CAN_RX_BUDGET       32U
#define USB_REPORT_MS       500U
#define USB_RETRY_MS        10U
#define USB_REPORT_SIZE     1536U
#define ADC_VREF_MV         3300U

/* LCD 例程里的按键是 ADC 分压键盘，这里沿用它的典型阈值。 */
#define KEY_SELECT_ADC      50U
#define KEY_DOWN_ADC        13000U
#define KEY_UP_ADC          26100U
#define KEY_LEFT_ADC        39100U
#define KEY_RIGHT_ADC       52200U
#define KEY_TOLERANCE       1000U
#define KEY_NONE_MIN        60000U

#define SCOPE_SAMPLES       272U
#define SCOPE_SAMPLE_MS     2U
#define SCOPE_X             44U
#define SCOPE_Y             56U
#define SCOPE_W             232U
#define SCOPE_H             161U
#define SCOPE_SPAN_MS       ((SCOPE_SAMPLES - 1U) * SCOPE_SAMPLE_MS)
#define SCOPE_TIME_TICK_MS   100U
#define SCOPE_Y_DIVISIONS    4U
#define SCOPE_AXIS_FONT      12U
#define SCOPE_AXIS_CHAR_W    (SCOPE_AXIS_FONT / 2U)
#define SCOPE_TIME_LABEL_Y   225U
#define SCOPE_TRACE_BYTES    ((SCOPE_W * SCOPE_H + 7U) / 8U)

#define SCOPE_Y_SCALE_X10_MIN  1U
#define SCOPE_Y_SCALE_X10_MAX  80U
#define SCOPE_Y_SCALE_X10_INIT 10U
#define SCOPE_Y_OFFSET_MV_STEP 1000
#define SCOPE_Y_OFFSET_MV_MIN  (-10000)
#define SCOPE_Y_OFFSET_MV_MAX  10000

typedef enum
{
    PAGE_CAN = 0,
    PAGE_SCOPE = 1,
    PAGE_OUTPUT = 2
} DisplayPage;

typedef enum
{
    KEY_NONE = 0,
    KEY_SELECT,
    KEY_DOWN,
    KEY_UP,
    KEY_LEFT,
    KEY_RIGHT
} KeyId;

typedef struct
{
    uint32_t count;
    uint32_t fps;
    uint32_t last_count;
    uint32_t last_tick;
    uint32_t last_id;
    uint32_t last_dlc;
    uint32_t last_flags;
    uint32_t rx_errors;
    uint32_t lost_events;
    uint8_t last_data[64];
    uint8_t last_len;
    uint8_t active;
    uint8_t initialized;
    uint8_t bus_off;
} CanPortState;

/* A private cache line in DMA-accessible SRAM1; see MDK-ARM/BusScope.sct. */
#if defined(__CC_ARM)
static volatile uint16_t s_adc_dma[16] __attribute__((section("ADC_DMA"), zero_init, aligned(32)));
#else
static volatile uint16_t s_adc_dma[16] __attribute__((section("ADC_DMA"), aligned(32)));
#endif
static uint16_t s_scope[2][SCOPE_SAMPLES];
static volatile int8_t s_scope_ready = -1;
static volatile uint32_t s_scope_dropped;
static volatile uint32_t s_scope_late;
static CanPortState s_can[CAN_PORTS];
/* 记录 LCD 上一次已经画出来的 CAN 状态，只在内容变化时刷新对应行。 */
static CanPortState s_can_drawn[CAN_PORTS];
static uint8_t s_can_draw_valid[CAN_PORTS];
static volatile uint32_t s_can_lost_events[CAN_PORTS];
static TaskHandle_t s_can_task;
static volatile DisplayPage s_page = PAGE_CAN;
static volatile uint8_t s_scope_y_scale_pending_x10 = SCOPE_Y_SCALE_X10_INIT;
static volatile int16_t s_scope_y_offset_pending_mv = 0;
static DisplayPage s_drawn_page = (DisplayPage)0xff;
static uint8_t s_scope_axes_valid;
static uint8_t s_scope_axes_scale_x10;
static int16_t s_scope_axes_offset_mv;
static uint8_t s_scope_trace_drawn[SCOPE_TRACE_BYTES];
static uint8_t s_scope_trace_next[SCOPE_TRACE_BYTES];
static uint16_t s_scope_grid_row[SCOPE_H];
static uint8_t s_scope_grid_column[SCOPE_W];
static volatile uint8_t s_app_ready;
static volatile uint8_t s_adc_ready;
static BusScopeOutputConfig s_output;
static BusScopeOutputField s_output_field;
static uint32_t s_output_revision;
static uint32_t s_output_drawn_revision;
static uint8_t s_output_drawn_ready;

static void output_snapshot(BusScopeOutputConfig *config, BusScopeOutputField *field, uint32_t *revision)
{
    taskENTER_CRITICAL();
    *config = s_output;
    if (field != NULL) *field = s_output_field;
    if (revision != NULL) *revision = s_output_revision;
    taskEXIT_CRITICAL();
}

static void output_key(KeyId key)
{
    BusScopeOutputConfig config;
    if (key == KEY_UP || key == KEY_DOWN)
    {
        taskENTER_CRITICAL();
        s_output_field = (BusScopeOutputField)(((unsigned)s_output_field +
            (key == KEY_DOWN ? 1U : OUTPUT_FIELD_COUNT - 1U)) % OUTPUT_FIELD_COUNT);
        s_output_revision++;
        taskEXIT_CRITICAL();
    }
    else if (key == KEY_LEFT || key == KEY_RIGHT)
    {
        output_snapshot(&config, NULL, NULL);
        BusScope_OutputAdjust(&config, s_output_field, key == KEY_RIGHT ? 1 : -1);
        if (config.frequency_hz == s_output.frequency_hz &&
            config.duty_permille == s_output.duty_permille && config.enabled == s_output.enabled &&
            BusScope_OutputReady()) return;
        /* Only this task applies edits; initialization finishes before keys are enabled. */
        (void)BusScope_OutputApply(&config);
        taskENTER_CRITICAL();
        s_output = config;
        s_output_revision++;
        taskEXIT_CRITICAL();
    }
}

static uint16_t adc_read(uint8_t channel)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)s_adc_dma, sizeof(s_adc_dma));
    }
    return s_adc_dma[channel];
}

static void can_snapshot(uint8_t index, CanPortState *state)
{
    taskENTER_CRITICAL();
    *state = s_can[index];
    state->lost_events = s_can_lost_events[index];
    taskEXIT_CRITICAL();
}

static uint16_t abs_diff_u16(uint16_t a, uint16_t b)
{
    return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static KeyId read_key(void)
{
    uint16_t adc = adc_read(ADC_CH_KEY);

    /* 没按键时 ADC 接近满量程；按键按下后落到各自的分压区间。 */
    if (adc >= KEY_NONE_MIN)
    {
        return KEY_NONE;
    }
    if (abs_diff_u16(adc, KEY_SELECT_ADC) <= KEY_TOLERANCE)
    {
        return KEY_SELECT;
    }
    if (abs_diff_u16(adc, KEY_DOWN_ADC) <= KEY_TOLERANCE)
    {
        return KEY_DOWN;
    }
    if (abs_diff_u16(adc, KEY_UP_ADC) <= KEY_TOLERANCE)
    {
        return KEY_UP;
    }
    if (abs_diff_u16(adc, KEY_LEFT_ADC) <= KEY_TOLERANCE)
    {
        return KEY_LEFT;
    }
    if (abs_diff_u16(adc, KEY_RIGHT_ADC) <= KEY_TOLERANCE)
    {
        return KEY_RIGHT;
    }

    return KEY_NONE;
}

static uint32_t adc_to_mv(uint16_t raw)
{
    return BusScope_AdcToMv(raw);
}

static uint8_t scope_mv_to_y(int32_t voltage_mv, uint8_t scale_x10, int16_t offset_mv, uint16_t *y_out)
{
    /* 固定量程映射：1.0x 时屏幕上下限为 +3.3V / -3.3V，0V 在中线。 */
    int32_t signal_mv = voltage_mv - (int32_t)offset_mv;
    int32_t y = (int32_t)(SCOPE_Y + (SCOPE_H / 2U)) -
                ((signal_mv * (int32_t)scale_x10 * (int32_t)(SCOPE_H / 2U)) /
                 ((int32_t)ADC_VREF_MV * 10));

    if (y < (int32_t)SCOPE_Y)
    {
        *y_out = SCOPE_Y;
        return 0U;
    }
    if (y > (int32_t)(SCOPE_Y + SCOPE_H - 1U))
    {
        *y_out = SCOPE_Y + SCOPE_H - 1U;
        return 0U;
    }

    *y_out = (uint16_t)y;
    return 1U;
}

static uint8_t scope_raw_to_y(uint16_t raw, uint8_t scale_x10, int16_t offset_mv, uint16_t *y_out)
{
    return scope_mv_to_y((int32_t)adc_to_mv(raw), scale_x10, offset_mv, y_out);
}

static uint16_t scope_time_to_x(uint32_t time_ms)
{
    return (uint16_t)(SCOPE_X + (time_ms * (SCOPE_W - 1U) + SCOPE_SPAN_MS / 2U) / SCOPE_SPAN_MS);
}

static int32_t scope_tick_mv(uint8_t tick, uint8_t scale_x10, int16_t offset_mv)
{
    int32_t displacement = (int32_t)(SCOPE_Y_DIVISIONS / 2U) - (int32_t)tick;
    return (int32_t)offset_mv + displacement * (int32_t)ADC_VREF_MV * 20 /
           ((int32_t)scale_x10 * (int32_t)SCOPE_Y_DIVISIONS);
}

static void scope_format_voltage(char *label, size_t capacity, int32_t millivolts)
{
    int32_t centivolts = (millivolts >= 0) ? (millivolts + 5) / 10 : -((-millivolts + 5) / 10);
    uint32_t magnitude = (uint32_t)((centivolts < 0) ? -centivolts : centivolts);
    (void)snprintf(label, capacity, "%s%lu.%02lu",
                   (centivolts < 0) ? "-" : ((centivolts > 0) ? "+" : ""),
                   (unsigned long)(magnitude / 100U), (unsigned long)(magnitude % 100U));
}

static uint8_t fdcan_dlc_to_len(uint32_t dlc)
{
    /* FDCAN HAL 的 DataLength 是 DLC 编码，不是直接的字节数。 */
    switch (dlc)
    {
    case FDCAN_DLC_BYTES_0: return 0;
    case FDCAN_DLC_BYTES_1: return 1;
    case FDCAN_DLC_BYTES_2: return 2;
    case FDCAN_DLC_BYTES_3: return 3;
    case FDCAN_DLC_BYTES_4: return 4;
    case FDCAN_DLC_BYTES_5: return 5;
    case FDCAN_DLC_BYTES_6: return 6;
    case FDCAN_DLC_BYTES_7: return 7;
    case FDCAN_DLC_BYTES_8: return 8;
    case FDCAN_DLC_BYTES_12: return 12;
    case FDCAN_DLC_BYTES_16: return 16;
    case FDCAN_DLC_BYTES_20: return 20;
    case FDCAN_DLC_BYTES_24: return 24;
    case FDCAN_DLC_BYTES_32: return 32;
    case FDCAN_DLC_BYTES_48: return 48;
    case FDCAN_DLC_BYTES_64: return 64;
    default: return 0;
    }
}

static void can_store(uint8_t index, const FDCAN_RxHeaderTypeDef *header, const uint8_t *data)
{
    CanPortState *port;
    uint8_t len;

    if (index >= CAN_PORTS)
    {
        return;
    }

    port = &s_can[index];
    len = fdcan_dlc_to_len(header->DataLength);
    if (header->FDFormat == FDCAN_CLASSIC_CAN && len > 8U)
    {
        len = 8U;
    }
    if (len > sizeof(port->last_data))
    {
        len = sizeof(port->last_data);
    }

    /* USB reports the latest frame periodically, not a lossless frame stream. */
    port->count++;
    port->active = 1;
    port->last_id = header->Identifier;
    port->last_dlc = header->DataLength;
    port->last_flags = header->FDFormat | header->BitRateSwitch | header->IdType;
    port->last_len = len;
    memset(port->last_data, 0, sizeof(port->last_data));
    memcpy(port->last_data, data, len);
}

static void can_drain(FDCAN_HandleTypeDef *hfdcan, uint8_t index)
{
    FDCAN_RxHeaderTypeDef header;
    uint8_t data[64];

    uint32_t budget = CAN_RX_BUDGET;

    while (budget-- > 0U && HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0U)
    {
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &header, data) == HAL_OK)
        {
            can_store(index, &header, data);
        }
        else
        {
            s_can[index].rx_errors++;
            break;
        }
    }
}

static uint8_t can_config_one(FDCAN_HandleTypeDef *hfdcan)
{
    FDCAN_FilterTypeDef filter = {0};

    /* 标准帧和扩展帧都用掩码 0 接收到 FIFO0，相当于分析仪的“全接收”模式。 */
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = 0x000;
    filter.FilterID2 = 0x000;
    if (HAL_FDCAN_ConfigFilter(hfdcan, &filter) != HAL_OK) return 0U;

    filter.IdType = FDCAN_EXTENDED_ID;
    filter.FilterIndex = 0;
    filter.FilterID1 = 0x00000000;
    filter.FilterID2 = 0x00000000;
    if (HAL_FDCAN_ConfigFilter(hfdcan, &filter) != HAL_OK) return 0U;

    if (HAL_FDCAN_ConfigGlobalFilter(hfdcan,
                                       FDCAN_ACCEPT_IN_RX_FIFO0,
                                       FDCAN_ACCEPT_IN_RX_FIFO0,
                                       FDCAN_REJECT_REMOTE,
                                       FDCAN_REJECT_REMOTE) != HAL_OK) return 0U;
    if (HAL_FDCAN_Start(hfdcan) != HAL_OK) return 0U;
    if (HAL_FDCAN_ActivateNotification(hfdcan,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST, 0) != HAL_OK)
    {
        (void)HAL_FDCAN_Stop(hfdcan);
        return 0U;
    }
    return 1U;
}

static void can_update_fps(void)
{
    uint32_t now = HAL_GetTick();

    /* 每秒结算一次 FPS；如果一秒内没新帧，就把该通道标成非活跃。 */
    for (uint8_t i = 0; i < CAN_PORTS; i++)
    {
        uint32_t elapsed = now - s_can[i].last_tick;
        if (elapsed >= 1000U)
        {
            uint32_t received = s_can[i].count - s_can[i].last_count;
            s_can[i].fps = (uint32_t)(((uint64_t)received * 1000U) / elapsed);
            s_can[i].last_count = s_can[i].count;
            s_can[i].last_tick = now;
            s_can[i].active = (received != 0U);
        }
    }
}

static uint8_t report_append(char *buffer, size_t capacity, size_t *used, const char *format, ...)
{
    int written;
    va_list args;
    if (*used >= capacity) return 0U;
    va_start(args, format);
    written = vsnprintf(buffer + *used, capacity - *used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= capacity - *used) return 0U;
    *used += (size_t)written;
    return 1U;
}

static uint16_t usb_format_status(char *report, size_t capacity)
{
    size_t used = 0U;
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0; i < CAN_PORTS; i++)
    {
        CanPortState state;
        can_snapshot(i, &state);
        if (!report_append(report, capacity, &used,
                       "CAN%u,%lu,%lu,0x%08lX,%u,%s,%s",
                       (unsigned)(i + 1U),
                       (unsigned long)now,
                       (unsigned long)state.fps,
                       (unsigned long)state.last_id,
                       (unsigned)state.last_len,
                       (state.last_flags & FDCAN_FD_CAN) ? "FD" : "CAN",
                       (state.last_flags & FDCAN_BRS_ON) ? "BRS" : "NOBRS")) return 0U;

        for (uint8_t j = 0; j < state.last_len; j++)
        {
            if (!report_append(report, capacity, &used, ",%02X", (unsigned)state.last_data[j])) return 0U;
        }
        if (!report_append(report, capacity, &used,
            "\r\nSTAT%u,%lu,%lu,%lu,%u,%u,%s\r\n",
            (unsigned)(i + 1U), (unsigned long)state.count,
            (unsigned long)state.lost_events, (unsigned long)state.rx_errors,
            (unsigned)state.initialized, (unsigned)state.bus_off,
            (state.last_flags & FDCAN_EXTENDED_ID) ? "EXT" : "STD")) return 0U;
    }
    if (!report_append(report, capacity, &used, "SYS,%u,%u,%lu,%lu\r\n",
        (unsigned)s_adc_ready, (unsigned)BusScope_OutputReady(),
        (unsigned long)s_scope_dropped, (unsigned long)s_scope_late)) return 0U;
    {
        BusScopeOutputConfig config;
        output_snapshot(&config, NULL, NULL);
        if (!report_append(report, capacity, &used, "OUT,PWM,%lu,%u,%u,%u,%lu\r\n", (unsigned long)config.frequency_hz,
            (unsigned)config.duty_permille, (unsigned)config.enabled,
            (unsigned)BusScope_OutputReady(), (unsigned long)BusScope_OutputActualMillihz())) return 0U;
    }
    return (uint16_t)used;
}

static void draw_header(const char *title)
{
    LCD_Fill(0, 0, LCD_W, 28, BLACK);
    LCD_ShowString(6, 6, (const uint8_t *)title, YELLOW, BLACK, 16, 0);
}

static void draw_can_page_full(void)
{
    LCD_Fill(0, 0, LCD_W, LCD_H, BLACK);
    draw_header("DM-MC02 BusScope  CAN");
    LCD_ShowString(8, 218, (const uint8_t *)"OK:PAGE  USB:CDC", GRAYBLUE, BLACK, 16, 0);
    memset(s_can_draw_valid, 0, sizeof(s_can_draw_valid));
}

static void draw_output_page(void)
{
    BusScopeOutputConfig config;
    BusScopeOutputField field;
    uint32_t revision;
    uint8_t ready = BusScope_OutputReady();
    char line[40];
    output_snapshot(&config, &field, &revision);
    LCD_Fill(0, 0, LCD_W, LCD_H, BLACK);
    draw_header("BusScope  PWM OUT  PE13");
    for (uint8_t i = 0U; i < OUTPUT_FIELD_COUNT; i++)
    {
        switch ((BusScopeOutputField)i)
        {
        case OUTPUT_FIELD_FREQ:
            snprintf(line, sizeof(line), "%c FREQ: %lu Hz", field == i ? '>' : ' ', (unsigned long)config.frequency_hz);
            break;
        case OUTPUT_FIELD_DUTY:
            snprintf(line, sizeof(line), "%c DUTY: %u.%u%%", field == i ? '>' : ' ',
                     (unsigned)(config.duty_permille / 10U), (unsigned)(config.duty_permille % 10U));
            break;
        default:
            snprintf(line, sizeof(line), "%c OUTPUT: %s", field == i ? '>' : ' ',
                !ready ? "ERROR" : config.enabled ? "ON" : "OFF");
            break;
        }
        LCD_ShowString(6, (uint16_t)(38U + i * 30U), (const uint8_t *)line,
                       field == i ? YELLOW : WHITE, BLACK, 16, 0);
    }
    LCD_ShowString(8, 166, (const uint8_t *)"DIRECT  0/3.3V", CYAN, BLACK, 16, 0);
    LCD_ShowString(8, 190, (const uint8_t *)"UP/DN:FIELD  L/R:VALUE", GRAYBLUE, BLACK, 16, 0);
    LCD_ShowString(8, 218, (const uint8_t *)"OK:PAGE", GRAYBLUE, BLACK, 16, 0);
    s_output_drawn_revision = revision;
    s_output_drawn_ready = ready;
}

static uint8_t can_row_changed(uint8_t index, const CanPortState *current)
{
    const CanPortState *drawn = &s_can_drawn[index];

    /* 第一次进入 CAN 页面必须画一次，之后只画有变化的行。 */
    if (s_can_draw_valid[index] == 0U)
    {
        return 1U;
    }

    if (drawn->count != current->count ||
        drawn->fps != current->fps ||
        drawn->last_id != current->last_id ||
        drawn->last_len != current->last_len ||
        drawn->last_flags != current->last_flags ||
        drawn->active != current->active ||
        drawn->initialized != current->initialized ||
        drawn->bus_off != current->bus_off ||
        drawn->rx_errors != current->rx_errors ||
        drawn->lost_events != current->lost_events)
    {
        return 1U;
    }

    return (memcmp(drawn->last_data, current->last_data, 8U) != 0) ? 1U : 0U;
}

static void draw_can_row_state(uint8_t index, uint16_t y, const CanPortState *state)
{
    char line[48];
    uint16_t color = (!state->initialized || state->bus_off) ? RED :
                     (state->active ? GREEN : GRAYBLUE);

    LCD_Fill(0, y, LCD_W, y + 56U, BLACK);
    snprintf(line, sizeof(line), "CAN%u F:%lu N:%lu",
             (unsigned)(index + 1U),
             (unsigned long)state->fps,
             (unsigned long)state->count);
    LCD_ShowString(8, y, (const uint8_t *)line, color, BLACK, 16, 0);

    snprintf(line, sizeof(line), "%08lX L:%02u %s %s %s%s",
             (unsigned long)state->last_id,
             (unsigned)state->last_len,
             (state->last_flags & FDCAN_EXTENDED_ID) ? "EXT" : "STD",
             (state->last_flags & FDCAN_FD_CAN) ? "FD" : "CAN",
             (state->last_flags & FDCAN_BRS_ON) ? "BRS" : "",
             (state->lost_events || state->rx_errors) ? " !" : "");
    LCD_ShowString(8, y + 18U, (const uint8_t *)line, WHITE, BLACK, 12, 0);

    for (uint8_t i = 0; i < 8U; i++)
    {
        if (i < state->last_len)
        {
            (void)snprintf(line + i * 3U, 4U, "%02X ", (unsigned)state->last_data[i]);
        }
        else
        {
            memcpy(line + i * 3U, "-- ", 3U);
        }
    }
    line[23] = '\0';
    LCD_ShowString(8, y + 36U, (const uint8_t *)line, CYAN, BLACK, 16, 0);
}

static void draw_can_row_if_changed(uint8_t index, uint16_t y)
{
    CanPortState current;

    can_snapshot(index, &current);
    if (can_row_changed(index, &current) == 0U)
    {
        return;
    }

    draw_can_row_state(index, y, &current);
    memcpy(&s_can_drawn[index], &current, sizeof(s_can_drawn[index]));
    s_can_draw_valid[index] = 1U;
}

static void draw_scope_grid(uint8_t scale_x10, int16_t offset_mv)
{
    uint16_t zero_y;

    memset(s_scope_grid_row, 0, sizeof(s_scope_grid_row));
    memset(s_scope_grid_column, 0, sizeof(s_scope_grid_column));
    for (uint8_t tick = 0U; tick <= SCOPE_Y_DIVISIONS; tick++)
    {
        uint16_t y = (uint16_t)(SCOPE_Y + tick * (SCOPE_H - 1U) / SCOPE_Y_DIVISIONS);
        s_scope_grid_row[y - SCOPE_Y] = DARKBLUE;
        LCD_Fill(SCOPE_X, y, SCOPE_X + SCOPE_W, y + 1U, DARKBLUE);
    }
    for (uint32_t time_ms = 0U; time_ms <= SCOPE_SPAN_MS; time_ms += SCOPE_TIME_TICK_MS)
    {
        uint16_t x = scope_time_to_x(time_ms);
        s_scope_grid_column[x - SCOPE_X] = 1U;
        LCD_Fill(x, SCOPE_Y, x + 1U, SCOPE_Y + SCOPE_H, DARKBLUE);
    }
    if (scope_mv_to_y(0, scale_x10, offset_mv, &zero_y) != 0U)
    {
        s_scope_grid_row[zero_y - SCOPE_Y] = GRAYBLUE;
        LCD_Fill(SCOPE_X, zero_y, SCOPE_X + SCOPE_W, zero_y + 1U, GRAYBLUE);
    }
}

static void draw_scope_axes(uint8_t scale_x10, int16_t offset_mv)
{
    char label[16];

    /* Clear labels too: changing scale/offset can shorten signs and digits. */
    LCD_Fill(0, 34U, LCD_W, LCD_H, BLACK);
    LCD_ShowString(6U, 36U, (const uint8_t *)"V", GRAYBLUE, BLACK, SCOPE_AXIS_FONT, 0);
    (void)snprintf(label, sizeof(label), "T:%lums", (unsigned long)SCOPE_SPAN_MS);
    LCD_ShowString((uint16_t)(SCOPE_X + SCOPE_W - strlen(label) * SCOPE_AXIS_CHAR_W), 36U,
                   (const uint8_t *)label, GRAYBLUE, BLACK, SCOPE_AXIS_FONT, 0);
    LCD_ShowString(2U, SCOPE_TIME_LABEL_Y, (const uint8_t *)"t(ms)",
                   GRAYBLUE, BLACK, SCOPE_AXIS_FONT, 0);

    for (uint8_t tick = 0U; tick <= SCOPE_Y_DIVISIONS; tick++)
    {
        uint16_t y = (uint16_t)(SCOPE_Y + tick * (SCOPE_H - 1U) / SCOPE_Y_DIVISIONS);
        uint16_t width;
        scope_format_voltage(label, sizeof(label), scope_tick_mv(tick, scale_x10, offset_mv));
        width = (uint16_t)(strlen(label) * SCOPE_AXIS_CHAR_W);
        LCD_ShowString(SCOPE_X - 6U - width, y - SCOPE_AXIS_FONT / 2U,
                       (const uint8_t *)label, GRAYBLUE, BLACK, SCOPE_AXIS_FONT, 0);
        LCD_Fill(SCOPE_X - 4U, y, SCOPE_X, y + 1U, GRAYBLUE);
    }

    for (uint32_t time_ms = 0U; time_ms <= SCOPE_SPAN_MS; time_ms += SCOPE_TIME_TICK_MS)
    {
        uint16_t x = scope_time_to_x(time_ms);
        uint16_t width;
        (void)snprintf(label, sizeof(label), "%lu", (unsigned long)time_ms);
        width = (uint16_t)(strlen(label) * SCOPE_AXIS_CHAR_W);
        LCD_ShowString(x - width / 2U, SCOPE_TIME_LABEL_Y, (const uint8_t *)label,
                       GRAYBLUE, BLACK, SCOPE_AXIS_FONT, 0);
        LCD_Fill(x, SCOPE_Y + SCOPE_H, x + 1U, SCOPE_Y + SCOPE_H + 4U, GRAYBLUE);
    }

    draw_scope_grid(scale_x10, offset_mv);
    LCD_Fill(SCOPE_X - 1U, SCOPE_Y - 1U, SCOPE_X + SCOPE_W + 1U, SCOPE_Y, GRAYBLUE);
    LCD_Fill(SCOPE_X - 1U, SCOPE_Y + SCOPE_H, SCOPE_X + SCOPE_W + 1U, SCOPE_Y + SCOPE_H + 1U, GRAYBLUE);
    LCD_Fill(SCOPE_X - 1U, SCOPE_Y, SCOPE_X, SCOPE_Y + SCOPE_H, GRAYBLUE);
    LCD_Fill(SCOPE_X + SCOPE_W, SCOPE_Y, SCOPE_X + SCOPE_W + 1U, SCOPE_Y + SCOPE_H, GRAYBLUE);
    memset(s_scope_trace_drawn, 0, sizeof(s_scope_trace_drawn));
    s_scope_axes_scale_x10 = scale_x10;
    s_scope_axes_offset_mv = offset_mv;
    s_scope_axes_valid = 1U;
}

static void draw_scope_page_full(void)
{
    uint8_t scale_x10;
    int16_t offset_mv;
    taskENTER_CRITICAL();
    scale_x10 = s_scope_y_scale_pending_x10;
    offset_mv = s_scope_y_offset_pending_mv;
    taskEXIT_CRITICAL();
    LCD_Fill(0, 0, LCD_W, LCD_H, BLACK);
    draw_scope_axes(scale_x10, offset_mv);
}

static void scope_trace_pixel(uint8_t *mask, uint16_t x, uint16_t y)
{
    uint32_t pixel;
    if (x < SCOPE_X || x >= SCOPE_X + SCOPE_W || y < SCOPE_Y || y >= SCOPE_Y + SCOPE_H)
    {
        return;
    }
    pixel = (uint32_t)(y - SCOPE_Y) * SCOPE_W + x - SCOPE_X;
    mask[pixel / 8U] |= (uint8_t)(1U << (pixel % 8U));
}

static void scope_trace_line(uint8_t *mask, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int32_t dx = (int32_t)x2 - x1;
    int32_t dy = (int32_t)y2 - y1;
    int32_t ix = (dx > 0) - (dx < 0);
    int32_t iy = (dy > 0) - (dy < 0);
    int32_t x = x1, y = y1, x_error = 0, y_error = 0;
    int32_t distance;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    distance = (dx > dy) ? dx : dy;

    /* Match User/lcd.c rasterization so switching to pixel diffs preserves the trace. */
    for (int32_t step = 0; step <= distance; step++)
    {
        scope_trace_pixel(mask, (uint16_t)x, (uint16_t)y);
        x_error += dx;
        y_error += dy;
        if (x_error > distance) { x_error -= distance; x += ix; }
        if (y_error > distance) { y_error -= distance; y += iy; }
    }
}

static void draw_scope_trace(const uint16_t *frame, uint8_t scale_x10, int16_t offset_mv)
{
    uint16_t prev_y;
    uint16_t prev_x = SCOPE_X;
    uint8_t prev_valid;
    memset(s_scope_trace_next, 0, sizeof(s_scope_trace_next));
    prev_valid = scope_raw_to_y(frame[0], scale_x10, offset_mv, &prev_y);
    for (uint16_t i = 1U; i < SCOPE_SAMPLES; i++)
    {
        uint16_t y;
        uint16_t x = scope_time_to_x((uint32_t)i * SCOPE_SAMPLE_MS);
        uint8_t valid = scope_raw_to_y(frame[i], scale_x10, offset_mv, &y);
        if (prev_valid != 0U || valid != 0U || prev_y != y)
        {
            scope_trace_line(s_scope_trace_next, prev_x, prev_y, x, y);
        }
        prev_x = x;
        prev_y = y;
        prev_valid = valid;
    }
    if (prev_valid != 0U) scope_trace_pixel(s_scope_trace_next, prev_x, prev_y);

    /* Update only the symmetric difference; restore covered grid pixels directly. */
    for (uint32_t byte = 0U; byte < SCOPE_TRACE_BYTES; byte++)
    {
        uint8_t changed = s_scope_trace_drawn[byte] ^ s_scope_trace_next[byte];
        if (changed == 0U) continue;
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            uint8_t flag = (uint8_t)(1U << bit);
            if ((changed & flag) != 0U)
            {
                uint32_t pixel = byte * 8U + bit;
                uint16_t x = (uint16_t)(pixel % SCOPE_W);
                uint16_t y = (uint16_t)(pixel / SCOPE_W);
                uint16_t background = s_scope_grid_row[y];
                if (background == BLACK && s_scope_grid_column[x] != 0U) background = DARKBLUE;
                LCD_DrawPoint(SCOPE_X + x, SCOPE_Y + y,
                    (s_scope_trace_next[byte] & flag) ? GREEN : background);
            }
        }
    }
    memcpy(s_scope_trace_drawn, s_scope_trace_next, sizeof(s_scope_trace_drawn));
}

static void draw_scope(void)
{
    static uint16_t frame[SCOPE_SAMPLES];
    BusScopePwmMeasure pwm;
    uint8_t y_scale_x10;
    int16_t y_offset_mv;
    uint16_t min = 0xffffU;
    uint16_t max = 0U;
    char line[64];

    taskENTER_CRITICAL();
    if (s_scope_ready < 0)
    {
        taskEXIT_CRITICAL();
        return;
    }

    memcpy(frame, s_scope[(uint8_t)s_scope_ready], sizeof(frame));
    s_scope_ready = -1;
    y_scale_x10 = s_scope_y_scale_pending_x10;
    y_offset_mv = s_scope_y_offset_pending_mv;
    taskEXIT_CRITICAL();

    for (uint16_t i = 0; i < SCOPE_SAMPLES; i++)
    {
        uint16_t v = frame[i];
        if (v < min) min = v;
        if (v > max) max = v;
    }
    BusScope_MeasurePwm(frame, SCOPE_SAMPLES, SCOPE_SAMPLE_MS * 1000U, &pwm);

    LCD_Fill(0, 0, LCD_W, 34U, BLACK);
    snprintf(line, sizeof(line), "V:%4lumV  MIN:%4lumV  MAX:%4lumV",
             (unsigned long)adc_to_mv(frame[SCOPE_SAMPLES - 1U]),
             (unsigned long)adc_to_mv(min),
             (unsigned long)adc_to_mv(max));
    LCD_ShowString(6, 4, (const uint8_t *)line, WHITE, BLACK, 12, 0);
    if (pwm.valid != 0U)
    {
        snprintf(line, sizeof(line), "PWM:%3luHz DUTY:%2lu.%1lu%% Y:%u.%ux O:%+ldV",
                 (unsigned long)pwm.freq_hz,
                 (unsigned long)(pwm.duty_permille / 10U),
                 (unsigned long)(pwm.duty_permille % 10U),
                 (unsigned)(y_scale_x10 / 10U),
                 (unsigned)(y_scale_x10 % 10U),
                 (long)(y_offset_mv / 1000));
    }
    else
    {
        snprintf(line, sizeof(line), "PWM:--Hz DUTY:--.-%% Y:%u.%ux O:%+ldV",
                 (unsigned)(y_scale_x10 / 10U),
                 (unsigned)(y_scale_x10 % 10U),
                 (long)(y_offset_mv / 1000));
    }
    LCD_ShowString(6, 22, (const uint8_t *)line, CYAN, BLACK, 12, 0);

    if (s_scope_axes_valid == 0U || s_scope_axes_scale_x10 != y_scale_x10 ||
        s_scope_axes_offset_mv != y_offset_mv)
    {
        draw_scope_axes(y_scale_x10, y_offset_mv);
    }
    draw_scope_trace(frame, y_scale_x10, y_offset_mv);
}

void BusScope_Init(void)
{
    FDCAN_HandleTypeDef *ports[CAN_PORTS] = {&hfdcan1, &hfdcan2, &hfdcan3};
    if (s_app_ready != 0U) return;

    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    memset((void *)s_adc_dma, 0, sizeof(s_adc_dma));
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)(void *)s_adc_dma, sizeof(s_adc_dma));
    }
    memset(s_scope, 0, sizeof(s_scope));
    s_scope_ready = -1;
    memset(s_can, 0, sizeof(s_can));
    memset(s_can_drawn, 0, sizeof(s_can_drawn));
    memset(s_can_draw_valid, 0, sizeof(s_can_draw_valid));
    memset((void *)s_can_lost_events, 0, sizeof(s_can_lost_events));

    /* ADC1 双通道 DMA：PA0 是示波器输入，PA5 是 LCD 按键输入。 */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) == HAL_OK &&
        HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)s_adc_dma, ADC_DMA_CHANNELS) == HAL_OK)
    {
        /* Only DMA errors need an IRQ; tasks read the latest conversion. */
        __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_HT | DMA_IT_TC);
        s_adc_ready = 1U;
    }

    BusScope_OutputDefault(&s_output);
    s_output_field = OUTPUT_FIELD_FREQ;
    if (BusScope_OutputInit()) (void)BusScope_OutputApply(&s_output);

    for (uint8_t i = 0; i < CAN_PORTS; i++)
    {
        s_can[i].initialized = can_config_one(ports[i]);
        s_can[i].last_tick = HAL_GetTick();
    }
    s_app_ready = 1U;
}

void BusScope_KeyTask(void const *argument)
{
    KeyId stable_key = KEY_NONE;
    KeyId last_sample = KEY_NONE;
    uint32_t last_change = 0;
    (void)argument;

    while (!s_app_ready)
    {
        osDelay(10);
    }

    for (;;)
    {
        KeyId key = s_adc_ready ? read_key() : KEY_NONE;
        uint32_t now = HAL_GetTick();
        if (key != last_sample)
        {
            last_sample = key;
            last_change = now;
        }

        if ((uint32_t)(now - last_change) > 60U && key != stable_key)
        {
            stable_key = key;
            if (stable_key == KEY_SELECT)
            {
                s_page = (DisplayPage)(((unsigned)s_page + 1U) % 3U);
            }
            else if (s_page == PAGE_OUTPUT)
            {
                output_key(stable_key);
            }
            else if (stable_key == KEY_UP && s_page == PAGE_SCOPE)
            {
                uint8_t scale = s_scope_y_scale_pending_x10;
                if (scale < SCOPE_Y_SCALE_X10_MAX)
                {
                    scale++;
                }
                else
                {
                    scale = SCOPE_Y_SCALE_X10_MAX;
                }
                s_scope_y_scale_pending_x10 = scale;
            }
            else if (stable_key == KEY_DOWN && s_page == PAGE_SCOPE)
            {
                uint8_t scale = s_scope_y_scale_pending_x10;
                if (scale > SCOPE_Y_SCALE_X10_MIN)
                {
                    scale--;
                }
                s_scope_y_scale_pending_x10 = scale;
            }
            else if (stable_key == KEY_LEFT && s_page == PAGE_SCOPE)
            {
                int16_t offset = s_scope_y_offset_pending_mv;
                if (offset <= (SCOPE_Y_OFFSET_MV_MAX - SCOPE_Y_OFFSET_MV_STEP))
                {
                    offset += SCOPE_Y_OFFSET_MV_STEP;
                }
                else
                {
                    offset = SCOPE_Y_OFFSET_MV_MAX;
                }
                s_scope_y_offset_pending_mv = offset;
            }
            else if (stable_key == KEY_RIGHT && s_page == PAGE_SCOPE)
            {
                int16_t offset = s_scope_y_offset_pending_mv;
                if (offset >= (SCOPE_Y_OFFSET_MV_MIN + SCOPE_Y_OFFSET_MV_STEP))
                {
                    offset -= SCOPE_Y_OFFSET_MV_STEP;
                }
                else
                {
                    offset = SCOPE_Y_OFFSET_MV_MIN;
                }
                s_scope_y_offset_pending_mv = offset;
            }
        }
        osDelay(20);
    }
}

void BusScope_LcdTask(void const *argument)
{
    uint32_t last_scope = 0;
    (void)argument;

    while (!s_app_ready)
    {
        osDelay(10);
    }

    LCD_Init();

    for (;;)
    {
        DisplayPage page = s_page;
        if (s_drawn_page != page)
        {
            /* 页面切换时全屏重画一次，避免上一页残留。 */
            s_drawn_page = page;
            if (page == PAGE_CAN)
            {
                draw_can_page_full();
            }
            else if (page == PAGE_SCOPE)
            {
                draw_scope_page_full();
            }
            else draw_output_page();
        }

        if (page == PAGE_CAN)
        {
            /* CAN 页只刷新变化的行，降低 LCD 闪烁和 SPI 占用。 */
            draw_can_row_if_changed(0, 36);
            draw_can_row_if_changed(1, 96);
            draw_can_row_if_changed(2, 156);
            osDelay(50);
        }
        else if (page == PAGE_SCOPE)
        {
            if ((uint32_t)(HAL_GetTick() - last_scope) >= 80U)
            {
                last_scope = HAL_GetTick();
                draw_scope();
            }
            osDelay(20);
        }
        else
        {
            uint32_t revision;
            BusScopeOutputConfig config;
            output_snapshot(&config, NULL, &revision);
            if (s_output_drawn_revision != revision || s_output_drawn_ready != BusScope_OutputReady())
                draw_output_page();
            osDelay(50);
        }
    }
}

void BusScope_CanTask(void const *argument)
{
    FDCAN_HandleTypeDef *ports[CAN_PORTS] = {&hfdcan1, &hfdcan2, &hfdcan3};
    uint32_t last_health = 0U;
    (void)argument;

    s_can_task = xTaskGetCurrentTaskHandle();
    BusScope_Init();

    for (;;)
    {
        /* Bound each port's work so a busy bus cannot monopolize reception. */
        for (uint8_t i = 0; i < CAN_PORTS; i++)
        {
            if (s_can[i].initialized) can_drain(ports[i], i);
        }
        can_update_fps();
        if ((uint32_t)(HAL_GetTick() - last_health) >= 100U)
        {
            last_health = HAL_GetTick();
            for (uint8_t i = 0; i < CAN_PORTS; i++)
            {
                FDCAN_ProtocolStatusTypeDef status;
                if (s_can[i].initialized && HAL_FDCAN_GetProtocolStatus(ports[i], &status) == HAL_OK)
                {
                    s_can[i].bus_off = (status.BusOff != 0U);
                }
            }
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1U));
    }
}

void BusScope_SampleTask(void const *argument)
{
    uint16_t pos = 0U;
    uint8_t write_index = 0U;
    TickType_t wake;
    TickType_t previous;
    const TickType_t period = pdMS_TO_TICKS(SCOPE_SAMPLE_MS);
    (void)argument;
    while (!s_app_ready) osDelay(10);
    configASSERT(period > 0U);
    wake = xTaskGetTickCount();
    previous = wake - period;

    for (;;)
    {
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - previous) != period)
        {
            /* Discard uneven frames instead of reporting an incorrect frequency. */
            pos = 0U;
            s_scope_late++;
            wake = now;
        }
        previous = now;
        if (s_adc_ready)
        {
            s_scope[write_index][pos++] = adc_read(ADC_CH_SCOPE);
            if (pos == SCOPE_SAMPLES)
            {
                taskENTER_CRITICAL();
                if (s_scope_ready >= 0) s_scope_dropped++;
                s_scope_ready = (int8_t)write_index;
                write_index ^= 1U;
                taskEXIT_CRITICAL();
                pos = 0U;
            }
        }
        vTaskDelayUntil(&wake, period);
    }
}

void BusScope_UsbTask(void const *argument)
{
    static char report[USB_REPORT_SIZE];
    uint16_t pending = 0U;
    uint32_t last_report;
    (void)argument;
    while (!s_app_ready) osDelay(10);
    last_report = HAL_GetTick() - USB_REPORT_MS;
    for (;;)
    {
        uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - last_report) >= USB_REPORT_MS)
        {
            last_report = now;
            pending = usb_format_status(report, sizeof(report));
        }
        if (pending != 0U && CDC_Transmit_HS((uint8_t *)report, pending) == USBD_OK)
        {
            pending = 0U;
        }
        osDelay(USB_RETRY_MS);
    }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    uint8_t index;
    BaseType_t wake = pdFALSE;
    if (hfdcan->Instance == FDCAN1) index = 0U;
    else if (hfdcan->Instance == FDCAN2) index = 1U;
    else if (hfdcan->Instance == FDCAN3) index = 2U;
    else return;

    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0U)
    {
        s_can_lost_events[index]++;
    }
    if (s_can_task != NULL)
    {
        vTaskNotifyGiveFromISR(s_can_task, &wake);
        portYIELD_FROM_ISR(wake);
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1) s_adc_ready = 0U;
}

void KeyTask_Entry(void const *argument)
{
    BusScope_KeyTask(argument);
}

void LcdTask_Entry(void const *argument)
{
    BusScope_LcdTask(argument);
}

void FunTest_Entry(void const *argument)
{
    BusScope_CanTask(argument);
}

void ImuTask_Entry(void const *argument)
{
    BusScope_SampleTask(argument);
}
