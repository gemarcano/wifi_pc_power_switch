/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org),
 * Copyright (c) 2024 Gabriel Marcano (gabemarcano@yahoo.com)
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
 *
 */

#include <tusb.h>

#include <pico/unique_id.h>

#include <FreeRTOS.h>
#include <task.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <concepts>
#include <vector>
#include <atomic>
#include <span>
#include <memory>
#include <bit>

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]         HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
		                       _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4) )

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
const tusb_desc_device_t desc_device =
{
		.bLength            = sizeof(tusb_desc_device_t),
		.bDescriptorType    = TUSB_DESC_DEVICE,
		.bcdUSB             = 0x0200,
		.bDeviceClass       = TUSB_CLASS_MISC,
		.bDeviceSubClass    = MISC_SUBCLASS_COMMON,
		.bDeviceProtocol    = MISC_PROTOCOL_IAD,
		.bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

		.idVendor           = 0x6666,
		.idProduct          = USB_PID,
		.bcdDevice          = 0x0100,

		.iManufacturer      = 0x01,
		.iProduct           = 0x02,
		.iSerialNumber      = 0x03,

		.bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
const uint8_t* tud_descriptor_device_cb(void)
{
	return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum
{
	ITF_NUM_CDC,
	ITF_NUM_CDC_DATA,
};

const constexpr int EPNUM_CDC_NOTIF = 0x81;
const constexpr int EPNUM_CDC_OUT   = 0x02;
const constexpr int EPNUM_CDC_IN    = 0x82;

constexpr const auto desc_configuration = std::to_array<uint8_t>({
	TUD_CONFIG_DESCRIPTOR(
		1,                                      // config number
		2,                                      // interface count
		0,                                      // string index
		TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, // total length
		0x00,                                   // attribute
		500                                     // power in mA
	),

	TUD_CDC_DESCRIPTOR(
		ITF_NUM_CDC,     // interface number
		4,               // string index
		EPNUM_CDC_NOTIF, // ep notification address
		8,               // ep notification size
		EPNUM_CDC_OUT,   // ep data address out
		EPNUM_CDC_IN,    // ep data address in
		64               // size
	),
});

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
const uint8_t* tud_descriptor_configuration_cb(uint8_t index)
{
	// we onnly have a single configuration
	(void) index; // for multiple configurations
	return desc_configuration.data();
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
constexpr auto string_desc_arr = std::array
{
	u"\x0409",                             // 0: is supported language is English (0x0409)
	u"Gabriel Marcano",                    // 1: Manufacturer
	u"Wireless PC Power Switch",           // 2: Product
	u"",                                   // 3: Serials, dynamically generated
	u"CDC",                                // 4: CDC
};

template<typename T>
concept string_like = requires(const T& s)
{
	std::basic_string_view{s};
};

template<size_t s>
constexpr size_t max_size(std::span<string_like auto, s> container)
{
	size_t max = 0;
	for (const auto& elem : container)
	{
		auto view = std::basic_string_view{elem};
		if (max < view.size())
			max = view.size();
	}
	return max;
}

static constexpr auto to_little_endian(std::integral auto value)
{
	if constexpr (std::endian::native != std::endian::little)
	{
		return std::byteswap(value);
	}
	else
	{
		return value;
	}
}

/** Helper to get Pico ID and ready it for USB strings use.
 *
 * Making it a singleton that gets evaluated lazily as the Pico ID is
 * initialized during general construction and can run afoul of the static
 * initialization fiasco issue.
 */
class pico_id
{
public:
	static const std::array<uint16_t, PICO_UNIQUE_BOARD_ID_SIZE_BYTES*2>& get()
	{
		if (!id_)
		{
			id_.reset(new pico_id());
		}
		return id_->data;
	}

private:
	static std::unique_ptr<pico_id> id_;

	pico_id()
	{
		pico_unique_board_id_t id;
		pico_get_unique_board_id(&id);

		// Convert ID to a hex string, don't bother using stringstream as that
		// pulls in way, way too much code.
		for (size_t i = 0; i < sizeof(id.id); ++i)
		{
			unsigned char byte = id.id[i];
			for (size_t j = 0; j < 2; ++j)
			{
				unsigned char nibble = (byte >> (4*j)) & 0xF;
				if (nibble < 10)
					nibble = '0' + nibble;
				else
					nibble = 'A' + (nibble - 10);
				data[2*i + j] = to_little_endian(static_cast<uint16_t>(nibble));
			}
		}
	}

	std::array<uint16_t, PICO_UNIQUE_BOARD_ID_SIZE_BYTES*2> data;
};
std::unique_ptr<pico_id> pico_id::id_{nullptr};

// Maximum USB string buffer size in 16 bit units
constexpr size_t desc_max =
	1 +
	std::max(
		max_size(std::span{string_desc_arr}),
		static_cast<size_t>(2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES)
	)
;

// Long-lived buffer containing string to be sent over USB hardware.
static std::array<uint16_t, desc_max> _desc_str;

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	// We only support english, and I'm just going to send all text regardless
	// of language id
	(void) langid;

	uint8_t chr_count = 0;
	if (index == 3)
	{
		static_assert(sizeof(pico_id::get()) < (sizeof(_desc_str) - 1));
		memcpy(_desc_str.data() + 1, pico_id::get().data(), pico_id::get().size() * 2);
		chr_count = pico_id::get().size();
	}
	else
	{
		// Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
		// https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

		if (index > string_desc_arr.size())
			return nullptr;

		const std::u16string_view str = string_desc_arr[index];

		chr_count = str.length();

		for(uint8_t i = 0; i < chr_count; ++i)
		{
			_desc_str[1+i] = to_little_endian(static_cast<uint16_t>(str[i]));
		}
	}

	// first byte is length (including header), second byte is string type
	_desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

	return _desc_str.data();
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
	// TODO not Implemented
	(void) itf;
	(void) report_id;
	(void) report_type;
	(void) buffer;
	(void) reqlen;

	return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
	// This example doesn't use multiple report and report ID
	(void) itf;
	(void) report_id;
	(void) report_type;
	(void)buffer;
	(void)bufsize;
}