#pragma once
// OTA pending record (18 bytes in reserved FRAM) and image sanity checks.
// Pure functions, no Arduino dependencies, so they run on the desktop.
#include <cstddef>
#include <cstdint>

constexpr uint32_t WPM_OTA_RECORD_ADDR = 0x02BE50;
constexpr size_t WPM_OTA_RECORD_LEN = 18;
constexpr uint32_t WPM_OTA_MAX_LEN = 0x2BD18;
constexpr uint32_t WPM_APP_FLASH_BASE = 0x6000;
constexpr uint32_t WPM_FLASH_END = 0x40000;
constexpr uint32_t WPM_LOADER_BLOB_LEN = 0x4000;

// Raw layout (little-endian fields):
//   [0..3] magic "OTA1" when pending, else zeros; [4..7] len; [8..11] crc32;
//   [12] flags; [13] last_result; [14..17] app_crc32.
struct WpmOtaRecord {
  uint32_t len;
  uint32_t crc32;
  uint8_t flags;
  uint8_t last_result;
  uint32_t app_crc32;
  bool pending;
};

// Never rejects: fields are read regardless of the magic. pending is true only
// when the magic matches exactly. Always returns true.
bool wpm_ota_record_parse(const uint8_t raw[WPM_OTA_RECORD_LEN], WpmOtaRecord* out);

// Magic is written iff in->pending; otherwise the first four bytes are zero.
void wpm_ota_record_serialize(const WpmOtaRecord* in, uint8_t raw[WPM_OTA_RECORD_LEN]);

enum WpmOtaCheck { WPM_OTA_OK, WPM_OTA_ERR_LEN, WPM_OTA_ERR_IMAGE };

WpmOtaCheck wpm_ota_check_len(uint32_t len);

// first8 = first eight bytes of the image at WPM_APP_FLASH_BASE (SP, reset).
WpmOtaCheck wpm_ota_check_vectors(const uint8_t first8[8]);

// 0 -> "none", 1 -> "ok", 2 -> "crc_mismatch", anything else -> "none".
const char* wpm_ota_last_result_str(uint8_t v);
