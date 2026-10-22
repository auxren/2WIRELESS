#include <cstdint>
#include <cstdio>
#include <cstring>

#include "minitest.h"
#include "wpm_ota_record.h"

int main() {
  const uint8_t good[8] = {0x00, 0x80, 0x00, 0x20, 0x05, 0x6B, 0x00, 0x00};
  const uint8_t bad[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const uint32_t L = 256;

  CHECK(wpm_ota_commit_check(L, good, 0x1234u, 0x1234u, false) == WPM_OTA_OK);
  CHECK(wpm_ota_commit_check(L, good, 0x1234u, 0x1235u, false) == WPM_OTA_ERR_CRC);
  CHECK(wpm_ota_commit_check(L, bad, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_IMAGE);
  CHECK(wpm_ota_commit_check(0, good, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(255, good, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(0x2BD1Au, good, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(L, good, 0x1234u, 0x1234u, true) == WPM_OTA_ERR_BUSY);

  // First-failure precedence: busy > len > image > crc.
  CHECK(wpm_ota_commit_check(0, bad, 1u, 2u, true) == WPM_OTA_ERR_BUSY);
  CHECK(wpm_ota_commit_check(0, bad, 1u, 2u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(L, bad, 1u, 2u, false) == WPM_OTA_ERR_IMAGE);
  CHECK(wpm_ota_commit_check(L, good, 1u, 2u, false) == WPM_OTA_ERR_CRC);

  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_OK), "ok") == 0);
  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_ERR_LEN), "len") == 0);
  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_ERR_IMAGE), "image") == 0);
  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_ERR_CRC), "crc") == 0);
  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_ERR_BUSY), "busy") == 0);
  std::printf("test_ota_commit_logic: %d failures\n", minitest_failures);
  return minitest_failures;
}
