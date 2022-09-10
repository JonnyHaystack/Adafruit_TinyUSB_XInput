#include <Adafruit_TinyUSB.h>
#include <Adafruit_USBD_XInput.hpp>
#include <Arduino.h>
#include <TUGamepad.hpp>

Adafruit_USBD_XInput *_xinput;
TUGamepad *_gamepad;

xinput_report_t _report = {};

bool _led = false;
uint8_t _led_clk = 0;

void setup() {
    pinMode(PICO_DEFAULT_LED_PIN, OUTPUT);
    digitalWrite(PICO_DEFAULT_LED_PIN, 0);

    // Serial.begin(115200);
    Serial.end();

    TinyUSBDevice.setID(0x045E, 0x028E);
    TinyUSBDevice.setManufacturerDescriptor("Microsoft");
    TinyUSBDevice.setProductDescriptor("XInput STANDARD GAMEPAD");
    TinyUSBDevice.setSerialDescriptor("1.0");

    _xinput = new Adafruit_USBD_XInput();
    // TUGamepad::registerDescriptor();
    // _gamepad = new TUGamepad();
    _xinput->begin();
    // _gamepad->begin();
    // Serial.begin(115200);
    // while (!_gamepad->ready()) {
    //     busy_wait_ms(1);
    // }
}

void loop() {
    _report.buttons1 = ~_report.buttons1;
    while (!_xinput->ready()) {
        tight_loop_contents();
    }
    _xinput->sendReport(&_report);

    // while (!_gamepad->ready()) {
    //     tight_loop_contents();
    // }
    // _gamepad->sendState();

    _led_clk++;
    if (_led_clk > 5) {
        _led = !_led;
        digitalWrite(PICO_DEFAULT_LED_PIN, _led);
        _led_clk = 0;
    }

    // put your main code here, to run repeatedly:
}