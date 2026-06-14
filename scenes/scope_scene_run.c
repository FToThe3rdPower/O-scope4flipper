#include <complex.h>
#include <math.h>
#include <float.h>
#include <string.h>

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_resources.h>
#include <gui/gui.h>
#include <gui/elements.h>

#include "stm32wbxx_ll_adc.h"
#include "stm32wbxx_ll_dma.h"
#include "stm32wbxx_ll_rcc.h"
#include "stm32wbxx_ll_bus.h"
#include "stm32wbxx_ll_tim.h"
#include "stm32wbxx_ll_gpio.h"
#include "stm32wbxx_ll_cortex.h"
#include "stm32wbxx_ll_utils.h"

#include "../scope_app_i.h"
#include "oscope_icons.h"

// ─── Constants ────────────────────────────────────────────────────────────────
#define VDDA_APPLI              ((uint32_t)2500)
#define DIGITAL_SCALE_12BITS    ((uint32_t)0xFFF)
#define ADC_DELAY_CALIB_CPU     (LL_ADC_DELAY_CALIB_ENABLE_ADC_CYCLES * 32)
#define TABLE_SIZE              79
#define DISPLAY_W               128
#define DISPLAY_H               64
#define PAN_STEP                16      // samples to pan per button press
#define VSHIFT_STEP_MV          100     // mV per up/down press

// ─── RAM vector table (must be 512-byte aligned) ──────────────────────────────
uint32_t ramVector[TABLE_SIZE + 1] __attribute__((aligned(512)));

// SystemClock lookup tables (required by LL RCC helpers)
const uint32_t AHBPrescTable[16UL] =
    {1,3,5,1,1,6,10,32,2,4,8,16,64,128,256,512};
const uint32_t APBPrescTable[8UL] = {0,0,0,0,1,2,3,4};
const uint32_t MSIRangeTable[16UL] = {
    100000,200000,400000,800000,1000000,2000000,4000000,
    8000000,16000000,24000000,32000000,48000000,0,0,0,0};

// ─── Global run-state (set in on_enter, read by draw callback + ISRs) ─────────
static char*            g_time_str;     // Label for current time period
static float            g_scale;        // Vertical zoom
static float            g_freq;         // Sample rate (Hz) — float: Cortex-M4F is single-precision only
static uint8_t          g_pause;        // 0 = live, 1 = frozen
static enum measureenum g_type;         // Active mode
static uint32_t         g_adc_buf_sz;   // Total ADC buffer depth (samples)
static uint16_t         g_trigger_mv;   // Pulse detection threshold (mV)
static int32_t          g_h_offset;     // Horizontal pan (samples from buf start)
static int16_t          g_v_offset_mv;  // Vertical level-shift (mV, can be negative)
static bool             g_hist_clear;   // Request histogram reset on next draw
static uint16_t*        g_capture_snap; // Snapshot buffer for triggered capture
static bool             g_capture_triggered; // True once the trigger has fired

// ─── ADC / DMA buffers ────────────────────────────────────────────────────────
static uint16_t*        adc_raw;        // DMA destination (12-bit raw)
static __IO uint16_t*   mv_buf_a;       // Double-buffer A (mV)
static __IO uint16_t*   mv_buf_b;       // Double-buffer B (mV)
static __IO uint8_t     dma_status = 2;
static __IO uint16_t*   mv_write;       // ISR writes here
static __IO uint16_t*   mv_display;     // draw callback reads here

// ─── Signal-processing scratch buffers ───────────────────────────────────────
static int16_t*         zero_idx;
static float*           norm_data;
static float*           crossings;
static float complex*   fft_buf;
static float*           fft_pwr;

// ─── Histogram accumulator ────────────────────────────────────────────────────
static uint32_t         g_histogram[DISPLAY_W];

// ─── Error handler ────────────────────────────────────────────────────────────
void Error_Handler(void) { while(1) {} }

// ─── Forward declarations ─────────────────────────────────────────────────────
void AdcDmaTransferComplete_Callback(void);
void AdcDmaTransferHalf_Callback(void);

// ─── IRQ handlers (installed into RAM vector table) ──────────────────────────
void DMA1_Channel1_IRQHandler(void) {
    if(LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);
        AdcDmaTransferComplete_Callback();
    }
    if(LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);
        AdcDmaTransferHalf_Callback();
    }
    if(LL_DMA_IsActiveFlag_TE1(DMA1)) {
        LL_DMA_ClearFlag_TE1(DMA1);
    }
}

void ADC1_IRQHandler(void) {
    if(LL_ADC_IsActiveFlag_OVR(ADC1)) {
        LL_ADC_ClearFlag_OVR(ADC1);
        LL_ADC_DisableIT_OVR(ADC1);
    }
}

void TIM2_IRQHandler(void) {}

// ─── DMA callbacks ────────────────────────────────────────────────────────────
void AdcDmaTransferHalf_Callback(void) {
    for(uint32_t i = 0; i < g_adc_buf_sz / 2; i++)
        mv_write[i] = __LL_ADC_CALC_DATA_TO_VOLTAGE(VDDA_APPLI, adc_raw[i], LL_ADC_RESOLUTION_12B);
    dma_status = 0;
}

void AdcDmaTransferComplete_Callback(void) {
    for(uint32_t i = g_adc_buf_sz / 2; i < g_adc_buf_sz; i++)
        mv_write[i] = __LL_ADC_CALC_DATA_TO_VOLTAGE(VDDA_APPLI, adc_raw[i], LL_ADC_RESOLUTION_12B);
    dma_status = 1;
    if(!g_pause) {
        __IO uint16_t* tmp = mv_write;
        mv_write   = mv_display;
        mv_display = tmp;
    }
}

// ─── Math helpers ─────────────────────────────────────────────────────────────
static float abs_errorf(float a, float b) { return fabsf((a - b) / a); }

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ─── FFT (Cooley-Tukey, iterative) ───────────────────────────────────────────
static void bit_reverse(float complex* X, int N) {
    for(int i = 0; i < N; i++) {
        int n = i, a = i, count = (int)ceil(log2f((float)N)) - 1;
        n >>= 1;
        while(n > 0) { a = (a << 1) | (n & 1); count--; n >>= 1; }
        n = (a << count) & (int)((1 << (int)ceil(log2f((float)N))) - 1);
        if(n > i) { float complex t = X[i]; X[i] = X[n]; X[n] = t; }
    }
}

static void fft(float complex* X, int N) {
    bit_reverse(X, N);
    for(int i = 1; i <= (int)ceil(log2f((float)N)); i++) {
        int stride = 1 << i;
        float complex w = cexpf(-2.0f * I * (float)M_PI / (float)stride);
        for(int j = 0; j < N; j += stride) {
            float complex v = 1.0f;
            for(int k = 0; k < stride / 2; k++) {
                X[k+j+stride/2] = X[k+j] - v * X[k+j+stride/2];
                X[k+j] -= (X[k+j+stride/2] - X[k+j]);
                v *= w;
            }
        }
    }
}

// ─── Hardware init ────────────────────────────────────────────────────────────
static void MX_GPIO_Init(void) {
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);
}

static void MX_DMA_Init(void) {
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMAMUX1);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    NVIC_SetPriority(DMA1_Channel1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 1, 0));
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

static void MX_ADC1_Init(void) {
    LL_ADC_CommonInitTypeDef adc_common = {0};
    LL_ADC_InitTypeDef       adc_init   = {0};
    LL_ADC_REG_InitTypeDef   adc_reg    = {0};
    LL_GPIO_InitTypeDef      gpio       = {0};

    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_ADC);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);
    gpio.Pin  = LL_GPIO_PIN_0;
    gpio.Mode = LL_GPIO_MODE_ANALOG;
    gpio.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOC, &gpio);

    LL_DMA_SetPeriphRequest(DMA1, LL_DMA_CHANNEL_1, LL_DMAMUX_REQ_ADC1);
    LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_CHANNEL_1, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
    LL_DMA_SetChannelPriorityLevel(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PRIORITY_HIGH);
    LL_DMA_SetMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MODE_CIRCULAR);
    LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PDATAALIGN_HALFWORD);
    LL_DMA_SetMemorySize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MDATAALIGN_HALFWORD);
    LL_DMAMUX_SetRequestID(DMAMUX1, LL_DMAMUX_CHANNEL_0, LL_DMAMUX_REQ_ADC1);
    LL_DMA_ConfigAddresses(DMA1, LL_DMA_CHANNEL_1,
        LL_ADC_DMA_GetRegAddr(ADC1, LL_ADC_DMA_REG_REGULAR_DATA),
        (uint32_t)adc_raw, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_1, g_adc_buf_sz);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableIT_HT(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);

    NVIC_SetPriority(ADC1_IRQn, 0);
    NVIC_EnableIRQ(ADC1_IRQn);

    adc_common.CommonClock = LL_ADC_CLOCK_SYNC_PCLK_DIV2;
    LL_ADC_CommonInit(__LL_ADC_COMMON_INSTANCE(ADC1), &adc_common);

    adc_init.Resolution   = LL_ADC_RESOLUTION_12B;
    adc_init.DataAlignment = LL_ADC_DATA_ALIGN_RIGHT;
    adc_init.LowPowerMode  = LL_ADC_LP_MODE_NONE;
    LL_ADC_Init(ADC1, &adc_init);

    adc_reg.TriggerSource   = LL_ADC_REG_TRIG_EXT_TIM2_TRGO;
    adc_reg.SequencerLength = LL_ADC_REG_SEQ_SCAN_DISABLE;
    adc_reg.SequencerDiscont = LL_ADC_REG_SEQ_DISCONT_DISABLE;
    adc_reg.ContinuousMode  = LL_ADC_REG_CONV_SINGLE;
    adc_reg.DMATransfer     = LL_ADC_REG_DMA_TRANSFER_UNLIMITED;
    adc_reg.Overrun         = LL_ADC_REG_OVR_DATA_OVERWRITTEN;
    LL_ADC_REG_Init(ADC1, &adc_reg);
    LL_ADC_SetOverSamplingScope(ADC1, LL_ADC_OVS_DISABLE);
    LL_ADC_REG_SetTriggerEdge(ADC1, LL_ADC_REG_TRIG_EXT_FALLING);

    LL_ADC_DisableDeepPowerDown(ADC1);
    LL_ADC_EnableInternalRegulator(ADC1);
    uint32_t w = (LL_ADC_DELAY_INTERNAL_REGUL_STAB_US * (SystemCoreClock / (100000 * 2))) / 10;
    while(w--) {}

    LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_1);
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_1, LL_ADC_SAMPLINGTIME_247CYCLES_5);
    LL_ADC_SetChannelSingleDiff(ADC1, LL_ADC_CHANNEL_1, LL_ADC_SINGLE_ENDED);
    LL_ADC_EnableIT_OVR(ADC1);
}

static void MX_TIM2_Init(int freq_hz) {
    uint32_t clk;
    if(LL_RCC_GetAPB1Prescaler() == LL_RCC_APB1_DIV_1)
        clk = __LL_RCC_CALC_PCLK1_FREQ(SystemCoreClock, LL_RCC_GetAPB1Prescaler());
    else
        clk = __LL_RCC_CALC_PCLK1_FREQ(SystemCoreClock, LL_RCC_GetAPB1Prescaler()) * 2;

    float target = (float)clk / (float)freq_hz;
    uint32_t psc = 0, arr = 0;
    float minerr = 1e9f;
    for(int i = 1; i < 65536; i++) {
        float a = target / (float)i;
        float err = abs_errorf((float)(int)a, a);
        if(err < 0.001f && err < minerr && a - 1.0f > 0.0f) {
            psc = (uint32_t)(i - 1);
            arr = (uint32_t)(a - 1.0f);
            minerr = err;
            break;
        }
    }

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_TIM_InitTypeDef tim = {0};
    tim.Prescaler   = psc;
    tim.CounterMode = LL_TIM_COUNTERMODE_UP;
    tim.Autoreload  = arr;
    tim.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    LL_TIM_Init(TIM2, &tim);
    LL_TIM_SetTriggerInput(TIM2, LL_TIM_TS_ITR0);
    LL_TIM_SetSlaveMode(TIM2, LL_TIM_SLAVEMODE_DISABLED);
    LL_TIM_DisableIT_TRIG(TIM2);
    LL_TIM_DisableDMAReq_TRIG(TIM2);
    LL_TIM_SetTriggerOutput(TIM2, LL_TIM_TRGO_UPDATE);
    LL_TIM_DisableMasterSlaveMode(TIM2);
    LL_TIM_EnableCounter(TIM2);
}

static void Activate_ADC(void) {
    if(LL_ADC_IsEnabled(ADC1)) return;
    LL_ADC_DisableDeepPowerDown(ADC1);
    LL_ADC_EnableInternalRegulator(ADC1);
    uint32_t w = (LL_ADC_DELAY_INTERNAL_REGUL_STAB_US * (SystemCoreClock / (100000 * 2))) / 10;
    while(w--) {}
    LL_ADC_StartCalibration(ADC1, LL_ADC_SINGLE_ENDED);
    while(LL_ADC_IsCalibrationOnGoing(ADC1)) {}
    w = ADC_DELAY_CALIB_CPU >> 1;
    while(w--) {}
    LL_ADC_Enable(ADC1);
    while(!LL_ADC_IsActiveFlag_ADRDY(ADC1)) {}
}

// ─── Waveform draw helper ─────────────────────────────────────────────────────
// Maps a millivolt value + vertical offset + scale to a display Y coordinate.
static inline int32_t mv_to_y(float mv) {
    float shifted = mv + (float)g_v_offset_mv;
    return 63 - (int32_t)((shifted / (float)VDDA_APPLI) * g_scale * (float)(DISPLAY_H - 1));
}

// Draw a dashed horizontal line (every 4px, 2px on)
static void draw_dashed_hline(Canvas* canvas, int32_t y) {
    if(y < 0 || y >= DISPLAY_H) return;
    for(int x = 0; x < DISPLAY_W; x += 4)
        canvas_draw_line(canvas, x, y, x + 1, y);
}

// ─── Main draw callback ───────────────────────────────────────────────────────
static void app_draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    static char hud[50];
    uint32_t n = g_adc_buf_sz;

    // Reset histogram if requested (safe – draw runs in main task, not ISR)
    if(g_hist_clear) {
        memset(g_histogram, 0, sizeof(g_histogram));
        g_hist_clear = false;
    }

    // ── Pause / play icon (top-right) ────────────────────────────────────────
    canvas_draw_icon(canvas, 116, 1, g_pause ? &I_pause_10x10 : &I_play_10x10);

    // ── Mode-specific button hints ────────────────────────────────────────────
    if(g_type == m_capture) {
        if(!g_pause) elements_button_center(canvas, "Stop");
        else {
            elements_button_center(canvas, "REC");
            elements_button_right(canvas, "Save");
        }
    } else if(g_type == m_record) {
        elements_button_center(canvas, g_pause ? "Resume" : "Pause");
    }

    // ── Global min/max across full buffer (in mV) ─────────────────────────────
    float buf_max_mv = 0.0f, buf_min_mv = 2500.0f;
    for(uint32_t i = 0; i < n; i++) {
        float v = (float)mv_display[i];
        if(v > buf_max_mv) buf_max_mv = v;
        if(v < buf_min_mv) buf_min_mv = v;
    }
    float buf_max_v = buf_max_mv / 1000.0f;
    float buf_min_v = buf_min_mv / 1000.0f;

    // ── Mode-specific overlays ────────────────────────────────────────────────
    switch(g_type) {

    case m_time: {
        snprintf(hud, sizeof(hud), "T:%s", g_time_str);
        canvas_draw_str(canvas, 2, 10, hud);
        snprintf(hud, sizeof(hud), "%.0fx", (double)g_scale);
        canvas_draw_str(canvas, 95, 10, hud);

        // Normalise to [-1, 1] around signal midpoint for zero-crossing detection
        float range = buf_max_v - buf_min_v;
        int   n_cross = 0;
        for(uint32_t i = 0; i < n; i++) {
            zero_idx[i] = -1;
            crossings[i] = -1.0f;
            norm_data[i] = (range > 0.001f)
                ? ((2.0f / range) * ((float)mv_display[i] / 1000.0f - buf_min_v)) - 1.0f
                : 0.0f;
        }
        for(uint32_t i = 1; i < n; i++)
            if(norm_data[i] >= 0.0f && norm_data[i-1] < 0.0f)
                zero_idx[n_cross++] = (int16_t)(i - 1);

        int cn = 0;
        for(int i = 0; i < n_cross; i++) {
            int16_t xi = zero_idx[i];
            float d0 = norm_data[xi], d1 = norm_data[xi + 1];
            crossings[cn++] = (float)xi - d0 / (d1 - d0);
        }

        float sum = 0.0f; int pairs = 0;
        for(int i = 0; i + 1 < cn; i++) {
            sum += crossings[i+1] - crossings[i];
            pairs++;
        }
        if(pairs > 0 && sum > 0.0f)
            snprintf(hud, sizeof(hud), "%.1f Hz", (double)((float)g_freq / (sum / (float)pairs)));
        else
            snprintf(hud, sizeof(hud), "< 1 Hz");
        canvas_draw_str(canvas, 2, 20, hud);
    } break;

    case m_voltage: {
        snprintf(hud, sizeof(hud), "%.0fx", (double)g_scale);
        canvas_draw_str(canvas, 95, 10, hud);
        snprintf(hud, sizeof(hud), "Max:%.2fV", (double)buf_max_v);
        canvas_draw_str(canvas, 2, 10, hud);
        snprintf(hud, sizeof(hud), "Min:%.2fV", (double)buf_min_v);
        canvas_draw_str(canvas, 2, 20, hud);
        snprintf(hud, sizeof(hud), "Vpp:%.2fV", (double)(buf_max_v - buf_min_v));
        canvas_draw_str(canvas, 2, 30, hud);
    } break;

    case m_fft: {
        for(uint32_t i = 0; i < n; i++)
            fft_buf[i] = (float)mv_display[i] / 1000.0f;
        fft(fft_buf, (int)n);

        float peak_pwr = -1.0f; int peak_bin = 0;
        for(uint32_t i = 1; i < n / 2; i++) {
            float p = cabsf(fft_buf[i]) * cabsf(fft_buf[i]);
            if(p > peak_pwr) { peak_pwr = p; peak_bin = (int)i; }
            fft_pwr[i] = p;
        }
        snprintf(hud, sizeof(hud), "%.1fHz", (double)((float)peak_bin * (g_freq / (float)n)));
        canvas_draw_str(canvas, 2, 10, hud);

        // Render FFT bars
        float fft_max = 0.0f;
        uint32_t step = (n / 2) / DISPLAY_W;
        if(step == 0) step = 1;
        for(uint32_t i = 1; i < n / 2; i += step) {
            float s = 0.0f;
            for(uint32_t j = i; j < i + step && j < n / 2; j++) s += fft_pwr[j];
            if(s > fft_max) fft_max = s;
        }
        uint32_t xp = 0;
        for(uint32_t i = 1; i < n / 2 && xp < DISPLAY_W; i += step) {
            float s = 0.0f;
            for(uint32_t j = i; j < i + step && j < n / 2; j++) s += fft_pwr[j];
            uint32_t h = (fft_max > 0) ? (uint32_t)((s / fft_max) * (DISPLAY_H - 1)) : 0;
            if(h > 0) canvas_draw_line(canvas, (int32_t)xp, DISPLAY_H-1, (int32_t)xp, (int32_t)(DISPLAY_H-1) - (int32_t)h);
            xp++;
        }
        return; // FFT handles its own render, skip waveform below
    }

    case m_pulse: {
        // Count rising edges above threshold across the full buffer
        uint32_t pulses = 0;
        for(uint32_t i = 1; i < n; i++)
            if(mv_display[i] >= g_trigger_mv && mv_display[i-1] < g_trigger_mv)
                pulses++;

        float buf_secs = (float)n / (float)g_freq;
        float cps = (buf_secs > 0.0f) ? (float)pulses / buf_secs : 0.0f;
        snprintf(hud, sizeof(hud), "CPS:%.1f", (double)cps);
        canvas_draw_str(canvas, 2, 10, hud);
        snprintf(hud, sizeof(hud), "CPM:%.0f", (double)(cps * 60.0f));
        canvas_draw_str(canvas, 2, 20, hud);
        snprintf(hud, sizeof(hud), "n=%lu T:%dmV", (unsigned long)pulses, (int)g_trigger_mv);
        canvas_draw_str(canvas, 2, 30, hud);

        // Dashed trigger line on right half (avoids HUD text)
        int32_t ty = mv_to_y((float)g_trigger_mv);
        if(ty >= 0 && ty < DISPLAY_H)
            for(int x = 64; x < DISPLAY_W; x += 4)
                canvas_draw_line(canvas, x, ty, x+1, ty);
    } break;

    case m_histogram: {
        // Find peaks of pulses above threshold and accumulate into histogram
        bool above = false; uint16_t peak = 0;
        for(uint32_t i = 0; i < n; i++) {
            uint16_t v = mv_display[i];
            if(v >= g_trigger_mv) {
                above = true;
                if(v > peak) peak = v;
            } else if(above) {
                uint32_t bin = (uint32_t)((float)peak / 2500.0f * (float)(DISPLAY_W - 1));
                if(bin >= DISPLAY_W) bin = DISPLAY_W - 1;
                g_histogram[bin]++;
                above = false; peak = 0;
            }
        }

        // Normalise and draw bars
        uint32_t h_max = 1;
        for(int b = 0; b < DISPLAY_W; b++)
            if(g_histogram[b] > h_max) h_max = g_histogram[b];
        for(int b = 0; b < DISPLAY_W; b++) {
            if(g_histogram[b] == 0) continue;
            uint32_t bar = (uint32_t)((float)g_histogram[b] / (float)h_max * (float)(DISPLAY_H - 1));
            if(bar > 0) canvas_draw_line(canvas, b, DISPLAY_H-1, b, (int32_t)(DISPLAY_H-1) - (int32_t)bar);
        }

        canvas_draw_str(canvas, 2, 10, "PHD");
        snprintf(hud, sizeof(hud), "T:%dmV", (int)g_trigger_mv);
        canvas_draw_str(canvas, 2, 20, hud);
        if(g_pause) elements_button_left(canvas, "Clear");
        return; // histogram has its own full-screen render
    }

    case m_record: {
        snprintf(hud, sizeof(hud), "REC T:%s", g_time_str);
        canvas_draw_str(canvas, 2, 10, hud);
    } break;

    case m_capture: {
        // Use snapshot when triggered; live buffer while waiting
        const uint16_t* src = g_capture_triggered
            ? g_capture_snap
            : (const uint16_t*)mv_display;

        if(!g_capture_triggered) {
            snprintf(hud, sizeof(hud), "WAIT >%dmV", (int)g_trigger_mv);
            canvas_draw_str(canvas, 2, 10, hud);
            draw_dashed_hline(canvas, mv_to_y((float)g_trigger_mv));
            elements_button_center(canvas, "Force");
        } else {
            canvas_draw_str(canvas, 2, 10, "CAPTURED");
            snprintf(hud, sizeof(hud), ">%dmV", (int)g_trigger_mv);
            canvas_draw_str(canvas, 2, 20, hud);
            elements_button_center(canvas, "Next");
            elements_button_right(canvas, "Save");
        }

        for(uint32_t px = 1; px < DISPLAY_W; px++) {
            uint32_t si = (uint32_t)g_h_offset + px;
            if(si >= g_adc_buf_sz) break;
            int32_t y0 = mv_to_y((float)src[si - 1]);
            int32_t y1 = mv_to_y((float)src[si]);
            if(y0 < 0 && y1 < 0) continue;
            canvas_draw_line(canvas,
                (int32_t)(px - 1), clampi(y0, 0, DISPLAY_H - 1),
                (int32_t)px,       clampi(y1, 0, DISPLAY_H - 1));
        }
        return;
    }

    default:
        break;
    }

    // ── Waveform rendering ────────────────────────────────────────────────────
    // Show 128 pixels mapped to samples [g_h_offset .. g_h_offset+127].
    // v_offset_mv shifts the waveform vertically (level shifting).
    for(uint32_t px = 1; px < DISPLAY_W; px++) {
        uint32_t si = (uint32_t)g_h_offset + px;
        if(si >= n) break;
        int32_t y0 = mv_to_y((float)mv_display[si - 1]);
        int32_t y1 = mv_to_y((float)mv_display[si]);
        if(y0 < 0 && y1 < 0) continue;
        canvas_draw_line(canvas,
            (int32_t)(px - 1), (int32_t)clampi(y0, 0, DISPLAY_H-1),
            (int32_t)px,       (int32_t)clampi(y1, 0, DISPLAY_H-1));
    }

    // Trigger threshold reference line (pulse mode already draws it; show in other modes too)
    if(g_type == m_time || g_type == m_voltage) {
        // skip threshold line in these modes
    }

    // ── HUD: pan position + level-shift offset ────────────────────────────────
    if(g_pause && n > DISPLAY_W) {
        snprintf(hud, sizeof(hud), "%lu/%lu", (unsigned long)(g_h_offset + 1), (unsigned long)n);
        canvas_draw_str(canvas, 60, 10, hud);
    }
    if(g_v_offset_mv != 0) {
        snprintf(hud, sizeof(hud), "%+dmV", (int)g_v_offset_mv);
        canvas_draw_str(canvas, 60, 20, hud);
        // Draw a dashed zero-reference line so the user can see where 0V sits
        draw_dashed_hline(canvas, mv_to_y(0.0f));
    }
}

// ─── Input callback ───────────────────────────────────────────────────────────
static void app_input_callback(InputEvent* ev, void* ctx) {
    furi_assert(ctx);
    furi_message_queue_put((FuriMessageQueue*)ctx, ev, FuriWaitForever);
}

// ─── Free all heap buffers ────────────────────────────────────────────────────
static void free_bufs(void) {
    free(adc_raw);          adc_raw      = NULL;
    free((void*)mv_buf_a);  mv_buf_a     = NULL;
    free((void*)mv_buf_b);  mv_buf_b     = NULL;
    free(zero_idx);         zero_idx     = NULL;
    free(norm_data);        norm_data    = NULL;
    free(crossings);        crossings    = NULL;
    free(fft_buf);          fft_buf      = NULL;
    free(fft_pwr);          fft_pwr          = NULL;
    free(g_capture_snap);   g_capture_snap   = NULL;
}

// ─── Scene entry ──────────────────────────────────────────────────────────────
void scope_scene_run_on_enter(void* context) {
    ScopeApp* app = context;

    // Copy settings into run-globals
    g_scale      = app->scale;
    g_pause      = 0;
    g_type       = app->measurement;
    g_trigger_mv = app->trigger_mv;
    g_h_offset   = 0;
    g_v_offset_mv = 0;
    g_hist_clear = true;  // Start each session with a clean histogram

    // Resolve time-period label
    g_time_str = "?";
    for(uint32_t i = 0; i < COUNT_OF(time_list); i++) {
        if(time_list[i].time == app->time) { g_time_str = time_list[i].str; break; }
    }

    // Buffer depth: use FFT window size in FFT mode, full ADC_BUFFER_SIZE otherwise
    g_adc_buf_sz = (g_type == m_fft) ? (uint32_t)app->fft : ADC_BUFFER_SIZE;
    g_freq       = 1.0f / (float)app->time;

    // Allocate buffers
    adc_raw  = malloc(g_adc_buf_sz * sizeof(uint16_t));
    mv_buf_a = malloc(g_adc_buf_sz * sizeof(uint16_t));
    mv_buf_b = malloc(g_adc_buf_sz * sizeof(uint16_t));
    zero_idx  = malloc(g_adc_buf_sz * sizeof(int16_t));
    norm_data = malloc(g_adc_buf_sz * sizeof(float));
    crossings = malloc(g_adc_buf_sz * sizeof(float));
    fft_buf   = malloc(g_adc_buf_sz * sizeof(float complex));
    fft_pwr          = malloc(g_adc_buf_sz * sizeof(float));
    g_capture_snap   = malloc(g_adc_buf_sz * sizeof(uint16_t));
    g_capture_triggered = false;

    mv_write   = mv_buf_a;
    mv_display = mv_buf_b;

    // Zero initial data
    memset(adc_raw,  0, g_adc_buf_sz * sizeof(uint16_t));
    memset((void*)mv_buf_a, 0, g_adc_buf_sz * sizeof(uint16_t));
    memset((void*)mv_buf_b, 0, g_adc_buf_sz * sizeof(uint16_t));

    // Relocate vector table to RAM so we can install our IRQ handlers
    __disable_irq();
    memcpy(ramVector, (uint32_t*)(FLASH_BASE | SCB->VTOR), sizeof(uint32_t) * TABLE_SIZE);
    SCB->VTOR = (uint32_t)ramVector;
    ramVector[27] = (uint32_t)DMA1_Channel1_IRQHandler;
    ramVector[34] = (uint32_t)ADC1_IRQHandler;
    ramVector[44] = (uint32_t)TIM2_IRQHandler;
    __enable_irq();

    furi_hal_bus_enable(FuriHalBusTIM2);

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_TIM2_Init((int)g_freq);

    // Enable internal 2.5 V reference (not connected externally on Flipper Zero)
    VREFBUF->CSR |= VREFBUF_CSR_ENVR;
    VREFBUF->CSR &= ~VREFBUF_CSR_HIZ;
    VREFBUF->CSR |= VREFBUF_CSR_VRS;
    while(!(VREFBUF->CSR & VREFBUF_CSR_VRR)) {}

    MX_ADC1_Init();
    Activate_ADC();

    if(LL_ADC_IsEnabled(ADC1) && !LL_ADC_IsDisableOngoing(ADC1) &&
       !LL_ADC_REG_IsConversionOngoing(ADC1))
        LL_ADC_REG_StartConversion(ADC1);

    // Set up viewport
    FuriMessageQueue* eq = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, app_draw_callback, vp);
    view_port_input_callback_set(vp, app_input_callback, eq);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    // ── Main event loop ───────────────────────────────────────────────────────
    InputEvent ev;
    bool running = true, do_save = false;
    while(running) {
        if(furi_message_queue_get(eq, &ev, 150) == FuriStatusOk) {
            if(ev.type == InputTypePress || ev.type == InputTypeRepeat) {
                switch(ev.key) {

                case InputKeyOk:
                    if(g_type == m_capture) {
                        if(g_capture_triggered) {
                            // "Next": discard snapshot, resume watching
                            g_capture_triggered = false;
                            g_pause = 0;
                            g_h_offset = 0;
                        } else {
                            // "Force": snapshot the current buffer immediately
                            const uint16_t* snap = (const uint16_t*)mv_display;
                            memcpy(g_capture_snap, snap, g_adc_buf_sz * sizeof(uint16_t));
                            g_capture_triggered = true;
                            g_pause = 1;
                        }
                    } else if(g_type == m_histogram && g_pause) {
                        g_hist_clear = true;
                        g_pause = 0;
                    } else {
                        g_pause ^= 1;
                        if(!g_pause) g_h_offset = 0;
                    }
                    break;

                case InputKeyUp:
                    g_v_offset_mv += VSHIFT_STEP_MV;
                    if(g_v_offset_mv > 2500) g_v_offset_mv = 2500;
                    break;

                case InputKeyDown:
                    g_v_offset_mv -= VSHIFT_STEP_MV;
                    if(g_v_offset_mv < -2500) g_v_offset_mv = -2500;
                    break;

                case InputKeyRight:
                    if(g_type == m_capture && g_capture_triggered) {
                        running = false; do_save = true;
                    } else if(g_pause) {
                        int32_t max_off = (int32_t)g_adc_buf_sz - DISPLAY_W;
                        if(max_off < 0) max_off = 0;
                        g_h_offset += PAN_STEP;
                        if(g_h_offset > max_off) g_h_offset = max_off;
                    }
                    break;

                case InputKeyLeft:
                    if(g_type == m_histogram && g_pause) {
                        g_hist_clear = true; // Left clears histogram when paused
                    } else if(g_pause) {
                        g_h_offset -= PAN_STEP;
                        if(g_h_offset < 0) g_h_offset = 0;
                    }
                    break;

                default:
                    running = false;
                    break;
                }
            }
        }
        // Triggered-capture: scan the current display buffer for a rising edge
        if(g_type == m_capture && !g_capture_triggered) {
            const uint16_t* snap = (const uint16_t*)mv_display;
            for(uint32_t i = 1; i < g_adc_buf_sz; i++) {
                if(snap[i] >= g_trigger_mv && snap[i - 1] < g_trigger_mv) {
                    memcpy(g_capture_snap, snap, g_adc_buf_sz * sizeof(uint16_t));
                    g_capture_triggered = true;
                    g_pause = 1;
                    g_h_offset = 0;
                    break;
                }
            }
        }

        view_port_update(vp);
    }

    // ── Teardown hardware ─────────────────────────────────────────────────────
    furi_hal_bus_disable(FuriHalBusTIM2);
    LL_ADC_DisableIT_OVR(ADC1);
    LL_TIM_DisableCounter(TIM2);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_1);
    __disable_irq();
    SCB->VTOR = 0;
    __enable_irq();

    // ── Remove viewport ───────────────────────────────────────────────────────
    view_port_enabled_set(vp, false);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(eq);

    // ── Navigate ──────────────────────────────────────────────────────────────
    if(do_save) {
        app->data_size = g_adc_buf_sz;
        app->data = malloc(sizeof(uint16_t) * g_adc_buf_sz);
        // Capture mode saves the triggered snapshot; other modes save the live buffer
        const void* src = (g_type == m_capture && g_capture_snap)
            ? (const void*)g_capture_snap
            : (const void*)mv_display;
        memcpy(app->data, src, sizeof(uint16_t) * g_adc_buf_sz);
        free_bufs();
        scene_manager_next_scene(app->scene_manager, ScopeSceneSave);
    } else {
        free_bufs();
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool scope_scene_run_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void scope_scene_run_on_exit(void* context) {
    ScopeApp* app = context;
    widget_reset(app->widget);
}
