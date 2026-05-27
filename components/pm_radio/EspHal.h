#ifndef ESP_HAL_H
#define ESP_HAL_H

// include RadioLib
#include <RadioLib.h>

// ESP-IDF common headers
#include <rom/ets_sys.h>
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "hal/gpio_hal.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "soc/gpio_sig_map.h"
#include "soc/rtc.h"
#include "esp_system.h"
#include "esp_intr_alloc.h"

// Target-specific ROM headers
#if CONFIG_IDF_TARGET_ESP32
  // original ESP32 ROM headers are at the top level
#elif CONFIG_IDF_TARGET_ESP32S2
  #include "esp32s2/rom/ets_sys.h"
  #include "esp32s2/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32S3
  #include "esp32s3/rom/ets_sys.h"
  #include "esp32s3/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32C2
  #include "esp32c2/rom/ets_sys.h"
  #include "esp32c2/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32C3
  #include "esp32c3/rom/ets_sys.h"
  #include "esp32c3/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32C6
  #include "esp32c6/rom/ets_sys.h"
  #include "esp32c6/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32H2
  #include "esp32h2/rom/ets_sys.h"
  #include "esp32h2/rom/gpio.h"
#elif CONFIG_IDF_TARGET_ESP32P4
  #include "esp32p4/rom/ets_sys.h"
  #include "esp32p4/rom/gpio.h"
#else
  #error "Target CONFIG_IDF_TARGET is not supported by EspHal"
#endif

// pin modes
#ifndef INPUT
#define INPUT  0x01
#endif
#ifndef OUTPUT
#define OUTPUT 0x03
#endif

// logic levels
#ifndef LOW
#define LOW  0x0
#endif
#ifndef HIGH
#define HIGH 0x1
#endif

// interrupt modes
#ifndef RISING
#define RISING  0x01
#endif
#ifndef FALLING
#define FALLING 0x02
#endif

class EspHal : public RadioLibHal {
  public:
    EspHal(int8_t sck, int8_t miso, int8_t mosi, spi_host_device_t spi_host = SPI2_HOST)
      : RadioLibHal(INPUT, OUTPUT, LOW, HIGH, RISING, FALLING),
        spiSCK(sck), spiMISO(miso), spiMOSI(mosi), spiHost(spi_host) {}

    void init() override {
      spiBegin();
    }

    void term() override {
      spiEnd();
    }

    void pinMode(uint32_t pin, uint32_t mode) override {
      if (pin == RADIOLIB_NC) { return; }
      gpio_config_t conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
      };
      gpio_config(&conf);
    }

    void digitalWrite(uint32_t pin, uint32_t value) override {
      if (pin == RADIOLIB_NC) { return; }
      gpio_set_level((gpio_num_t)pin, value);
    }

    uint32_t digitalRead(uint32_t pin) override {
      if (pin == RADIOLIB_NC) { return 0; }
      return gpio_get_level((gpio_num_t)pin);
    }

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override {
      if (interruptNum == RADIOLIB_NC) { return; }
      gpio_install_isr_service((int)ESP_INTR_FLAG_IRAM);
      gpio_set_intr_type((gpio_num_t)interruptNum,
        (mode == RISING) ? GPIO_INTR_POSEDGE :
        (mode == FALLING) ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
      gpio_isr_handler_add((gpio_num_t)interruptNum, (gpio_isr_t)interruptCb, NULL);
    }

    void detachInterrupt(uint32_t interruptNum) override {
      if (interruptNum == RADIOLIB_NC) { return; }
      gpio_isr_handler_remove((gpio_num_t)interruptNum);
      gpio_set_intr_type((gpio_num_t)interruptNum, GPIO_INTR_DISABLE);
    }

    void delay(unsigned long ms) override {
      vTaskDelay(ms / portTICK_PERIOD_MS);
    }

    void delayMicroseconds(unsigned long us) override {
      esp_rom_delay_us(us);
    }

    unsigned long millis() override {
      return (unsigned long)(esp_timer_get_time() / 1000ULL);
    }

    unsigned long micros() override {
      return (unsigned long)(esp_timer_get_time());
    }

    long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override {
      if (pin == RADIOLIB_NC) { return 0; }
      this->pinMode(pin, INPUT);
      uint32_t start = this->micros();
      uint32_t curtick = this->micros();
      while (this->digitalRead(pin) == state) {
        if ((this->micros() - curtick) > timeout) { return 0; }
      }
      return (long)(this->micros() - start);
    }

    void spiBegin() {
      if (!initialized) {
        spi_bus_config_t bus_config = {
          .mosi_io_num = spiMOSI,
          .miso_io_num = spiMISO,
          .sclk_io_num = spiSCK,
          .quadwp_io_num = -1,
          .quadhd_io_num = -1,
          .max_transfer_sz = 0,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(spiHost, &bus_config, SPI_DMA_CH_AUTO));

        spi_device_interface_config_t dev_config = {
          .mode = 0,
          .clock_speed_hz = 2000000,
          .spics_io_num = -1,
          .flags = 0,
          .queue_size = 1,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(spiHost, &dev_config, &spiHandle));
        initialized = true;
      }
    }

    void spiBeginTransaction() {}

    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
      spi_transaction_t t = {};
      t.length = 8 * len;
      t.rxlength = 8 * len;
      t.tx_buffer = out;
      t.rx_buffer = in;
      ESP_ERROR_CHECK(spi_device_polling_transmit(spiHandle, &t));
    }

    void spiEndTransaction() {}

    void spiEnd() {
      if (initialized) {
        spi_bus_remove_device(spiHandle);
        spi_bus_free(spiHost);
        initialized = false;
      }
    }

  private:
    int8_t spiSCK;
    int8_t spiMISO;
    int8_t spiMOSI;
    spi_host_device_t spiHost;
    spi_device_handle_t spiHandle = NULL;
    bool initialized = false;
};

#endif // ESP_HAL_H
