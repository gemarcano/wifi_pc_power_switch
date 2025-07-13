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

#include <gpico/reset.h>

#include <tusb.h>
#include <device/usbd_pvt.h>

#include <pico/unique_id.h>
#include <pico/bootrom.h>

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

static constexpr int RESET_INTERFACE_SUBCLASS = 0;
static constexpr int RESET_INTERFACE_PROTOCOL = 1;

#define TUD_RPI_RESET_DESCRIPTOR(_itfnum, _stridx) \
  /* Interface */\
  9, TUSB_DESC_INTERFACE, (_itfnum), 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, (_stridx)

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
	ITF_NUM_MSC,
	ITF_RESET,
	ITF_NUM_TOTAL,
};

const constexpr int EPNUM_CDC_NOTIF = 0x81;
const constexpr int EPNUM_CDC_OUT   = 0x02;
const constexpr int EPNUM_CDC_IN    = 0x82;
const constexpr int EPNUM_MSC_OUT   = 0x03;
const constexpr int EPNUM_MSC_IN    = 0x83;

static constexpr int TUD_RPI_RESET_DESC_LEN = 9;

constexpr const auto desc_length =
	TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN + TUD_RPI_RESET_DESC_LEN;

constexpr const auto desc_configuration = std::to_array<uint8_t>({
	TUD_CONFIG_DESCRIPTOR(
		1,             // config number
		ITF_NUM_TOTAL, // interface count
		0,             // string index
		desc_length,   // total length
		0x00,          // attribute
		500            // power in mA
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

	TUD_MSC_DESCRIPTOR(
		ITF_NUM_MSC,   // interace number
		5,             // string index
		EPNUM_MSC_OUT, // EP out
		EPNUM_MSC_IN,  // EP in
		64             // EP size
	),

	TUD_RPI_RESET_DESCRIPTOR(ITF_RESET, 6),
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
	u"MSC",                                // 5: MSC
	u"Reset",                              // 6: Reset
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
/*uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
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
}*/

constexpr const int DISK_BLOCK_NUM = 16; // 16 * 512 = 8kB, the smallest windows apparently tolerates
constexpr const int DISK_BLOCK_SIZE = 512;

static std::array<std::array<uint8_t, DISK_BLOCK_SIZE>, DISK_BLOCK_NUM> block_data =
{{
	//              Block0: Boot Sector
	// byte_per_sector         = DISK_BLOCK_SIZE;
	// fat12_sector_num_16     = DISK_BLOCK_NUM;
	// sector_per_cluster      = 1;
	// reserved_sectors        = 1;
	// fat_num                 = 1;
	// fat12_root_entry_num    = 16;
	// sector_per_fat          = 1;
	// sector_per_track        = 1;
	// head_num                = 1;
	// hidden_sectors          = 0;
	// drive_number            = 0x80;
	// media_type              = 0xf8;
	// extended_boot_signature = 0x29;
	// filesystem_type         = "FAT12   ";
	// volume_serial_number    = 0xCAFE;
	// volume_label            = "Grub Boot";
	// FAT magic code at offset 510-511
	{
		0xEB, 0x3C, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30, 0x00, 0x02, 0x01, 0x01, 0x00,
		0x01, 0x10, 0x00, 0x10, 0x00, 0xF8, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x29, 0xFE, 0xCA, 0x00, 0x00, 'G' , 'r' , 'u' , 'b' , ' ' ,
		'B' , 'o' , 'o' , 't' , ' ' , ' ' , 0x46, 0x41, 0x54, 0x31, 0x32, 0x20, 0x20, 0x20, 0x00, 0x00,

		// Pad rest of block with zero until last two bytes, magic FAT code
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x55, 0xAA // magic FAT code
	},

	//              Block1: FAT12 Table
	{
		0xF8, 0xFF, 0xFF, 0xFF, 0x0F // // first 2 entries must be F8FF, third entry is cluster end of readme file
	},

	//              Block2: Root Directory
	{
		// first entry is volume label
		'G' , 'r' , 'u' , 'b' , ' ' , 'B' , 'o' , 'o' , 't' , ' ' , ' ' , 0x08, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4F, 0x6D, 0x65, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		// second entry is readme file
		'b' , 'o' , 'o' , 't' , ' ' , ' ' , ' ' , ' ' , 'c' , 'f' , 'g' , 0x20, 0x00, 0xC6, 0x52, 0x6D,
		0x65, 0x43, 0x65, 0x43, 0x00, 0x00, 0x88, 0x6D, 0x65, 0x43, 0x02, 0x00,
		16, 0x00, 0x00, 0x00 // file size is 4 bytes
	},

	{
	"set default=\"0\"\n" // size 16
	}
}};

uint8_t tud_msc_get_maxlun_cb()
{
	return 1;
}

void tud_msc_inquiry_cb(uint8_t /*lun*/, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
	const std::string_view vid("BOOT");
	const std::string_view pid("BOOT");
	const std::string_view rev("1.0");

	memcpy(vendor_id, vid.data(), vid.size());
	memcpy(product_id, pid.data(), pid.size());
	memcpy(product_rev, rev.data(), rev.size());
}

bool tud_msc_test_unit_ready_cb(uint8_t /*lun*/)
{
	return true;
}

void tud_msc_capacity_cb(uint8_t /*lun*/, uint32_t *block_count, uint16_t *block_size)
{
	*block_count = DISK_BLOCK_NUM;
	*block_size = DISK_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t /*lun*/, uint8_t /*power_condition*/, bool /*start*/, bool /*load_eject*/)
{
	return true;
}

int32_t tud_msc_read10_cb(uint8_t /*lun*/, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
	if (lba >= DISK_BLOCK_NUM)
		return -1;

	// FIXME what if offset goes beyond the end?
	const uint8_t *ptr = &block_data[lba][offset];
	memcpy(buffer, ptr, bufsize);
	return bufsize;
}

bool tud_msc_is_writable_cb(uint8_t /*lun*/)
{
	return true;
}

int32_t tud_msc_write10_cb(uint8_t /*lun*/, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
	if (lba >= DISK_BLOCK_NUM)
		return -1;

	uint8_t *ptr = &block_data[lba][offset];
	memcpy(ptr, buffer, bufsize);
	return bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16], void *buffer, uint16_t bufsize)
{
	const void* response = nullptr;
	uint32_t resplen = 0;

	// True if host wants to read data from device, false if host is sending
	// data to us
	bool in_xfer = true;

	switch (scsi_cmd[0])
	{
		default:
			// Set Sense = Invalid Command Operation
			tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

			return -1;
	}

	if (resplen > bufsize)
		resplen = bufsize;

	if (response && (resplen > 0))
	{
		if (in_xfer)
		{
			memcpy(buffer, response, resplen);
		}
		else
		{
			// host is sending data to us
		}
	}

	return resplen;
}

constexpr unsigned PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE = 1200;

void tud_cdc_line_coding_cb(__unused uint8_t itf, cdc_line_coding_t const* p_line_coding) {
	if (p_line_coding->bit_rate == PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE) {
		gpico::bootsel_reset();
	}
}

static unsigned reset_interface_number = 0;

static void reset_init()
{}

static void reset_reset(uint8_t)
{
	reset_interface_number = 0;
}

static uint16_t reset_open(uint8_t, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
	TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
		RESET_INTERFACE_SUBCLASS == itf_desc->bInterfaceSubClass &&
		RESET_INTERFACE_PROTOCOL == itf_desc->bInterfaceProtocol, 0);

	reset_interface_number = itf_desc->bInterfaceNumber;
	constexpr const int drv_len = sizeof(tusb_desc_interface_t);
	TU_VERIFY(max_len >= drv_len, 0);
	return drv_len;
}

static bool reset_control_xfer_cb(uint8_t, uint8_t stage, const tusb_control_request_t *request)
{
	if (stage != CONTROL_STAGE_SETUP)
		return true;

	if (request->wIndex == reset_interface_number)
	{
		constexpr const int RESET_REQUEST_BOOTSEL = 1;
		constexpr const int RESET_REQUEST_FLASH = 2;
		if (request->bRequest == RESET_REQUEST_BOOTSEL)
		{
			printf("Rebooting to BOOTSEL %i...\r\n", (request->wValue & 0x7f));
			gpico::bootsel_reset();
			return true;
		}
		else if (request->bRequest == RESET_REQUEST_FLASH)
		{
			printf("Rebooting to application...\r\n");
			gpico::flash_reset();
		}
	}

	return false;
}

static bool reset_xfer_cb(uint8_t, uint8_t, xfer_result_t, uint32_t)
{
	return true;
}

static const usbd_class_driver_t reset_driver =
{
	.name = nullptr,
	.init = reset_init,
	.deinit = nullptr,
	.reset = reset_reset,
	.open = reset_open,
	.control_xfer_cb = reset_control_xfer_cb,
	.xfer_cb = reset_xfer_cb,
	.xfer_isr = nullptr,
	.sof = nullptr,
};

const usbd_class_driver_t* usbd_app_driver_get_cb(uint8_t *driver_count)
{
	*driver_count = 1;
	return &reset_driver;
}

namespace pcrb
{
uint8_t get_boot_select()
{
	return strtoul(reinterpret_cast<const char*>(&block_data[3][13]), nullptr, 10);
}

void set_boot_select(uint8_t select)
{
	snprintf(reinterpret_cast<char*>(&block_data[3][0]), DISK_BLOCK_SIZE, "set default=\"%u\"\n", select);
}
}
