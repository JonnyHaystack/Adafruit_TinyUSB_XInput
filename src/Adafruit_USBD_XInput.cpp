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

#include "Adafruit_USBD_XInput.hpp"

#include "device/usbd_pvt.h"
#include "tusb_option.h"

enum {
    VENDOR_REQUEST_MICROSOFT = 1, // bRequest value to be used by control transfers
};

// TODO multiple instances
static Adafruit_USBD_XInput *_xinput_dev = NULL;

#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

#define MS_OS_20_DESC_LEN 0xB2

// BOS Descriptor is required for automatic driver instalaltion
const uint8_t desc_bos[] = {
    // total length, number of device caps
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    // Microsoft OS 2.0 descriptor
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)
};

// clang-format off

uint8_t desc_ms_os_20[MS_OS_20_DESC_LEN] = {
    // Set header: length, type, windows version, total length
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    // Configuration subset header: length, type, configuration index, reserved,
    // configuration total length
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

    // Function Subset header: length, type, first interface, reserved, subset
    // length
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    0 /*itf num*/, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

    // MS OS 2.0 Compatible ID descriptor: length, type, compatible ID, sub
    // compatible ID
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'X',
    'U', 'S', 'B', '2', '0', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, // sub-compatible

    // MS OS 2.0 Registry property descriptor: length, type
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY), U16_TO_U8S_LE(0x0007),
    U16_TO_U8S_LE(0x002A), // wPropertyDataType, wPropertyNameLength and
                           // PropertyName "DeviceInterfaceGUIDs\0" in UTF-16
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00,
    'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00,
    'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00,
    0x00,
    U16_TO_U8S_LE(0x0050), // wPropertyDataLength
    // bPropertyData: "{8D90842C-1594-41CE-AA3F-62D464E1BE79}".
    '{', 0x00, '8', 0x00, 'D', 0x00, '9', 0x00, '0', 0x00, '8', 0x00, '4', 0x00,
    '2', 0x00, 'C', 0x00, '-', 0x00, '1', 0x00, '5', 0x00, '9', 0x00, '4', 0x00,
    '-', 0x00, '4', 0x00, '1', 0x00, 'C', 0x00, 'E', 0x00, '-', 0x00, 'A', 0x00,
    'A', 0x00, '3', 0x00, 'F', 0x00, '-', 0x00, '6', 0x00, '2', 0x00, 'D', 0x00,
    '4', 0x00, '6', 0x00, '4', 0x00, 'E', 0x00, '1', 0x00, 'B', 0x00, 'E', 0x00,
    '7', 0x00, '9', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00};

// clang-format on

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

    // update the bFirstInterface in MS OS 2.0 descriptor
    // that is bound to XUSB driver
    desc_ms_os_20[0x0a + 0x08 + 4] = itfnum;

    return len;
}

bool Adafruit_USBD_XInput::begin(void) {
    if (!TinyUSBDevice.addInterface(*this)) {
        return false;
    }

    TinyUSBDevice.setVersion(0x0210);

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
    return _xinput_dev && _xinput_dev->_endpoint_in && tud_ready() &&
           !usbd_edpt_busy(TUD_OPT_RHPORT, _xinput_dev->_endpoint_in);
}

void receive_xinput_report(void) {
    if (_xinput_dev && _xinput_dev->_endpoint_out && tud_ready() &&
        !usbd_edpt_busy(TUD_OPT_RHPORT, _xinput_dev->_endpoint_out)) {
        // Take control of OUT endpoint
        usbd_edpt_claim(TUD_OPT_RHPORT, _xinput_dev->_endpoint_out);
        // Retrieve report buffer
        usbd_edpt_xfer(
            TUD_OPT_RHPORT,
            _xinput_dev->_endpoint_out,
            _xinput_dev->_xinput_out_buffer,
            EPSIZE
        );
        // Release control of OUT endpoint
        usbd_edpt_release(TUD_OPT_RHPORT, _xinput_dev->_endpoint_out);
    }
}

bool send_xinput_report(xinput_report_t *report) {
    bool sent = false;

    // Is the device ready and is the IN endpoint available?
    if (tud_xinput_ready()) {
        // Take control of IN endpoint
        usbd_edpt_claim(TUD_OPT_RHPORT, _xinput_dev->_endpoint_in);
        // Send report buffer
        usbd_edpt_xfer(
            TUD_OPT_RHPORT,
            _xinput_dev->_endpoint_in,
            (uint8_t *)report,
            sizeof(xinput_report_t)
        );
        // Release control of IN endpoint
        usbd_edpt_release(TUD_OPT_RHPORT, _xinput_dev->_endpoint_in);
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
        usbd_edpt_xfer(
            TUD_OPT_RHPORT,
            _xinput_dev->_endpoint_out,
            _xinput_dev->_xinput_out_buffer,
            EPSIZE
        );

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

const uint8_t *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

bool tud_vendor_control_xfer_cb(
    uint8_t rhport,
    uint8_t stage,
    const tusb_control_request_t *request
) {
    if (!_xinput_dev) {
        return false;
    }

    // nothing to with DATA & ACK stage
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    switch (request->bmRequestType_bit.type) {
        case TUSB_REQ_TYPE_VENDOR:
            switch (request->bRequest) {
                case VENDOR_REQUEST_MICROSOFT:
                    if (request->wIndex == 7) {
                        // Get Microsoft OS 2.0 compatible descriptor
                        uint16_t total_len;
                        memcpy(&total_len, desc_ms_os_20 + 8, 2);

                        return tud_control_xfer(rhport, request, (void *)desc_ms_os_20, total_len);
                    } else {
                        return false;
                    }

                default:
                    break;
            }
            break;

        default:
            // stall unknown request
            return false;
    }

    return true;
}

} // extern "C"
