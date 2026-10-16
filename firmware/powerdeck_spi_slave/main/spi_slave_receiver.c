#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi_link_config.h"

/* Apollo STM32N657 SPI5 master -> ESP32-C6 SPI2 slave
 *   PF15 (SCK)  -> GPIO6
 *   PF11 (MOSI) -> GPIO7
 *   PA3  (CS)   -> GPIO4
 *   GND         -> GND
 * MISO is not needed. Both ends must use SPI mode 0.
 */
static const char *TAG = "spi_slave_rx";
static TaskHandle_t s_rx_task_handle = NULL;
static const uint8_t s_test_pattern[] = {0x00U, 0x01U, 0x10U, 0x11U};

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

static bool check_test_pattern(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (data[i] != s_test_pattern[i % sizeof(s_test_pattern)]) {
            return false;
        }
    }
    return true;
}

void app_main(void)
{
    uint8_t *rx_buffers[SPI_RX_QUEUE_DEPTH] = {0};
    spi_slave_transaction_t transactions[SPI_RX_QUEUE_DEPTH] = {0};

    s_rx_task_handle = xTaskGetCurrentTaskHandle();

    for (size_t slot = 0; slot < SPI_RX_QUEUE_DEPTH; ++slot) {
        rx_buffers[slot] = heap_caps_aligned_alloc(
            4, SPI_FRAME_SIZE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (rx_buffers[slot] == NULL || !esp_ptr_dma_capable(rx_buffers[slot])) {
            ESP_LOGE(TAG, "DMA allocation failed at slot=%u", (unsigned)slot);
            return;
        }

        memset(rx_buffers[slot], 0, SPI_FRAME_SIZE_BYTES);
        transactions[slot].length = SPI_FRAME_SIZE_BYTES * 8U;
        transactions[slot].rx_buffer = rx_buffers[slot];
        transactions[slot].tx_buffer = NULL;
        transactions[slot].user = (void *)(uintptr_t)slot;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_PIN_MOSI,
        .miso_io_num = SPI_PIN_MISO,
        .sclk_io_num = SPI_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_FRAME_SIZE_BYTES,
    };
    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = SPI_PIN_CS,
        .queue_size = SPI_RX_QUEUE_DEPTH,
        .flags = 0,
        .post_trans_cb = spi_rx_done_isr,
    };

    ESP_ERROR_CHECK(spi_slave_initialize(SPI2_HOST, &buscfg,
                                         &slvcfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG,
             "SPI ready: one-second statistics, size=%u, SCK=%d MOSI=%d CS=%d",
             SPI_FRAME_SIZE_BYTES, SPI_PIN_SCLK, SPI_PIN_MOSI, SPI_PIN_CS);

    for (size_t slot = 0; slot < SPI_RX_QUEUE_DEPTH; ++slot) {
        ESP_ERROR_CHECK(spi_slave_queue_trans(
            SPI2_HOST, &transactions[slot], portMAX_DELAY));
    }

    uint32_t total_frames = 0;
    uint32_t total_ok = 0;
    uint32_t total_errors = 0;
    uint32_t interval_frames = 0;
    uint32_t interval_ok = 0;
    uint32_t interval_errors = 0;
    int64_t report_start_us = esp_timer_get_time();

    while (1) {
        (void)ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

        spi_slave_transaction_t *completed = NULL;
        esp_err_t ret = spi_slave_get_trans_result(
            SPI2_HOST, &completed, portMAX_DELAY);
        if (ret != ESP_OK || completed == NULL) {
            ++total_errors;
            ++interval_errors;
            ESP_LOGE(TAG, "get_trans_result failed: %s", esp_err_to_name(ret));
            continue;
        }

        const size_t slot = (size_t)(uintptr_t)completed->user;
        uint8_t *rx_buffer = rx_buffers[slot];
        const size_t received_bits = completed->trans_len;
        const bool length_ok =
            (received_bits == SPI_FRAME_SIZE_BYTES * 8U);
        const bool pattern_ok = length_ok &&
            check_test_pattern(rx_buffer, SPI_FRAME_SIZE_BYTES);

        ++total_frames;
        ++interval_frames;
        if (pattern_ok) {
            ++total_ok;
            ++interval_ok;
        } else {
            ++total_errors;
            ++interval_errors;
        }

        const int64_t now_us = esp_timer_get_time();
        const int64_t elapsed_us = now_us - report_start_us;
        if (elapsed_us >= SPI_REPORT_INTERVAL_US) {
            const float fps = (float)interval_frames * 1000000.0f /
                              (float)elapsed_us;
            ESP_LOGI(TAG,
                     "1s stats: frames=%lu ok=%lu errors=%lu fps=%.2f | total: frames=%lu ok=%lu errors=%lu",
                     (unsigned long)interval_frames,
                     (unsigned long)interval_ok,
                     (unsigned long)interval_errors,
                     fps,
                     (unsigned long)total_frames,
                     (unsigned long)total_ok,
                     (unsigned long)total_errors);

            interval_frames = 0;
            interval_ok = 0;
            interval_errors = 0;
            report_start_us = now_us;
        }

        memset(rx_buffer, 0, SPI_FRAME_SIZE_BYTES);
        completed->trans_len = 0;
        ESP_ERROR_CHECK(spi_slave_queue_trans(
            SPI2_HOST, completed, portMAX_DELAY));
    }
}
