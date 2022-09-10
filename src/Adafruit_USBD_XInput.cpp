/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "device/usbd_pvt.h"
#include "tusb_option.h"

#if TUSB_OPT_DEVICE_ENABLED && CFG_TUD_HID

#include "Adafruit_USBD_XInput.hpp"

// TODO multiple instances
static Adafruit_USBD_XInput *_xinput_dev = NULL;

#ifdef ARDUINO_ARCH_ESP32
static uint16_t xinput_load_descriptor(uint8_t *dst, uint8_t *itf) {
    // uint8_t str_index = tinyusb_add_string_descriptor("TinyUSB XInput");
    uint8_t str_index = 0;

    uint8_t ep_in = tinyusb_get_free_in_endpoint();
    uint8_t ep_out = tinyusb_get_free_out_endpoint();
    TU_VERIFY(ep_in && ep_out);
    ep_in |= EPIN;

    const uint8_t descriptor[TUD_XINPUT_DESC_LEN] = {
        // Interface number, string index, EP Out & EP In address, EP size
        TUD_XINPUT_DESCRIPTOR(*itf, str_index, ep_out, ep_in, EPSIZE, 1)
    };

    *itf += 1;
    memcpy(dst, descriptor, TUD_XINPUT_DESC_LEN);
    return TUD_XINPUT_DESC_LEN;
}
#endif

//------------- IMPLEMENTATION -------------//

Adafruit_USBD_XInput::Adafruit_USBD_XInput(uint8_t interval_ms) {
    _interval_ms = interval_ms;

#ifdef ARDUINO_ARCH_ESP32
    // ESP32 requires setup configuration descriptor within constructor
    _xinput_dev = this;
    const uint16_t desc_len = getInterfaceDescriptor(0, NULL, 0);
    tinyusb_enable_interface(USB_INTERFACE_VENDOR, desc_len, xinput_load_descriptor);
#endif
}

uint16_t Adafruit_USBD_XInput::getInterfaceDescriptor(
    uint8_t itfnum,
    uint8_t *buf,
    uint16_t bufsize
) {
    // usb core will automatically update endpoint number
    const uint8_t desc[] = { TUD_XINPUT_DESCRIPTOR(itfnum, 0, EPOUT, EPIN, EPSIZE, _interval_ms) };
    const uint16_t len = sizeof(desc);

    if (bufsize < len) {
        return 0;
    }

    memcpy(buf, desc, len);
    return len;
}

bool Adafruit_USBD_XInput::begin(void) {
    // TinyUSBDevice._desc_device.bDeviceClass = 0xEF;
    // TinyUSBDevice._desc_device.bDeviceSubClass = 0x02;
    // TinyUSBDevice._desc_device.bDeviceProtocol = 0x01;
    if (!TinyUSBDevice.addInterface(*this)) {
        return false;
    }

    _xinput_dev = this;
    return true;
}

bool Adafruit_USBD_XInput::ready(void) {
    return tud_xinput_ready();
}

bool Adafruit_USBD_XInput::sendReport(xinput_report_t *report) {
    return send_xinput_report(report);
}

bool tud_xinput_ready() {
    return tud_ready() && _xinput_dev && _xinput_dev->_endpoint_in != 0 &&
           !usbd_edpt_busy(TUD_OPT_RHPORT, _xinput_dev->_endpoint_in);
}

void receive_xinput_report(void) {
    if (!_xinput_dev) {
        return;
    }

    if (tud_ready() && (_xinput_dev->_endpoint_out != 0) &&
        (!usbd_edpt_busy(0, _xinput_dev->_endpoint_out))) {
        // Take control of OUT endpoint
        usbd_edpt_claim(0, _xinput_dev->_endpoint_out);
        // Retrieve report buffer
        usbd_edpt_xfer(0, _xinput_dev->_endpoint_out, _xinput_dev->_xinput_out_buffer, EPSIZE);
        // Release control of OUT endpoint
        usbd_edpt_release(0, _xinput_dev->_endpoint_out);
    }
}

bool send_xinput_report(xinput_report_t *report) {
    bool sent = false;

    if (!_xinput_dev) {
        return false;
    }

    // Is the device ready and is the IN endpoint available?
    if (tud_xinput_ready()) {
        // Take control of IN endpoint
        usbd_edpt_claim(0, _xinput_dev->_endpoint_in);
        // Send report buffer
        usbd_edpt_xfer(0, _xinput_dev->_endpoint_in, (uint8_t *)report, sizeof(xinput_report_t));
        // Release control of IN endpoint
        usbd_edpt_release(0, _xinput_dev->_endpoint_in);
        sent = true;
    }

    return sent;
}

static void xinput_init(void) {}

static void xinput_reset(uint8_t rhport) {
    (void)rhport;
}

uint16_t xinput_open(
    uint8_t rhport,
    const tusb_desc_interface_t *itf_descriptor,
    uint16_t max_length
) {
    if (itf_descriptor->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        itf_descriptor->bInterfaceSubClass != XINPUT_SUBCLASS_DEFAULT ||
        itf_descriptor->bInterfaceProtocol != XINPUT_PROTOCOL_DEFAULT) {
        return false;
    }

    uint16_t driver_length = sizeof(tusb_desc_interface_t) +
                             (itf_descriptor->bNumEndpoints * sizeof(tusb_desc_endpoint_t)) + 16;

    TU_VERIFY(max_length >= driver_length, 0);

    const uint8_t *current_descriptor = tu_desc_next(itf_descriptor);
    uint8_t found_endpoints = 0;
    while ((found_endpoints < itf_descriptor->bNumEndpoints) && (driver_length <= max_length)) {
        const tusb_desc_endpoint_t *endpoint_descriptor =
            (const tusb_desc_endpoint_t *)current_descriptor;
        if (TUSB_DESC_ENDPOINT == tu_desc_type(endpoint_descriptor)) {
            TU_ASSERT(usbd_edpt_open(rhport, endpoint_descriptor));

            if (tu_edpt_dir(endpoint_descriptor->bEndpointAddress) == TUSB_DIR_IN)
                _xinput_dev->_endpoint_in = endpoint_descriptor->bEndpointAddress;
            else
                _xinput_dev->_endpoint_out = endpoint_descriptor->bEndpointAddress;

            ++found_endpoints;
        }

        current_descriptor = tu_desc_next(current_descriptor);
    }
    return driver_length;
}

bool xinput_control_xfer_callback(
    uint8_t rhport,
    uint8_t stage,
    const tusb_control_request_t *request
) {
    (void)rhport;
    (void)stage;
    (void)request;

    return true;
}

bool xinput_xfer_callback(
    uint8_t rhport,
    uint8_t ep_addr,
    xfer_result_t result,
    uint32_t xferred_bytes
) {
    (void)rhport;
    (void)result;
    (void)xferred_bytes;

    if (ep_addr == _xinput_dev->_endpoint_out)
        usbd_edpt_xfer(0, _xinput_dev->_endpoint_out, _xinput_dev->_xinput_out_buffer, EPSIZE);

    return true;
}

//------------- TinyUSB callbacks -------------//
extern "C" {

const usbd_class_driver_t xinput_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "XINPUT",
#endif
    .init = xinput_init,
    .reset = xinput_reset,
    .open = xinput_open,
    .control_xfer_cb = xinput_control_xfer_callback,
    .xfer_cb = xinput_xfer_callback,
    .sof = NULL
};

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &xinput_driver;
}

} // extern "C"

#endif // TUSB_OPT_DEVICE_ENABLED
