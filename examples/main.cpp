// #include <Adafruit_TinyUSB.h>
#include <Adafruit_USBD_XInput.hpp>
#include <Arduino.h>

Adafruit_USBD_XInput *_xinput;

xinput_report_t _report = {};

bool _led = false;
uint8_t _led_clk = 0;

void setup() {
    pinMode(PICO_DEFAULT_LED_PIN, OUTPUT);
    digitalWrite(PICO_DEFAULT_LED_PIN, 0);

    Serial.end();

    _xinput = new Adafruit_USBD_XInput();
    _xinput->begin();
    Serial.begin(115200);
}

void loop() {
    _report.b = !_report.b;
    _report.a = !_report.a;
    _report.lb = !_report.lb;
    _report.rb = !_report.rb;
    // _report._reserved[0] = ~_report._reserved[0];
    while (!_xinput->ready()) {
        tight_loop_contents();
    }
    _xinput->sendReport(&_report);

    _led_clk++;
    if (_led_clk > 5) {
        _led = !_led;
        digitalWrite(PICO_DEFAULT_LED_PIN, _led);
        _led_clk = 0;
    }
}