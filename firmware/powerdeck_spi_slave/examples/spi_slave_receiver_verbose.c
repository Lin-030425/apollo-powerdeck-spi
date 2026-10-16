#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TEST_FRAME_SIZE_BYTES 14900U
#define REPORT_EVERY_FRAMES   10U
#define RX_QUEUE_DEPTH      3U
#define RX_PREVIEW_BYTES     16U

/* Apollo STM32N657 SPI5 master -> ESP32-C6 SPI2 slave
 *   PF15 (SCK)  -> GPIO6
 *   PF11 (MOSI) -> GPIO7
 *   PA3 (CS)    -> GPIO4
 *   GND         -> GND
 * MISO is not needed. Both ends must use SPI mode 0.
 */
#define PIN_NUM_SCLK 6
#define PIN_NUM_MOSI 7
#define PIN_NUM_MISO (-1)
#define PIN_NUM_CS   4

static const char *TAG = "spi_slave_rx";
static TaskHandle_t s_rx_task_handle = NULL;
static const uint8_t s_test_pattern[] = {0x00U, 0x01U, 0x10U, 0x11U};

/* Called by the SPI slave driver *in interrupt context* after a transaction.
 * Never print, allocate memory, or inspect/reuse DMA buffers in this ISR.
 */
static void IRAM_ATTR spi_rx_done_isr(spi_slave_transaction_t *trans)
{
    (void)trans;
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (s_rx_task_handle != NULL) {
        vTaskNotifyGiveFromISR(s_rx_task_handle, &higher_priority_task_woken);
    }
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static bool check_test_pattern(const uint8_t *data, size_t len, size_t *bad_index)
{
    for (size_t i = 0; i < len; ++i) {
        if (data[i] != s_test_pattern[i % sizeof(s_test_pattern)]) {
            if (bad_index != NULL) {
                *bad_index = i;
            }
            return false;
        }
    }
    return true;
}

static bool buffer_is_value(const uint8_t *data, size_t len, uint8_t value)
{
    for (size_t i = 0; i < len; ++i) {
        if (data[i] != value) {
            return false;
        }
    }
    return true;
}

/* Normal task context; safe to format and output logs here.
 * A debugger breakpoint may be placed on the ESP_LOGI line in this function.
 */
static void print_received_data(uint32_t frame, size_t slot,
                                const uint8_t *data, size_t received_bytes)
{
    char preview[RX_PREVIEW_BYTES * 3U + 1U] = {0};
    size_t n = (received_bytes < RX_PREVIEW_BYTES) ? received_bytes : RX_PREVIEW_BYTES;
    for (size_t i = 0; i < n; ++i) {
        (void)snprintf(preview + i * 3U,
                       sizeof(preview) - i * 3U,
                       "%02X ", data[i]);
    }
    ESP_LOGI(TAG, "RX COMPLETE: frame=%lu slot=%u len=%u first_%u=[%s]",
             (unsigned long)frame, (unsigned)slot, (unsigned)received_bytes,
             (unsigned)n, n ? preview : "none");  /* DEBUG BREAKPOINT HERE */
}

void app_main(void)
{
    uint8_t *rx_buffers[RX_QUEUE_DEPTH] = {0};
    spi_slave_transaction_t transactions[RX_QUEUE_DEPTH] = {0};

    /* The receiving task is notified by the SPI ISR. */
    s_rx_task_handle = xTaskGetCurrentTaskHandle();

    for (size_t slot = 0; slot < RX_QUEUE_DEPTH; ++slot) {
        rx_buffers[slot] = heap_caps_aligned_alloc(
            4, TEST_FRAME_SIZE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (rx_buffers[slot] == NULL || !esp_ptr_dma_capable(rx_buffers[slot])) {
            ESP_LOGE(TAG, "DMA allocation failed at slot=%u", (unsigned)slot);
            return;
        }
        memset(rx_buffers[slot], 0, TEST_FRAME_SIZE_BYTES);
        transactions[slot].length = TEST_FRAME_SIZE_BYTES * 8U;
        transactions[slot].rx_buffer = rx_buffers[slot];
        transactions[slot].tx_buffer = NULL;
        transactions[slot].user = (void *)(uintptr_t)slot;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TEST_FRAME_SIZE_BYTES,
    };
    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = RX_QUEUE_DEPTH,
        .flags = 0,
        .post_trans_cb = spi_rx_done_isr,  /* Enable receive-complete ISR */
    };

    ESP_ERROR_CHECK(spi_slave_initialize(SPI2_HOST, &buscfg,
                                         &slvcfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI ready: interrupt callback + task notification, size=%u, SCK=%d MOSI=%d CS=%d",
             TEST_FRAME_SIZE_BYTES, PIN_NUM_SCLK, PIN_NUM_MOSI, PIN_NUM_CS);

    /* Arm all 3 DMA buffers before waiting for any interrupts. */
    for (size_t slot = 0; slot < RX_QUEUE_DEPTH; ++slot) {
        ESP_ERROR_CHECK(spi_slave_queue_trans(
            SPI2_HOST, &transactions[slot], portMAX_DELAY));
    }

    uint32_t frame_count = 0;
    uint32_t ok_count = 0;
    uint32_t error_count = 0;
    uint32_t all_zero_count = 0;
    uint32_t all_ff_count = 0;
    int64_t report_start_us = esp_timer_get_time();

    while (1) {
        /* Block without polling; one ISR notification = one completed transaction.
         * pdFALSE decrements by one so back-to-back notifications aren't lost.
         */
        (void)ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

        spi_slave_transaction_t *completed = NULL;
        esp_err_t ret = spi_slave_get_trans_result(
            SPI2_HOST, &completed, portMAX_DELAY);
        if (ret != ESP_OK || completed == NULL) {
            error_count++;
            ESP_LOGE(TAG, "get_trans_result failed: %s", esp_err_to_name(ret));
            continue;
        }

        /* COMPLETED TRANSACTION: safe place for debugger breakpoint. */
        const size_t slot = (size_t)(uintptr_t)completed->user;
        uint8_t *rx_buffer = rx_buffers[slot];
        const size_t received_bits = completed->trans_len;
        const size_t received_bytes = received_bits / 8U;
        ++frame_count;

        const bool length_ok = (received_bits == TEST_FRAME_SIZE_BYTES * 8U);
        const bool all_zero = length_ok &&
                              buffer_is_value(rx_buffer, TEST_FRAME_SIZE_BYTES, 0x00U);
        const bool all_ff = length_ok &&
                            buffer_is_value(rx_buffer, TEST_FRAME_SIZE_BYTES, 0xFFU);
        if (all_zero) ++all_zero_count;
        if (all_ff) ++all_ff_count;

        size_t bad_index = 0;
        const bool pattern_ok = length_ok &&
                                check_test_pattern(rx_buffer, TEST_FRAME_SIZE_BYTES, &bad_index);
        if (pattern_ok) {
            ++ok_count;
        } else {
            ++error_count;
        }

        /* Print every complete frame; throttle incomplete-frame logs so UART
         * output won't starve the IDLE task and trigger Task Watchdog.
         */
        const bool show = length_ok || frame_count <= 5U ||
                          (frame_count % REPORT_EVERY_FRAMES) == 0U;
        if (show) {
            print_received_data(frame_count, slot, rx_buffer, received_bytes);
        }
        if (!length_ok && show) {
            ESP_LOGW(TAG, "Frame %lu short: received=%u bits (%u full bytes), expected=%u bits",
                     (unsigned long)frame_count, (unsigned)received_bits,
                     (unsigned)received_bytes, TEST_FRAME_SIZE_BYTES * 8U);
        } else if (length_ok && !pattern_ok && show) {
            ESP_LOGW(TAG, "Frame %lu mismatch at byte %u: got=0x%02X expected=0x%02X",
                     (unsigned long)frame_count, (unsigned)bad_index,
                     rx_buffer[bad_index],
                     s_test_pattern[bad_index % sizeof(s_test_pattern)]);
        }

        if ((frame_count % REPORT_EVERY_FRAMES) == 0U) {
            const int64_t now_us = esp_timer_get_time();
            const int64_t elapsed_us = now_us - report_start_us;
            const float fps = (elapsed_us > 0) ?
                ((float)REPORT_EVERY_FRAMES * 1000000.0f / (float)elapsed_us) : 0.0f;
            report_start_us = now_us;
            ESP_LOGI(TAG, "frames=%lu ok=%lu errors=%lu all_zero=%lu all_ff=%lu transaction_fps=%.2f",
                     (unsigned long)frame_count, (unsigned long)ok_count,
                     (unsigned long)error_count, (unsigned long)all_zero_count,
                     (unsigned long)all_ff_count, fps);
        }

        /* The SPI driver returned this completed descriptor to us: safe to
         * reuse its DMA buffer only now. Requeue to receive the next frame.
         */
        memset(rx_buffer, 0, TEST_FRAME_SIZE_BYTES);
        completed->trans_len = 0;
        ESP_ERROR_CHECK(spi_slave_queue_trans(SPI2_HOST, completed, portMAX_DELAY));
    }
}
