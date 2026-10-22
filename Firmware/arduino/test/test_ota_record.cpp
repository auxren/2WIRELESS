#include <cstdint>
#include <cstdio>
#include <cstring>

#include "minitest.h"
#include "wpm_ota_record.h"

static bool eq_rec(const WpmOtaRecord& a, const WpmOtaRecord& b) {
  return a.len == b.len && a.crc32 == b.crc32 && a.flags == b.flags &&
         a.last_result == b.last_result && a.app_crc32 == b.app_crc32 &&
         a.pending == b.pending;
}

int main() {
  // Round trip with pending=true: magic and little-endian len on the wire.
  {
    WpmOtaRecord in = {0x1234u, 0xCBF43926u, 0x01, 0x01, 0xDEADBEEFu, true};
    uint8_t raw[WPM_OTA_RECORD_LEN];
    wpm_ota_record_serialize(&in, raw);
    CHECK(raw[0] == 0x4F && raw[1] == 0x54 && raw[2] == 0x41 && raw[3] == 0x31);
    CHECK(raw[4] == 0x34 && raw[5] == 0x12 && raw[6] == 0x00 && raw[7] == 0x00);
    WpmOtaRecord out = {};
    CHECK(wpm_ota_record_parse(raw, &out));
    CHECK(out.pending);
    CHECK(eq_rec(in, out));
  }

  // pending=false: magic serialized as zeros, parses as not pending, fields intact.
  {
    WpmOtaRecord in = {0x2BD18u, 0x01020304u, 0x80, 0x02, 0xA5A5A5A5u, false};
    uint8_t raw[WPM_OTA_RECORD_LEN];
    wpm_ota_record_serialize(&in, raw);
    CHECK(raw[0] == 0 && raw[1] == 0 && raw[2] == 0 && raw[3] == 0);
    WpmOtaRecord out = {};
    CHECK(wpm_ota_record_parse(raw, &out));
    CHECK(!out.pending);
    CHECK(eq_rec(in, out));
  }

  // All-0xFF and all-0x00 raw: not pending; last_result 0 and >2 both read as "none".
  {
    uint8_t ff[WPM_OTA_RECORD_LEN];
    std::memset(ff, 0xFF, sizeof ff);
    WpmOtaRecord out = {};
    CHECK(wpm_ota_record_parse(ff, &out));
    CHECK(!out.pending);
    CHECK(std::strcmp(wpm_ota_last_result_str(out.last_result), "none") == 0);

    uint8_t zero[WPM_OTA_RECORD_LEN];
    std::memset(zero, 0x00, sizeof zero);
    CHECK(wpm_ota_record_parse(zero, &out));
    CHECK(!out.pending);
    CHECK(std::strcmp(wpm_ota_last_result_str(out.last_result), "none") == 0);
    CHECK(std::strcmp(wpm_ota_last_result_str(3), "none") == 0);
    CHECK(std::strcmp(wpm_ota_last_result_str(0xFF), "none") == 0);
  }

  CHECK(std::strcmp(wpm_ota_last_result_str(1), "ok") == 0);
  CHECK(std::strcmp(wpm_ota_last_result_str(2), "crc_mismatch") == 0);

  // Length checks.
  CHECK(wpm_ota_check_len(0) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_check_len(1) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_check_len(0x2BD18u) == WPM_OTA_OK);
  CHECK(wpm_ota_check_len(0x2BD1Au) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_check_len(0x3A000u) == WPM_OTA_ERR_LEN);

  // Vector checks (first 8 bytes of the image at 0x6000).
  {
    const uint8_t reset_4b05[8] = {0x00, 0x80, 0x00, 0x20, 0x05, 0x4B, 0x00, 0x00};
    // a 0x2000-linked image (stock v1.18 reset vector) passes: the vector check cannot distinguish link bases; only build.sh .ota.bin outputs are valid OTA images
    const uint8_t linked_2000_cb05[8] = {0x00, 0x80, 0x00, 0x20, 0x05, 0xCB, 0x00, 0x00};
    const uint8_t ok_6b05[8] = {0x00, 0x80, 0x00, 0x20, 0x05, 0x6B, 0x00, 0x00};
    const uint8_t even_6b04[8] = {0x00, 0x80, 0x00, 0x20, 0x04, 0x6B, 0x00, 0x00};
    const uint8_t sp_above_ram[8] = {0x00, 0x80, 0x00, 0x21, 0x05, 0x6B, 0x00, 0x00};
    const uint8_t all_ff[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(wpm_ota_check_vectors(reset_4b05) == WPM_OTA_ERR_IMAGE);
    CHECK(wpm_ota_check_vectors(linked_2000_cb05) == WPM_OTA_OK);
    CHECK(wpm_ota_check_vectors(ok_6b05) == WPM_OTA_OK);
    CHECK(wpm_ota_check_vectors(even_6b04) == WPM_OTA_ERR_IMAGE);
    CHECK(wpm_ota_check_vectors(sp_above_ram) == WPM_OTA_ERR_IMAGE);
    CHECK(wpm_ota_check_vectors(all_ff) == WPM_OTA_ERR_IMAGE);
  }

  std::printf("test_ota_record: %d failures\n", minitest_failures);
  return minitest_failures;
}
