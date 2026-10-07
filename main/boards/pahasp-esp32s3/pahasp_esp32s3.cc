#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "config.h"

#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>

#define TAG "PahaspEsp32S3"

// SSD1306 OLED
#define DISPLAY_SDA_PIN     GPIO_NUM_41
#define DISPLAY_SCL_PIN     GPIO_NUM_42
#define DISPLAY_WIDTH       128
#define DISPLAY_HEIGHT      64
#define DISPLAY_MIRROR_X    true
#define DISPLAY_MIRROR_Y    true

class PahaspEsp32S3 : public WifiBoard {
private:
    Display* display_ = nullptr;

    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };

        ESP_ERROR_CHECK(
            i2c_new_master_bus(&bus_config, &display_i2c_bus_)
        );
    }

    void InitializeSsd1306Display() {
        // SSD1306 I2C configuration
        esp_lcd_panel_io_i2c_config_t io_config = {};

        io_config.dev_addr = 0x3C;
        io_config.scl_speed_hz = 400 * 1000;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.on_color_trans_done = nullptr;
        io_config.user_ctx = nullptr;
        io_config.flags.dc_low_on_data = 0;
        io_config.flags.disable_control_phase = 0;

        ESP_ERROR_CHECK(
            esp_lcd_new_panel_io_i2c(
                display_i2c_bus_,
                &io_config,
                &panel_io_
            )
        );

        ESP_LOGI(TAG, "Installing SSD1306 driver");

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = DISPLAY_HEIGHT,
        };

        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(
            esp_lcd_new_panel_ssd1306(
                panel_io_,
                &panel_config,
                &panel_
            )
        );

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(
            esp_lcd_panel_invert_color(panel_, false)
        );
        ESP_ERROR_CHECK(
            esp_lcd_panel_disp_on_off(panel_, true)
        );

        ESP_LOGI(TAG, "SSD1306 display initialized");

        display_ = new OledDisplay(
            panel_io_,
            panel_,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y
        );
    }

public:
    PahaspEsp32S3() {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,

            // MAX98357A speaker
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            I2S_STD_SLOT_LEFT,

            // INMP441 microphone
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN,
            I2S_STD_SLOT_LEFT
        );

        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(PahaspEsp32S3);