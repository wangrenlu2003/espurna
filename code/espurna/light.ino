/*

LIGHT MODULE

Copyright (C) 2016-2017 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
#ifndef LIGHT_PROVIDER_EXPERIMENTAL_RGB_ONLY_HSV_IR

#include <Ticker.h>
#include <ArduinoJson.h>
#include <vector>

#if LIGHT_PROVIDER == LIGHT_PROVIDER_DIMMER
#define PWM_CHANNEL_NUM_MAX LIGHT_CHANNELS
extern "C" {
    #include "pwm.h"
}
#endif

Ticker colorTicker;
typedef struct {
    unsigned char pin;
    bool reverse;
    unsigned char value;
    unsigned char shadow;
} channel_t;
std::vector<channel_t> _channels;
bool _lightState = false;
unsigned int _brightness = LIGHT_MAX_BRIGHTNESS;

#if LIGHT_PROVIDER == LIGHT_PROVIDER_MY9192
#include <my9291.h>
my9291 * _my9291;
#endif

// Gamma Correction lookup table (8 bit)
// TODO: move to PROGMEM
const unsigned char gamma_table[] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,
    3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,
    6,   7,   7,   7,   7,   8,   8,   8,   9,   9,   9,   10,  10,  11,  11,  11,
    12,  12,  13,  13,  14,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,
    19,  20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,
    29,  30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,
    41,  42,  43,  43,  44,  45,  46,  47,  48,  49,  50,  50,  51,  52,  53,  54,
    55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  71,
    72,  73,  74,  75,  76,  77,  78,  80,  81,  82,  83,  84,  86,  87,  88,  89,
    91,  92,  93,  94,  96,  97,  98,  100, 101, 102, 104, 105, 106, 108, 109, 110,
    112, 113, 115, 116, 118, 119, 121, 122, 123, 125, 126, 128, 130, 131, 133, 134,
    136, 137, 139, 140, 142, 144, 145, 147, 149, 150, 152, 154, 155, 157, 159, 160,
    162, 164, 166, 167, 169, 171, 173, 175, 176, 178, 180, 182, 184, 186, 187, 189,
    191, 193, 195, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
    223, 225, 227, 229, 231, 233, 235, 238, 240, 242, 244, 246, 248, 251, 253, 255
};

// -----------------------------------------------------------------------------
// UTILS
// -----------------------------------------------------------------------------

void _fromRGB(const char * rgb) {

    char * p = (char *) rgb;
    if (strlen(p) == 0) return;

    // if color begins with a # then assume HEX RGB
    if (p[0] == '#') {

        if (lightHasColor()) {

            ++p;
            unsigned long value = strtoul(p, NULL, 16);

            // RGBA values are interpreted like RGB + brightness
            if (strlen(p) > 7) {
                _channels[0].value = (value >> 24) & 0xFF;
                _channels[1].value = (value >> 16) & 0xFF;
                _channels[2].value = (value >> 8) & 0xFF;
                _brightness = (value & 0xFF) * LIGHT_MAX_BRIGHTNESS / 255;
            } else {
                _channels[0].value = (value >> 16) & 0xFF;
                _channels[1].value = (value >> 8) & 0xFF;
                _channels[2].value = (value) & 0xFF;
            }

        }

    // it's a temperature in mireds
    } else if (p[0] == 'M') {

        if (lightHasColor()) {
            unsigned long mireds = atol(p + 1);
            _fromMireds(mireds);
        }

    // it's a temperature in kelvin
    } else if (p[0] == 'K') {

        if (lightHasColor()) {
            unsigned long kelvin = atol(p + 1);
            _fromKelvin(kelvin);
        }

    // otherwise assume decimal values separated by commas
    } else {

        char * tok;
        unsigned char count = 0;
        unsigned char channels = _channels.size();

        tok = strtok(p, ",");
        while (tok != NULL) {
            _channels[count].value = atoi(tok);
            if (++count == channels) break;
            tok = strtok(NULL, ",");
        }

        // RGB but less than 3 values received
        if (lightHasColor() && (count < 3)) {
            _channels[1].value = _channels[0].value;
            _channels[2].value = _channels[0].value;
        }

    }

}

void _toRGB(char * rgb, size_t len, bool applyBrightness) {

    if (!lightHasColor()) return;

    float b = applyBrightness ? (float) _brightness / LIGHT_MAX_BRIGHTNESS : 1;

    unsigned long value = 0;

    value += _channels[0].value * b;
    value <<= 8;
    value += _channels[1].value * b;
    value <<= 8;
    value += _channels[2].value * b;

    snprintf_P(rgb, len, PSTR("#%06X"), value);

}

void _toRGB(char * rgb, size_t len) {
    _toRGB(rgb, len, false);
}

void _toLong(char * color, size_t len, bool applyBrightness) {

    if (!lightHasColor()) return;

    float b = applyBrightness ? (float) _brightness / LIGHT_MAX_BRIGHTNESS : 1;

    snprintf_P(color, len, PSTR("%d,%d,%d"),
        (int) (_channels[0].value * b),
        (int) (_channels[1].value * b),
        (int) (_channels[2].value * b)
    );

}

void _toLong(char * color, size_t len) {
    _toLong(color, len, false);
}

// Thanks to Sacha Telgenhof for sharing this code in his AiLight library
// https://github.com/stelgenhof/AiLight
void _fromKelvin(unsigned long kelvin) {

    // Check we have RGB channels
    if (!lightHasColor()) return;

    // Calculate colors
    unsigned int red = (kelvin <= 66)
        ? LIGHT_MAX_VALUE
        : 329.698727446 * pow((kelvin - 60), -0.1332047592);
    unsigned int green = (kelvin <= 66)
        ? 99.4708025861 * log(kelvin) - 161.1195681661
        : 288.1221695283 * pow(kelvin, -0.0755148492);
    unsigned int blue = (kelvin >= 66)
        ? LIGHT_MAX_VALUE
        : ((kelvin <= 19)
            ? 0
            : 138.5177312231 * log(kelvin - 10) - 305.0447927307);

    // Save values
    _channels[0].value = constrain(red, 0, LIGHT_MAX_VALUE);
    _channels[1].value = constrain(green, 0, LIGHT_MAX_VALUE);
    _channels[2].value = constrain(blue, 0, LIGHT_MAX_VALUE);

}

// Color temperature is measured in mireds (kelvin = 1e6/mired)
void _fromMireds(unsigned long mireds) {
    if (mireds == 0) mireds = 1;
    unsigned long kelvin = constrain(1000000UL / mireds, 1000, 40000) / 100;
    _fromKelvin(kelvin);
}

unsigned int _toPWM(unsigned long value, bool bright, bool gamma, bool reverse) {
    value = constrain(value, 0, LIGHT_MAX_VALUE);
    if (bright) value *= ((float) _brightness / LIGHT_MAX_BRIGHTNESS);
    if (gamma) value = gamma_table[value];
    if (LIGHT_MAX_VALUE != LIGHT_LIMIT_PWM) value = map(value, 0, LIGHT_MAX_VALUE, 0, LIGHT_LIMIT_PWM);
    if (reverse) value = LIGHT_LIMIT_PWM - value;
    return value;
}

// Returns a PWM valule for the given channel ID
unsigned int _toPWM(unsigned char id) {
    if (id < _channels.size()) {
        bool isColor = lightHasColor() && (id < 3);
        bool bright = isColor;
        bool gamma = isColor & (getSetting("useGamma", LIGHT_USE_GAMMA).toInt() == 1);
        return _toPWM(_channels[id].shadow, bright, gamma, _channels[id].reverse);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// PROVIDER
// -----------------------------------------------------------------------------

void _shadow() {

    for (unsigned int i=0; i < _channels.size(); i++) {
        _channels[i].shadow = _lightState ? _channels[i].value : 0;
    }

    if (lightHasColor()) {

        bool useWhite = getSetting("useWhite", LIGHT_USE_WHITE).toInt() == 1;

        if (_lightState && useWhite && (_channels.size() > 3)) {
            if (_channels[0].shadow == _channels[1].shadow  && _channels[1].shadow == _channels[2].shadow ) {
                _channels[3].shadow = _channels[0].shadow * ((float) _brightness / LIGHT_MAX_BRIGHTNESS);
                _channels[2].shadow = 0;
                _channels[1].shadow = 0;
                _channels[0].shadow = 0;
            }
        }

    }

}

void _lightProviderUpdate() {

    _shadow();

    #ifdef LIGHT_ENABLE_PIN
        digitalWrite(LIGHT_ENABLE_PIN, _lightState);
    #endif

    #if LIGHT_PROVIDER == LIGHT_PROVIDER_MY9192

        if (_lightState) {

            unsigned int red = _toPWM(0);
            unsigned int green = _toPWM(1);
            unsigned int blue = _toPWM(2);
            unsigned int white = _toPWM(3)
            unsigned int warm = _toPWM(4);
            _my9291->setColor((my9291_color_t) { red, green, blue, white, warm });
            _my9291->setState(true);

        } else {

            _my9291->setColor((my9291_color_t) { 0, 0, 0, 0, 0 });
            _my9291->setState(false);

        }

    #endif

    #if LIGHT_PROVIDER == LIGHT_PROVIDER_DIMMER

        for (unsigned int i=0; i < _channels.size(); i++) {
            pwm_set_duty(_toPWM(i), i);
        }
        pwm_start();

    #endif

}

// -----------------------------------------------------------------------------
// PERSISTANCE
// -----------------------------------------------------------------------------

void _lightColorSave() {
    for (unsigned int i=0; i < _channels.size(); i++) {
        setSetting("ch", i, _channels[i].value);
    }
    setSetting("brightness", _brightness);
    saveSettings();
}

void _lightColorRestore() {
    for (unsigned int i=0; i < _channels.size(); i++) {
        _channels[i].value = getSetting("ch", i, i==0 ? 255 : 0).toInt();
    }
    _brightness = getSetting("brightness", LIGHT_MAX_BRIGHTNESS).toInt();
    lightUpdate(false, false);
}

// -----------------------------------------------------------------------------
// MQTT
// -----------------------------------------------------------------------------

void _lightMQTTCallback(unsigned int type, const char * topic, const char * payload) {


    if (type == MQTT_CONNECT_EVENT) {

        if (lightHasColor()) {
            mqttSubscribe(MQTT_TOPIC_BRIGHTNESS);
            mqttSubscribe(MQTT_TOPIC_MIRED);
            mqttSubscribe(MQTT_TOPIC_KELVIN);
            mqttSubscribe(MQTT_TOPIC_COLOR);
        }

        char buffer[strlen(MQTT_TOPIC_CHANNEL) + 3];
        snprintf_P(buffer, sizeof(buffer), PSTR("%s/+"), MQTT_TOPIC_CHANNEL);
        mqttSubscribe(buffer);

    }

    if (type == MQTT_MESSAGE_EVENT) {

        // Match topic
        String t = mqttSubtopic((char *) topic);

        // Color temperature in mireds
        if (t.equals(MQTT_TOPIC_MIRED)) {
            _fromMireds(atol(payload));
            lightUpdate(true, mqttForward());
        }

        // Color temperature in kelvins
        if (t.equals(MQTT_TOPIC_KELVIN)) {
            _fromKelvin(atol(payload));
            lightUpdate(true, mqttForward());
        }

        // Color
        if (t.equals(MQTT_TOPIC_COLOR)) {
            lightColor(payload);
            lightUpdate(true, mqttForward());
        }

        // Brightness
        if (t.equals(MQTT_TOPIC_BRIGHTNESS)) {
            _brightness = constrain(atoi(payload), 0, LIGHT_MAX_BRIGHTNESS);
            lightUpdate(true, mqttForward());
        }

        // Channel
        if (t.startsWith(MQTT_TOPIC_CHANNEL)) {
            unsigned int channelID = t.substring(strlen(MQTT_TOPIC_CHANNEL)+1).toInt();
            if (channelID >= _channels.size()) {
                DEBUG_MSG_P(PSTR("[LIGHT] Wrong channelID (%d)\n"), channelID);
                return;
            }
            lightChannel(channelID, atoi(payload));
            lightUpdate(true, mqttForward());
        }

    }

}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

unsigned char lightChannels() {
    return _channels.size();
}

bool lightHasColor() {
    bool useColor = getSetting("useColor", LIGHT_USE_COLOR).toInt() == 1;
    return useColor && (_channels.size() > 2);
}

unsigned char lightWhiteChannels() {
    return _channels.size() % 3;
}

void lightMQTT() {

    char buffer[12];

    if (lightHasColor()) {

        // Color
        if (getSetting("useCSS", LIGHT_USE_CSS).toInt() == 1) {
            _toRGB(buffer, 12, false);
        } else {
            _toLong(buffer, 12, false);
        }
        mqttSend(MQTT_TOPIC_COLOR, buffer);

        // Brightness
        snprintf_P(buffer, sizeof(buffer), PSTR("%d"), _brightness);
        mqttSend(MQTT_TOPIC_BRIGHTNESS, buffer);

    }

    // Channels
    for (unsigned int i=0; i < _channels.size(); i++) {
        snprintf_P(buffer, sizeof(buffer), PSTR("%d"), _channels[i].value);
        mqttSend(MQTT_TOPIC_CHANNEL, i, buffer);
    }

}

void lightUpdate(bool save, bool forward) {

    _lightProviderUpdate();

    // Report color & brightness to MQTT broker
    if (forward) lightMQTT();

    // Report color to WS clients (using current brightness setting)
    #if WEB_SUPPORT
    {
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        root["colorVisible"] = 1;
        root["useColor"] = getSetting("useColor", LIGHT_USE_COLOR).toInt() == 1;
        root["useWhite"] = getSetting("useWhite", LIGHT_USE_WHITE).toInt() == 1;
        root["useGamma"] = getSetting("useGamma", LIGHT_USE_GAMMA).toInt() == 1;
        if (lightHasColor()) {
            root["color"] = lightColor();
            root["brightness"] = lightBrightness();
        }
        JsonArray& channels = root.createNestedArray("channels");
        for (unsigned char id=0; id < lightChannels(); id++) {
            channels.add(lightChannel(id));
        }
        String output;
        root.printTo(output);
        wsSend(output.c_str());
    }
    #endif

    #if LIGHT_SAVE_ENABLED
        // Delay saving to EEPROM 5 seconds to avoid wearing it out unnecessarily
        if (save) colorTicker.once(LIGHT_SAVE_DELAY, _lightColorSave);
    #endif

};

#if LIGHT_SAVE_ENABLED == 0
void lightSave() {
    _lightColorSave();
}
#endif

void lightState(bool state) {
    _lightState = state;
}

bool lightState() {
    return _lightState;
}

void lightColor(const char * color) {
    _fromRGB(color);
}

String lightColor() {
    char rgb[8];
    _toRGB(rgb, 8, false);
    return String(rgb);
}

unsigned int lightChannel(unsigned char id) {
    if (id <= _channels.size()) {
        return _channels[id].value;
    }
    return 0;
}

void lightChannel(unsigned char id, unsigned int value) {
    if (id <= _channels.size()) {
        _channels[id].value = constrain(value, 0, LIGHT_MAX_VALUE);
    }
}

unsigned int lightBrightness() {
    return _brightness;
}

void lightBrightness(unsigned int b) {
    _brightness = constrain(b, 0, LIGHT_MAX_BRIGHTNESS);
}

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------

void _lightAPISetup() {

    #if WEB_SUPPORT

        // API entry points (protected with apikey)
        if (lightHasColor()) {

            apiRegister(MQTT_TOPIC_COLOR, MQTT_TOPIC_COLOR,
                [](char * buffer, size_t len) {
                    if (getSetting("useCSS", LIGHT_USE_CSS).toInt() == 1) {
                        _toRGB(buffer, len, false);
                    } else {
                        _toLong(buffer, len, false);
                    }
                },
                [](const char * payload) {
                    lightColor(payload);
                    lightUpdate(true, true);
                }
            );

            apiRegister(MQTT_TOPIC_BRIGHTNESS, MQTT_TOPIC_BRIGHTNESS,
                [](char * buffer, size_t len) {
        			snprintf_P(buffer, len, PSTR("%d"), _brightness);
                },
                [](const char * payload) {
                    lightBrightness(atoi(payload));
                    lightUpdate(true, true);
                }
            );

            apiRegister(MQTT_TOPIC_KELVIN, MQTT_TOPIC_KELVIN,
                [](char * buffer, size_t len) {},
                [](const char * payload) {
                    _fromKelvin(atol(payload));
                    lightUpdate(true, true);
                }
            );

            apiRegister(MQTT_TOPIC_MIRED, MQTT_TOPIC_MIRED,
                [](char * buffer, size_t len) {},
                [](const char * payload) {
                    _fromMireds(atol(payload));
                    lightUpdate(true, true);
                }
            );

        }

        for (unsigned int id=0; id<lightChannels(); id++) {

            char url[15];
            snprintf_P(url, sizeof(url), PSTR("%s/%d"), MQTT_TOPIC_CHANNEL, id);

            char key[10];
            snprintf_P(key, sizeof(key), PSTR("%s%d"), MQTT_TOPIC_CHANNEL, id);

            apiRegister(url, key,
                [id](char * buffer, size_t len) {
    				snprintf_P(buffer, len, PSTR("%d"), lightChannel(id));
                },
                [id](const char * payload) {
                    lightChannel(id, atoi(payload));
                    lightUpdate(true, true);
                }
            );

        }

    #endif // WEB_SUPPORT

}

#if LIGHT_PROVIDER == LIGHT_PROVIDER_DIMMER

unsigned long getIOMux(unsigned long gpio) {
    unsigned long muxes[16] = {
        PERIPHS_IO_MUX_GPIO0_U, PERIPHS_IO_MUX_U0TXD_U, PERIPHS_IO_MUX_GPIO2_U, PERIPHS_IO_MUX_U0RXD_U,
        PERIPHS_IO_MUX_GPIO4_U, PERIPHS_IO_MUX_GPIO5_U, PERIPHS_IO_MUX_SD_CLK_U, PERIPHS_IO_MUX_SD_DATA0_U,
        PERIPHS_IO_MUX_SD_DATA1_U, PERIPHS_IO_MUX_SD_DATA2_U, PERIPHS_IO_MUX_SD_DATA3_U, PERIPHS_IO_MUX_SD_CMD_U,
        PERIPHS_IO_MUX_MTDI_U, PERIPHS_IO_MUX_MTCK_U, PERIPHS_IO_MUX_MTMS_U, PERIPHS_IO_MUX_MTDO_U
    };
    return muxes[gpio];
}

unsigned long getIOFunc(unsigned long gpio) {
    unsigned long funcs[16] = {
        FUNC_GPIO0, FUNC_GPIO1, FUNC_GPIO2, FUNC_GPIO3,
        FUNC_GPIO4, FUNC_GPIO5, FUNC_GPIO6, FUNC_GPIO7,
        FUNC_GPIO8, FUNC_GPIO9, FUNC_GPIO10, FUNC_GPIO11,
        FUNC_GPIO12, FUNC_GPIO13, FUNC_GPIO14, FUNC_GPIO15
    };
    return funcs[gpio];
}

#endif

void lightSetup() {

    #ifdef LIGHT_ENABLE_PIN
        pinMode(LIGHT_ENABLE_PIN, OUTPUT);
    #endif

    #if LIGHT_PROVIDER == LIGHT_PROVIDER_MY9192

        _my9291 = new my9291(MY9291_DI_PIN, MY9291_DCKI_PIN, MY9291_COMMAND, MY9291_CHANNELS);
        for (unsigned char i=0; i<MY9291_CHANNELS; i++) {
            _channels.push_back((channel_t) {0, false, 0});
        }

    #endif

    #if LIGHT_PROVIDER == LIGHT_PROVIDER_DIMMER

        #ifdef LIGHT_CH1_PIN
            _channels.push_back((channel_t) {LIGHT_CH1_PIN, LIGHT_CH1_INVERSE, 0});
        #endif

        #ifdef LIGHT_CH2_PIN
            _channels.push_back((channel_t) {LIGHT_CH2_PIN, LIGHT_CH2_INVERSE, 0});
        #endif

        #ifdef LIGHT_CH3_PIN
            _channels.push_back((channel_t) {LIGHT_CH3_PIN, LIGHT_CH3_INVERSE, 0});
        #endif

        #ifdef LIGHT_CH4_PIN
            _channels.push_back((channel_t) {LIGHT_CH4_PIN, LIGHT_CH4_INVERSE, 0});
        #endif

        #ifdef LIGHT_CH5_PIN
            _channels.push_back((channel_t) {LIGHT_CH5_PIN, LIGHT_CH5_INVERSE, 0});
        #endif

        uint32 pwm_duty_init[PWM_CHANNEL_NUM_MAX];
        uint32 io_info[PWM_CHANNEL_NUM_MAX][3];
        for (unsigned int i=0; i < _channels.size(); i++) {
            pwm_duty_init[i] = 0;
            io_info[i][0] = getIOMux(_channels[i].pin);
            io_info[i][1] = getIOFunc(_channels[i].pin);
            io_info[i][2] = _channels[i].pin;
            pinMode(_channels[i].pin, OUTPUT);
        }
        pwm_init(LIGHT_MAX_PWM, pwm_duty_init, PWM_CHANNEL_NUM_MAX, io_info);
        pwm_start();


    #endif

    DEBUG_MSG_P(PSTR("[LIGHT] LIGHT_PROVIDER = %d\n"), LIGHT_PROVIDER);
    DEBUG_MSG_P(PSTR("[LIGHT] Number of channels: %d\n"), _channels.size());

    _lightColorRestore();
    _lightAPISetup();
    mqttRegister(_lightMQTTCallback);

}

void lightLoop(){
}


#endif // LIGHT_PROVIDER_EXPERIMENTAL_RGB_ONLY_HSV_IR
#endif // LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
