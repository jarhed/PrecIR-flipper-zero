#include "precir_ir.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bus.h>
#include <furi_hal_cortex.h>
#include <furi_hal_gpio.h>
#include <furi_hal_infrared.h>
#include <furi_hal_resources.h>
#include <stm32wbxx_ll_tim.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * PrecIR uses a 1.25 MHz optical carrier. The public infrared HAL rejects
 * frequencies above 1 MHz even though the Flipper hardware supports them, so
 * this module owns TIM1 directly while a frame is being transmitted. TIM1
 * CH3N drives the internal IR LED; TIM1 CH1N drives an auto-detected module on
 * PA7. This is the same timer/pin arrangement used by the stock infrared HAL.
 */
#define PRECIR_CARRIER_TIMER TIM1

#define PRECIR_REPEAT_GAP_MS       5U
#define PRECIR_PARAM_REPEATS       15U
#define PRECIR_DATA_REPEATS        4U
#define PRECIR_REFRESH_REPEATS     20U
#define PRECIR_SEGMENT_REPEATS     101U
#define PRECIR_FRAME_SETTLE_MS     50U
#define PRECIR_DATA_PAUSE_INTERVAL 32U

/* These values are gaps between carrier bursts, not mark+gap totals. */
static const uint32_t precir_pp4_gap_us[4] = {
    PRECIR_PP4_SYM0_US,
    PRECIR_PP4_SYM1_US,
    PRECIR_PP4_SYM2_US,
    PRECIR_PP4_SYM3_US,
};

/* PP16's symbol-to-gap mapping is deliberately non-linear. */
static const uint32_t precir_pp16_gap_us[16] = {
    27U,
    51U,
    35U,
    43U,
    147U,
    123U,
    139U,
    131U,
    83U,
    59U,
    75U,
    67U,
    91U,
    115U,
    99U,
    107U,
};

static const uint8_t precir_pp16_header[PRECIR_PP16_HEADER_LEN] = {0x00, 0x00, 0x00, 0x40};

typedef struct {
    PrecIRProtocolMode mode;
    const uint8_t* frame;
    size_t symbol_index;
    size_t symbol_count;
    bool next_is_mark;
    bool finished;
} PrecIRWaveform;

struct PrecIRTransmitter {
    PrecIRProtocolMode mode;
    FuriMutex* operation_mutex;
    volatile bool stop_requested;
    volatile bool busy;

    bool timer_owned;
    FuriHalInfraredTxPin output;
};

static uint8_t precir_waveform_stream_byte(const PrecIRWaveform* waveform, size_t byte_index) {
    if(waveform->mode == PrecIRProtocolPP16) {
        if(byte_index < PRECIR_PP16_HEADER_LEN) {
            return precir_pp16_header[byte_index];
        }
        byte_index -= PRECIR_PP16_HEADER_LEN;
    }

    return waveform->frame[byte_index];
}

static void precir_waveform_init(
    PrecIRWaveform* waveform,
    PrecIRProtocolMode mode,
    const uint8_t* frame,
    size_t frame_len) {
    waveform->mode = mode;
    waveform->frame = frame;
    waveform->symbol_index = 0;
    waveform->next_is_mark = true;
    waveform->finished = false;

    const size_t header_len = (mode == PrecIRProtocolPP16) ? PRECIR_PP16_HEADER_LEN : 0U;
    const size_t symbols_per_byte = (mode == PrecIRProtocolPP16) ? 2U : 4U;
    waveform->symbol_count = (frame_len + header_len) * symbols_per_byte;
}

/**
 * Return one mark or gap from a complete PPM waveform. Every data symbol is
 * mark,gap and the iterator adds the required n+1 closing mark.
 */
static bool precir_waveform_next(PrecIRWaveform* waveform, uint32_t* duration_us, bool* mark) {
    if(waveform->finished) return false;

    if(waveform->next_is_mark) {
        *duration_us = (waveform->mode == PrecIRProtocolPP16) ? PRECIR_PP16_BURST_US :
                                                                PRECIR_PP4_BURST_US;
        *mark = true;

        if(waveform->symbol_index == waveform->symbol_count) {
            waveform->finished = true;
        } else {
            waveform->next_is_mark = false;
        }
        return true;
    }

    const size_t symbols_per_byte = (waveform->mode == PrecIRProtocolPP16) ? 2U : 4U;
    const size_t byte_index = waveform->symbol_index / symbols_per_byte;
    const size_t symbol_in_byte = waveform->symbol_index % symbols_per_byte;
    const uint8_t byte_value = precir_waveform_stream_byte(waveform, byte_index);

    if(waveform->mode == PrecIRProtocolPP16) {
        /* Low nibble first. */
        const uint8_t symbol = (byte_value >> (symbol_in_byte * 4U)) & 0x0FU;
        *duration_us = precir_pp16_gap_us[symbol];
    } else {
        /* Least-significant bit pair first. */
        const uint8_t symbol = (byte_value >> (symbol_in_byte * 2U)) & 0x03U;
        *duration_us = precir_pp4_gap_us[symbol];
    }

    *mark = false;
    waveform->symbol_index++;
    waveform->next_is_mark = true;
    return true;
}

bool precir_ir_self_test(void) {
    static const uint32_t expected_pp4[4] = {61U, 244U, 122U, 183U};
    static const uint32_t expected_pp16[16] = {
        27U,
        51U,
        35U,
        43U,
        147U,
        123U,
        139U,
        131U,
        83U,
        59U,
        75U,
        67U,
        91U,
        115U,
        99U,
        107U,
    };

    if(memcmp(precir_pp4_gap_us, expected_pp4, sizeof(expected_pp4)) != 0) return false;
    if(memcmp(precir_pp16_gap_us, expected_pp16, sizeof(expected_pp16)) != 0) return false;

    /* 0x84 must be encoded as crumbs 0,1,0,2, followed by a closing mark. */
    const uint8_t pp4_byte = 0x84;
    const uint32_t expected_pp4_waveform[9] = {
        40U,
        61U,
        40U,
        244U,
        40U,
        61U,
        40U,
        122U,
        40U,
    };
    PrecIRWaveform waveform;
    precir_waveform_init(&waveform, PrecIRProtocolPP4, &pp4_byte, 1U);
    for(size_t i = 0; i < COUNT_OF(expected_pp4_waveform); i++) {
        uint32_t duration;
        bool mark;
        if(!precir_waveform_next(&waveform, &duration, &mark)) return false;
        if(duration != expected_pp4_waveform[i]) return false;
        if(mark != ((i & 1U) == 0U)) return false;
    }
    {
        uint32_t duration;
        bool mark;
        if(precir_waveform_next(&waveform, &duration, &mark)) return false;
    }

    /* PP16 prepends 00 00 00 40, then sends 0xA5 as nibbles 5,A. */
    const uint8_t pp16_byte = 0xA5;
    const uint8_t expected_symbols[10] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 5U, 10U};
    precir_waveform_init(&waveform, PrecIRProtocolPP16, &pp16_byte, 1U);
    for(size_t i = 0; i < COUNT_OF(expected_symbols); i++) {
        uint32_t duration;
        bool mark;
        if(!precir_waveform_next(&waveform, &duration, &mark) || !mark ||
           duration != PRECIR_PP16_BURST_US) {
            return false;
        }
        if(!precir_waveform_next(&waveform, &duration, &mark) || mark ||
           duration != expected_pp16[expected_symbols[i]]) {
            return false;
        }
    }
    {
        uint32_t duration;
        bool mark;
        if(!precir_waveform_next(&waveform, &duration, &mark) || !mark ||
           duration != PRECIR_PP16_BURST_US) {
            return false;
        }
        if(precir_waveform_next(&waveform, &duration, &mark)) return false;
    }

    return true;
}

static const GpioPin* precir_ir_output_gpio(const PrecIRTransmitter* tx) {
    return (tx->output == FuriHalInfraredTxPinExtPA7) ? &gpio_ext_pa7 : &gpio_infrared_tx;
}

static inline void precir_ir_carrier_set(const PrecIRTransmitter* tx, bool enabled) {
    const uint32_t mode = enabled ? LL_TIM_OCMODE_PWM2 : LL_TIM_OCMODE_FORCED_INACTIVE;
    if(tx->output == FuriHalInfraredTxPinExtPA7) {
        LL_TIM_OC_SetMode(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH1, mode);
    } else {
        LL_TIM_OC_SetMode(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH3, mode);
    }
}

static bool precir_ir_hardware_begin(PrecIRTransmitter* tx) {
    /* Never steal TIM1 from the stock IR HAL or another peripheral user. */
    if(furi_hal_infrared_is_busy() || furi_hal_bus_is_enabled(FuriHalBusTIM1)) return false;

    tx->output = furi_hal_infrared_detect_tx_output();
    if(tx->output != FuriHalInfraredTxPinExtPA7) {
        tx->output = FuriHalInfraredTxPinInternal;
    }

    const uint32_t carrier_ticks =
        (SystemCoreClock + (PRECIR_CARRIER_FREQ / 2U)) / PRECIR_CARRIER_FREQ;
    if(carrier_ticks < 2U || carrier_ticks > UINT16_MAX) return false;

    furi_hal_bus_enable(FuriHalBusTIM1);
    tx->timer_owned = true;

    LL_TIM_DisableCounter(PRECIR_CARRIER_TIMER);
    LL_TIM_DisableAllOutputs(PRECIR_CARRIER_TIMER);
    LL_TIM_SetPrescaler(PRECIR_CARRIER_TIMER, 0U);
    LL_TIM_SetCounterMode(PRECIR_CARRIER_TIMER, LL_TIM_COUNTERMODE_UP);
    LL_TIM_SetClockDivision(PRECIR_CARRIER_TIMER, LL_TIM_CLOCKDIVISION_DIV1);
    LL_TIM_SetRepetitionCounter(PRECIR_CARRIER_TIMER, 0U);
    LL_TIM_EnableARRPreload(PRECIR_CARRIER_TIMER);
    LL_TIM_SetAutoReload(PRECIR_CARRIER_TIMER, carrier_ticks - 1U);
    LL_TIM_SetCounter(PRECIR_CARRIER_TIMER, 0U);
    LL_TIM_DisableIT_UPDATE(PRECIR_CARRIER_TIMER);
    LL_TIM_DisableDMAReq_UPDATE(PRECIR_CARRIER_TIMER);

    if(tx->output == FuriHalInfraredTxPinExtPA7) {
        LL_TIM_OC_SetCompareCH1(PRECIR_CARRIER_TIMER, carrier_ticks / 2U);
        LL_TIM_OC_EnablePreload(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH1);
        LL_TIM_OC_SetPolarity(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH1N, LL_TIM_OCPOLARITY_HIGH);
        LL_TIM_CC_EnableChannel(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH1N);
    } else {
        LL_TIM_OC_SetCompareCH3(PRECIR_CARRIER_TIMER, carrier_ticks / 2U);
        LL_TIM_OC_EnablePreload(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH3);
        LL_TIM_OC_SetPolarity(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH3N, LL_TIM_OCPOLARITY_HIGH);
        LL_TIM_CC_EnableChannel(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH3N);
    }

    precir_ir_carrier_set(tx, false);
    LL_TIM_EnableAllOutputs(PRECIR_CARRIER_TIMER);
    LL_TIM_GenerateEvent_UPDATE(PRECIR_CARRIER_TIMER);
    LL_TIM_ClearFlag_UPDATE(PRECIR_CARRIER_TIMER);

    const GpioPin* output_gpio = precir_ir_output_gpio(tx);
    LL_GPIO_ResetOutputPin(output_gpio->port, output_gpio->pin);
    furi_hal_gpio_init_ex(
        output_gpio, GpioModeAltFunctionPushPull, GpioPullNo, GpioSpeedVeryHigh, GpioAltFn1TIM1);

    LL_TIM_EnableCounter(PRECIR_CARRIER_TIMER);
    return true;
}

static void precir_ir_hardware_end(PrecIRTransmitter* tx) {
    if(!tx->timer_owned) return;

    precir_ir_carrier_set(tx, false);
    LL_TIM_DisableAllOutputs(PRECIR_CARRIER_TIMER);
    if(tx->output == FuriHalInfraredTxPinExtPA7) {
        LL_TIM_CC_DisableChannel(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH1N);
    } else {
        LL_TIM_CC_DisableChannel(PRECIR_CARRIER_TIMER, LL_TIM_CHANNEL_CH3N);
    }
    LL_TIM_DisableCounter(PRECIR_CARRIER_TIMER);

    furi_hal_gpio_init(precir_ir_output_gpio(tx), GpioModeAnalog, GpioPullDown, GpioSpeedLow);
    furi_hal_bus_disable(FuriHalBusTIM1);
    tx->timer_owned = false;
}

static inline void precir_ir_wait_until(uint32_t deadline) {
    while((int32_t)(DWT->CYCCNT - deadline) < 0) {
    }
}

static bool
    precir_ir_transmit_once(PrecIRTransmitter* tx, const uint8_t* frame, size_t frame_len) {
    if(tx->stop_requested) return false;

    PrecIRWaveform waveform;
    precir_waveform_init(&waveform, tx->mode, frame, frame_len);

    uint32_t duration_us;
    bool mark;
    if(!precir_waveform_next(&waveform, &duration_us, &mark) || !mark) return false;

    const uint32_t cycles_per_us = furi_hal_cortex_instructions_per_microsecond();
    FURI_CRITICAL_ENTER();

    precir_ir_carrier_set(tx, true);
    uint32_t deadline = DWT->CYCCNT + duration_us * cycles_per_us;

    uint32_t next_duration_us;
    bool next_mark;
    while(precir_waveform_next(&waveform, &next_duration_us, &next_mark)) {
        /* Build the next item while the current mark/gap is in progress. */
        precir_ir_wait_until(deadline);
        precir_ir_carrier_set(tx, next_mark);
        deadline += next_duration_us * cycles_per_us;
    }

    precir_ir_wait_until(deadline);
    precir_ir_carrier_set(tx, false);
    FURI_CRITICAL_EXIT();

    return !tx->stop_requested;
}

static bool precir_ir_delay_interruptible(PrecIRTransmitter* tx, uint32_t delay_ms) {
    while(delay_ms > 0U) {
        if(tx->stop_requested) return false;
        furi_delay_ms(1U);
        delay_ms--;
    }
    return !tx->stop_requested;
}

static bool precir_ir_transmit_frame_internal(
    PrecIRTransmitter* tx,
    const uint8_t* frame,
    size_t frame_len,
    uint16_t repeats,
    uint16_t delay_ms) {
    if(!frame || frame_len == 0U || frame_len > PRECIR_FRAME_MAX) return false;

    for(uint16_t repeat = 0; repeat < repeats; repeat++) {
        if(!precir_ir_transmit_once(tx, frame, frame_len)) return false;
        if((repeat + 1U) < repeats && !precir_ir_delay_interruptible(tx, delay_ms)) {
            return false;
        }
    }

    return !tx->stop_requested;
}

static bool precir_ir_operation_begin(PrecIRTransmitter* tx) {
    if(!tx || !tx->operation_mutex) return false;
    if(furi_mutex_acquire(tx->operation_mutex, FuriWaitForever) != FuriStatusOk) return false;

    /* A cancel latched after the GUI's reset must never be cleared here. */
    if(tx->stop_requested || tx->busy) {
        furi_mutex_release(tx->operation_mutex);
        return false;
    }

    tx->busy = true;
    __DMB();
    if(!precir_ir_hardware_begin(tx)) {
        tx->busy = false;
        __DMB();
        furi_mutex_release(tx->operation_mutex);
        return false;
    }

    return true;
}

static void precir_ir_operation_end(PrecIRTransmitter* tx) {
    precir_ir_hardware_end(tx);
    tx->busy = false;
    __DMB();
    furi_mutex_release(tx->operation_mutex);
}

static bool precir_ir_plid_valid(const uint8_t plid[4]) {
    return plid && (plid[0] != 0U || plid[1] != 0U || plid[2] != 0U || plid[3] != 0U);
}

PrecIRTransmitter* precir_ir_alloc(void) {
    PrecIRTransmitter* tx = malloc(sizeof(PrecIRTransmitter));
    if(!tx) return NULL;

    memset(tx, 0, sizeof(PrecIRTransmitter));
    tx->mode = PrecIRProtocolPP16;
    tx->operation_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!tx->operation_mutex) {
        free(tx);
        return NULL;
    }

    return tx;
}

void precir_ir_free(PrecIRTransmitter* tx) {
    if(!tx) return;

    /* If a worker is active, make it finish at the next intact frame. */
    precir_ir_stop(tx);
    if(furi_mutex_acquire(tx->operation_mutex, FuriWaitForever) == FuriStatusOk) {
        precir_ir_hardware_end(tx);
        furi_mutex_release(tx->operation_mutex);
    }

    furi_mutex_free(tx->operation_mutex);
    free(tx);
}

void precir_ir_stop(PrecIRTransmitter* tx) {
    if(!tx) return;
    tx->stop_requested = true;
    __DMB();
}

bool precir_ir_reset_cancel(PrecIRTransmitter* tx) {
    if(!tx || tx->busy) return false;

    /* Serialize the reset against operation_begin without waiting behind an
     * active transfer. Recheck busy after taking the mutex to close the race. */
    if(furi_mutex_acquire(tx->operation_mutex, 0U) != FuriStatusOk) return false;
    if(tx->busy) {
        furi_mutex_release(tx->operation_mutex);
        return false;
    }

    tx->stop_requested = false;
    __DMB();
    furi_mutex_release(tx->operation_mutex);
    return true;
}

bool precir_ir_is_busy(const PrecIRTransmitter* tx) {
    return tx && tx->busy;
}

void precir_ir_set_protocol(PrecIRTransmitter* tx, PrecIRProtocolMode mode) {
    if(!tx || (mode != PrecIRProtocolPP4 && mode != PrecIRProtocolPP16)) return;
    if(furi_mutex_acquire(tx->operation_mutex, 0U) != FuriStatusOk) return;
    if(!tx->busy) tx->mode = mode;
    furi_mutex_release(tx->operation_mutex);
}

void precir_ir_transmit_frame(
    PrecIRTransmitter* tx,
    const uint8_t* frame,
    size_t frame_len,
    uint16_t repeats,
    uint16_t delay_ms) {
    if(!tx || !frame || frame_len == 0U || repeats == 0U) return;
    if(!precir_ir_operation_begin(tx)) return;

    precir_ir_transmit_frame_internal(tx, frame, frame_len, repeats, delay_ms);
    precir_ir_operation_end(tx);
}

bool precir_ir_send_image(
    PrecIRTransmitter* tx,
    const uint8_t plid[4],
    const uint8_t* image_data,
    size_t image_len,
    uint16_t width,
    uint16_t height,
    uint8_t compression,
    uint8_t page,
    void (*progress_cb)(uint16_t current, uint16_t total, void* ctx),
    void* progress_ctx) {
    if(!tx || !precir_ir_plid_valid(plid) || !image_data || image_len == 0U ||
       image_len > UINT16_MAX || width == 0U || height == 0U ||
       (compression != 0U && compression != 2U) || page > 15U) {
        return false;
    }
    if(!precir_ir_operation_begin(tx)) return false;

    bool success = true;
    uint8_t frame_buf[PRECIR_FRAME_MAX];
    size_t frame_len = precir_build_wake_frame(frame_buf, plid);

    /* 400 is the exact number of complete wake frames sent. */
    success = precir_ir_transmit_frame_internal(
        tx, frame_buf, frame_len, PRECIR_WAKE_REPEATS, PRECIR_REPEAT_GAP_MS);
    if(success) success = precir_ir_delay_interruptible(tx, PRECIR_FRAME_SETTLE_MS);

    if(success) {
        frame_len = precir_build_params_frame(
            frame_buf, plid, (uint16_t)image_len, compression, page, width, height);
        success = precir_ir_transmit_frame_internal(
            tx, frame_buf, frame_len, PRECIR_PARAM_REPEATS, PRECIR_REPEAT_GAP_MS);
    }
    if(success) success = precir_ir_delay_interruptible(tx, PRECIR_FRAME_SETTLE_MS);

    const uint16_t total_frames =
        (uint16_t)((image_len + PRECIR_DATA_PER_FRAME - 1U) / PRECIR_DATA_PER_FRAME);
    for(uint16_t index = 0; success && index < total_frames; index++) {
        const size_t offset = (size_t)index * PRECIR_DATA_PER_FRAME;
        size_t chunk_len = image_len - offset;
        if(chunk_len > PRECIR_DATA_PER_FRAME) chunk_len = PRECIR_DATA_PER_FRAME;

        frame_len =
            precir_build_data_frame(frame_buf, plid, index, image_data + offset, chunk_len);
        success = precir_ir_transmit_frame_internal(
            tx, frame_buf, frame_len, PRECIR_DATA_REPEATS, PRECIR_REPEAT_GAP_MS);

        if(success && progress_cb) progress_cb(index + 1U, total_frames, progress_ctx);
        if(success && ((index + 1U) % PRECIR_DATA_PAUSE_INTERVAL) == 0U &&
           (index + 1U) < total_frames) {
            success = precir_ir_delay_interruptible(tx, 1U);
        }
    }

    if(success) {
        frame_len = precir_build_refresh_frame(frame_buf, plid);
        success = precir_ir_transmit_frame_internal(
            tx, frame_buf, frame_len, PRECIR_REFRESH_REPEATS, PRECIR_REPEAT_GAP_MS);
    }

    precir_ir_operation_end(tx);
    return success;
}

bool precir_ir_send_segment(
    PrecIRTransmitter* tx,
    const uint8_t plid[4],
    const uint8_t bitmap[PRECIR_SEGMENT_BITMAP]) {
    if(!tx || !precir_ir_plid_valid(plid) || !bitmap) return false;
    if(!precir_ir_operation_begin(tx)) return false;

    /* Known segment labels use legacy PP4 regardless of the image setting. */
    const PrecIRProtocolMode saved_mode = tx->mode;
    tx->mode = PrecIRProtocolPP4;

    uint8_t frame_buf[PRECIR_FRAME_MAX];
    const size_t frame_len = precir_build_segment_frame(frame_buf, plid, bitmap);
    const bool success = precir_ir_transmit_frame_internal(
        tx, frame_buf, frame_len, PRECIR_SEGMENT_REPEATS, PRECIR_REPEAT_GAP_MS);

    tx->mode = saved_mode;
    precir_ir_operation_end(tx);
    return success;
}
