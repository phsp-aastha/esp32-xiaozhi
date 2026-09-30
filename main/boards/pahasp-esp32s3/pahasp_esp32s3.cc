#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "config.h"

class PahaspEsp32S3 : public WifiBoard {
public:
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
};

DECLARE_BOARD(PahaspEsp32S3);
