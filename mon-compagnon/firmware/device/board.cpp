// SPDX-License-Identifier: GPL-3.0-only
// SSD1677 initialization adapted from FolloUp Sticky at
// 1682b7e7d7ff9c6b23ab7e903c44b241f095b402, components/epaper_panel/.
// See docs/device-board-sources.md and vendor/FolloUp-LICENSE.txt.
#include "board.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

namespace compagnon::device {
namespace {
constexpr gpio_num_t kCs = GPIO_NUM_10, kDc = GPIO_NUM_9;
constexpr gpio_num_t kReset = GPIO_NUM_46, kBusy = GPIO_NUM_3;
constexpr std::size_t kChunk = 1024;
spi_device_handle_t screen = nullptr;
uint8_t* staging = nullptr;
uint8_t* previous = nullptr;
bool base_valid = false;
sdmmc_card_t* card = nullptr;
bool ready = false;

void pauseMs(uint32_t ms) {
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(ms)));
}
bool check(esp_err_t result, const char* context, std::string& error) {
    if (result == ESP_OK) return true;
    error = std::string(context) + ": " + esp_err_to_name(result);
    return false;
}
esp_err_t bytes(const uint8_t* data, std::size_t size) {
    while (size) {
        const std::size_t count = std::min(size, kChunk);
        std::memcpy(staging, data, count);
        spi_transaction_t tx = {};
        tx.length = count * 8;
        tx.tx_buffer = staging;
        const esp_err_t result = spi_device_polling_transmit(screen, &tx);
        if (result != ESP_OK) return result;
        data += count;
        size -= count;
    }
    return ESP_OK;
}
esp_err_t command(uint8_t cmd, const uint8_t* data = nullptr, std::size_t size = 0) {
    gpio_set_level(kCs, 0);
    gpio_set_level(kDc, 0);
    esp_err_t result = bytes(&cmd, 1);
    if (result == ESP_OK && size) {
        gpio_set_level(kDc, 1);
        result = bytes(data, size);
    }
    gpio_set_level(kCs, 1);
    return result;
}
esp_err_t busy() {
    const int64_t start = esp_timer_get_time();
    while (gpio_get_level(kBusy) == 1) {
        if (esp_timer_get_time() - start >= 10000000LL) return ESP_ERR_TIMEOUT;
        pauseMs(5);
    }
    return ESP_OK;
}
esp_err_t cursor() {
    const uint8_t x[] = {0, 0};
    const uint8_t y[] = {0xDF, 0x01};  // 479
    esp_err_t result = command(0x4E, x, sizeof(x));
    return result == ESP_OK ? command(0x4F, y, sizeof(y)) : result;
}
esp_err_t initializePanel() {
    pauseMs(10);
    gpio_set_level(kReset, 0);
    pauseMs(2);
    gpio_set_level(kReset, 1);
    pauseMs(50);
    esp_err_t result = busy();
    if (result != ESP_OK) return result;
    result = command(0x12);
    if (result != ESP_OK) return result;
    result = busy();
    if (result != ESP_OK) return result;
    const uint8_t booster[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    const uint8_t output[] = {0xDF, 0x01, 0x02};
    const uint8_t entry[] = {0x01};
    const uint8_t x[] = {0x00, 0x00, 0x1F, 0x03};  // 0..799
    const uint8_t y[] = {0xDF, 0x01, 0x00, 0x00};  // 479..0
    const uint8_t border[] = {0x01}, temperature[] = {0x80};
    struct Step { uint8_t cmd; const uint8_t* data; std::size_t size; };
    const Step steps[] = {{0x0C, booster, sizeof(booster)},
        {0x01, output, sizeof(output)}, {0x11, entry, sizeof(entry)},
        {0x44, x, sizeof(x)}, {0x45, y, sizeof(y)},
        {0x3C, border, sizeof(border)}, {0x18, temperature, sizeof(temperature)}};
    for (const auto& step : steps) {
        result = command(step.cmd, step.data, step.size);
        if (result != ESP_OK) return result;
    }
    return cursor();
}
esp_err_t initializePartial() {
    // Pinned InitPartial: retain the established base and never reset the IC.
    const uint8_t output[] = {0xDF, 0x01, 0x02}, entry[] = {0x01};
    const uint8_t x[] = {0x00, 0x00, 0x1F, 0x03}, y[] = {0xDF, 0x01, 0x00, 0x00};
    const uint8_t border[] = {0x80}, temperature[] = {0x80};
    struct Step { uint8_t cmd; const uint8_t* data; std::size_t size; };
    const Step steps[] = {{0x01, output, sizeof(output)}, {0x11, entry, sizeof(entry)},
        {0x44, x, sizeof(x)}, {0x45, y, sizeof(y)},
        {0x3C, border, sizeof(border)}, {0x18, temperature, sizeof(temperature)}};
    for (const auto& step : steps) {
        const auto result = command(step.cmd, step.data, step.size);
        if (result != ESP_OK) return result;
    }
    return cursor();
}
}  // namespace

bool beginBoard(std::string& error) {
    error.clear();
    if (ready) return true;
    gpio_config_t buttons = {};
    buttons.pin_bit_mask = (1ULL << kButtonUp) | (1ULL << kButtonSelect) |
                          (1ULL << kButtonDown) | (1ULL << kButtonBoot);
    buttons.mode = GPIO_MODE_INPUT;
    buttons.pull_up_en = GPIO_PULLUP_ENABLE;
    if (!check(gpio_config(&buttons), "Boutons", error)) return false;
    gpio_config_t out = {};
    out.pin_bit_mask = (1ULL << kCs) | (1ULL << kDc) | (1ULL << kReset);
    out.mode = GPIO_MODE_OUTPUT;
    if (!check(gpio_config(&out), "GPIO ecran", error)) return false;
    gpio_set_level(kCs, 1);
    gpio_set_level(kDc, 0);
    gpio_set_level(kReset, 1);
    gpio_config_t in = {};
    in.pin_bit_mask = 1ULL << kBusy;
    in.mode = GPIO_MODE_INPUT;
    in.pull_up_en = GPIO_PULLUP_ENABLE;
    if (!check(gpio_config(&in), "BUSY ecran", error)) return false;
    if (!staging) staging = static_cast<uint8_t*>(heap_caps_malloc(
        kChunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (!staging) { error = "Memoire transfert ecran insuffisante"; return false; }
    if (!previous) previous = static_cast<uint8_t*>(heap_caps_malloc(
        kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!previous) { error = "Memoire image precedente insuffisante"; return false; }
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_12;
    bus.miso_io_num = -1;
    bus.sclk_io_num = GPIO_NUM_11;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kChunk;
    // Refuse an occupied bus: its pin mapping cannot safely be assumed.
    if (!check(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO), "SPI3", error)) return false;
    spi_device_interface_config_t device = {};
    device.clock_speed_hz = 20000000;
    device.mode = 0;
    device.spics_io_num = -1;
    device.queue_size = 1;
    const esp_err_t result = spi_bus_add_device(SPI3_HOST, &device, &screen);
    if (result != ESP_OK) {
        spi_bus_free(SPI3_HOST);
        return check(result, "Peripherique ecran", error);
    }
    ready = true;
    return true;
}

bool mountNotes(std::string& error) {
    error.clear();
    if (!card) {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.width = 4;
        slot.clk = GPIO_NUM_16;
        slot.cmd = GPIO_NUM_17;
        slot.d0 = GPIO_NUM_15;
        slot.d1 = GPIO_NUM_7;
        slot.d2 = GPIO_NUM_8;
        slot.d3 = GPIO_NUM_18;
        slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        esp_vfs_fat_sdmmc_mount_config_t config = {};
        config.format_if_mount_failed = false;
        config.max_files = 8;
        config.allocation_unit_size = 16 * 1024;
        if (!check(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &config, &card),
                   "Montage SD sans formatage", error)) return false;
    }
    if (mkdir(kNotesDirectory, 0700) != 0 && errno != EEXIST) {
        error = "Creation repertoire notes: " + std::string(std::strerror(errno));
        return false;
    }
    struct stat st = {};
    if (stat(kNotesDirectory, &st) != 0 || !S_ISDIR(st.st_mode)) {
        error = "Le chemin notes n'est pas un repertoire accessible";
        return false;
    }
    return true;
}

bool display(const uint8_t* frame, std::string& error, bool fast) {
    error.clear();
    if (!ready || !frame) { error = "Ecran non initialise ou image absente"; return false; }
    // A known glass shadow is necessary before differential drive. After any
    // transfer/BUSY failure it is invalidated so the next attempt restores a base.
    if (fast && base_valid && std::memcmp(previous, frame, kFrameBytes) == 0) return true;
    const bool differential = fast && base_valid;
    base_valid = false;
    if (!differential && !check(initializePanel(), "Initialisation SSD1677", error)) return false;
    if (differential) {
        // Pinned InitPartial + RefreshPartialFullScreen. The SSD1677 drive is
        // whole-panel; RAM windows cannot limit activation to a dirty rectangle.
        // Rewrite full OLD/NEW planes so previous changed regions cannot recur.
        if (!check(initializePartial(), "Initialisation differentielle", error) ||
            !check(command(0x26, previous, kFrameBytes), "Ancienne image ecran", error)) return false;
    }
    if (!check(cursor(), "Curseur nouvelle image", error) ||
        !check(command(0x24, frame, kFrameBytes), "Image ecran", error)) return false;
    if (!differential) {
        if (!check(cursor(), "Curseur base ecran", error) ||
            !check(command(0x26, frame, kFrameBytes), "Base ecran", error)) return false;
    }
    // Normal OTP F7 only for startup, explicit cleaning or recovery after error.
    // FF is the verified mode-2 old/new differential waveform, no invented LUT.
    const uint8_t update[] = {static_cast<uint8_t>(differential ? 0xFF : 0xF7)};
    if (!check(command(0x22, update, sizeof(update)), "Commande rafraichissement", error) ||
        !check(command(0x20), "Activation ecran", error) ||
        !check(busy(), "Attente ecran (10 s maximum)", error)) return false;
    std::memcpy(previous, frame, kFrameBytes);
    base_valid = true;
    return true;
}
}  // namespace compagnon::device
