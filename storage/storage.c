/*
 * This file is part of the Cerberus project, https://cerberus.uraanai.com/
 *
 * Copyright (c) SatoshiLabs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <string.h>

#include "chacha20poly1305/rfc7539.h"
#include "common.h"
#include "hmac.h"
#include "memzero.h"
#include "norcow.h"
#include "pbkdf2.h"
#include "rand.h"
#include "random_delays.h"
#include "sha2.h"
#include "storage.h"
#include "storage_utils.h"

#if USE_OPTIGA
#include "optiga.h"
#endif

// The APP namespace which is reserved for storage related values.
#define APP_STORAGE 0x00

// Norcow storage keys.
// PIN entry log and PIN success log.
#define PIN_LOGS_KEY ((APP_STORAGE << 8) | 0x01)

// Combined salt, EDEK, ESAK and PIN verification code entry.
#define EDEK_PVC_KEY ((APP_STORAGE << 8) | 0x02)

// PIN set flag.
#define PIN_NOT_SET_KEY ((APP_STORAGE << 8) | 0x03)

// Authenticated storage version.
// NOTE: This should equal the norcow version unless an upgrade is in progress.
#define VERSION_KEY ((APP_STORAGE << 8) | 0x04)

// Storage authentication tag.
#define STORAGE_TAG_KEY ((APP_STORAGE << 8) | 0x05)

// Wipe code data. Introduced in storage version 2.
#define WIPE_CODE_DATA_KEY ((APP_STORAGE << 8) | 0x06)

// Storage upgrade flag. Introduced in storage version 2.
#define STORAGE_UPGRADED_KEY ((APP_STORAGE << 8) | 0x07)

// Unauthenticated storage version. Introduced in storage version 3.
// NOTE: This should always equal the value in VERSION_KEY.
#define UNAUTH_VERSION_KEY ((APP_STORAGE << 8) | 0x08)

// The PIN value corresponding to an empty PIN.
const uint8_t *PIN_EMPTY = (const uint8_t *)"";

// The uint32 representation of an empty PIN, used prior to storage version 3.
const uint32_t V0_PIN_EMPTY = 1;

// Maximum number of PIN digits allowed prior to storage version 3.
#define V0_MAX_PIN_LEN 9

// Maximum length of the wipe code.
// Some limit should be imposed on the length, because the wipe code takes up
// storage space proportional to the length, as opposed to the PIN, which takes
// up constant storage space.
#define MAX_WIPE_CODE_LEN 50

// The total number of iterations to use in PBKDF2.
#define PIN_ITER_COUNT 20000

// The number of milliseconds required to execute PBKDF2.
#define PIN_PBKDF2_MS 1280

// The number of milliseconds required to derive the KEK and KEIV.
#if USE_OPTIGA
#define PIN_DERIVE_MS (PIN_PBKDF2_MS + OPTIGA_PIN_DERIVE_MS)
#else
#define PIN_DERIVE_MS PIN_PBKDF2_MS
#endif

// The length of the hashed hardware salt in bytes.
#define HARDWARE_SALT_SIZE SHA256_DIGEST_LENGTH

// The length of the data encryption key in bytes.
#define DEK_SIZE 32

// The length of the storage authentication key in bytes.
#define SAK_SIZE 16

// The combined length of the data encryption key and the storage authentication
// key in bytes.
#define KEYS_SIZE (DEK_SIZE + SAK_SIZE)

// The length of the PIN verification code in bytes.
#define PVC_SIZE 8

// The length of the storage authentication tag in bytes.
#define STORAGE_TAG_SIZE 16

// The length of the Poly1305 authentication tag in bytes.
#define POLY1305_TAG_SIZE 16

// The length of the ChaCha20 IV (aka nonce) in bytes as per RFC 7539.
#define CHACHA20_IV_SIZE 12

// The length of the ChaCha20 block in bytes.
#define CHACHA20_BLOCK_SIZE 64

// The byte length of the salt used in checking the wipe code.
#define WIPE_CODE_SALT_SIZE 8

// The byte length of the tag used in checking the wipe code.
#define WIPE_CODE_TAG_SIZE 8

// The value corresponding to an unconfigured wipe code.
// NOTE: This is intentionally different from an empty PIN so that we don't need
// special handling when both the PIN and wipe code are not set.
const uint8_t WIPE_CODE_EMPTY[] = {0, 0, 0, 0};
#define WIPE_CODE_EMPTY_LEN 4

// The uint32 representation of an empty wipe code used in storage version 2.
#define V2_WIPE_CODE_EMPTY 0

// TODO: handle translation
const char *const VERIFYING_PIN_MSG = "Verifying PIN";
const char *const PROCESSING_MSG = "Processing";
const char *const STARTING_MSG = "Starting up";
const char *const WRONG_PIN_MSG = "Wrong PIN";

static secbool initialized = secfalse;
static secbool unlocked = secfalse;
static PIN_UI_WAIT_CALLBACK ui_callback = NULL;
static uint32_t ui_total = 0;
static uint32_t ui_rem = 0;
static const char *ui_message = NULL;
static uint8_t cached_keys[KEYS_SIZE] = {0};
static uint8_t *const cached_dek = cached_keys;
static uint8_t *const cached_sak = cached_keys + DEK_SIZE;
static uint8_t authentication_sum[SHA256_DIGEST_LENGTH] = {0};
static uint8_t hardware_salt[HARDWARE_SALT_SIZE] = {0};
static uint32_t norcow_active_version = 0;
static const uint8_t TRUE_BYTE = 0x01;
static const uint8_t FALSE_BYTE = 0x00;
static const uint32_t TRUE_WORD = 0xC35A69A5;
static const uint32_t FALSE_WORD = 0x3CA5965A;

static void __handle_fault(const char *msg, const char *file, int line,
                           const char *func);
#define handle_fault(msg) (__handle_fault(msg, __FILE__, __LINE__, __func__))

static uint32_t pin_to_int(const uint8_t *pin, size_t pin_len);
static secbool storage_upgrade(void);
static secbool storage_upgrade_unlocked(const uint8_t *pin, size_t pin_len,
                                        const uint8_t *ext_salt);
static secbool storage_set_encrypted(const uint16_t key, const void *val,
                                     const uint16_t len);
static secbool storage_get_encrypted(const uint16_t key, void *val_dest,
                                     const uint16_t max_len, uint16_t *len);

#include "flash.h"
#ifdef FLASH_BIT_ACCESS
#include "pinlogs_bitwise.h"
#else
#include "pinlogs_blockwise.h"
#endif

static secbool secequal(const void *ptr1, const void *ptr2, size_t n) {
  const uint8_t *p1 = ptr1;
  const uint8_t *p2 = ptr2;
  uint8_t diff = 0;
  size_t i = 0;
  for (i = 0; i < n; ++i) {
    diff |= *p1 ^ *p2;
    ++p1;
    ++p2;
  }

  // Check loop completion in case of a fault injection attack.
  if (i != n) {
    handle_fault("loop completion check");
  }

  return diff ? secfalse : sectrue;
}

static secbool secequal32(const void *ptr1, const void *ptr2, size_t n) {
  assert(n % sizeof(uint32_t) == 0);
  assert((uintptr_t)ptr1 % sizeof(uint32_t) == 0);
  assert((uintptr_t)ptr2 % sizeof(uint32_t) == 0);

  size_t wn = n / sizeof(uint32_t);
  const uint32_t *p1 = (const uint32_t *)ptr1;
  const uint32_t *p2 = (const uint32_t *)ptr2;
  uint32_t diff = 0;
  size_t i = 0;
  for (i = 0; i < wn; ++i) {
    uint32_t mask = random32();
    diff |= (*p1 + mask - *p2) ^ mask;
    ++p1;
    ++p2;
  }

  // Check loop completion in case of a fault injection attack.
  if (i != wn) {
    handle_fault("loop completion check");
  }

  return diff ? secfalse : sectrue;
}

static secbool is_protected(uint16_t key) {
  const uint8_t app = key >> 8;
  return ((app & FLAG_PUBLIC) == 0 && app != APP_STORAGE) ? sectrue : secfalse;
}

/*
 * Initialize the storage authentication tag for freshly wiped storage.
 */
static secbool auth_init(void) {
  uint8_t tag[SHA256_DIGEST_LENGTH] = {0};
  memzero(authentication_sum, sizeof(authentication_sum));
  hmac_sha256(cached_sak, SAK_SIZE, authentication_sum,
              sizeof(authentication_sum), tag);
  return norcow_set(STORAGE_TAG_KEY, tag, STORAGE_TAG_SIZE);
}

/*
 * Update the storage authentication tag with the given key.
 */
static secbool auth_update(uint16_t key) {
  if (sectrue != is_protected(key)) {
    return sectrue;
  }

  uint8_t tag[SHA256_DIGEST_LENGTH] = {0};
  hmac_sha256(cached_sak, SAK_SIZE, (uint8_t *)&key, sizeof(key), tag);
  for (uint32_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    authentication_sum[i] ^= tag[i];
  }
  hmac_sha256(cached_sak, SAK_SIZE, authentication_sum,
              sizeof(authentication_sum), tag);
  return norcow_set(STORAGE_TAG_KEY, tag, STORAGE_TAG_SIZE);
}

/*
 * A secure version of norcow_set(), which updates the storage authentication
 * tag.
 */
static secbool auth_set(uint16_t key, const void *val, uint16_t len) {
  secbool found = secfalse;
  secbool ret = norcow_set_ex(key, val, len, &found);
  if (sectrue == ret && secfalse == found) {
    ret = auth_update(key);
    if (sectrue != ret) {
      norcow_delete(key);
    }
  }
  return ret;
}

/*
 * A secure version of norcow_get(), which checks the storage authentication
 * tag.
 */
static secbool auth_get(uint16_t key, const void **val, uint16_t *len) {
  *val = NULL;
  *len = 0;
  uint32_t sum[SHA256_DIGEST_LENGTH / sizeof(uint32_t)] = {0};

  // Prepare inner and outer digest.
  uint32_t odig[SHA256_DIGEST_LENGTH / sizeof(uint32_t)] = {0};
  uint32_t idig[SHA256_DIGEST_LENGTH / sizeof(uint32_t)] = {0};
  hmac_sha256_prepare(cached_sak, SAK_SIZE, odig, idig);

  // Prepare SHA-256 message padding.
  uint32_t g[SHA256_BLOCK_LENGTH / sizeof(uint32_t)] = {0};
  uint32_t h[SHA256_BLOCK_LENGTH / sizeof(uint32_t)] = {0};
  g[15] = (SHA256_BLOCK_LENGTH + 2) * 8;
  h[15] = (SHA256_BLOCK_LENGTH + SHA256_DIGEST_LENGTH) * 8;
  h[8] = 0x80000000;

  uint32_t offset = 0;
  uint16_t k = 0;
  uint16_t l = 0;
  uint16_t tag_len = 0;
  uint16_t entry_count = 0;  // Mitigation against fault injection.
  uint16_t other_count = 0;  // Mitigation against fault injection.
  const void *v = NULL;
  const void *tag_val = NULL;
  while (sectrue == norcow_get_next(&offset, &k, &v, &l)) {
    ++entry_count;
    if (k == key) {
      *val = v;
      *len = l;
    } else {
      ++other_count;
    }
    if (sectrue != is_protected(k)) {
      if (k == STORAGE_TAG_KEY) {
        tag_val = v;
        tag_len = l;
      }
      continue;
    }
    g[0] = (((uint32_t)k & 0xff) << 24) | (((uint32_t)k & 0xff00) << 8) |
           0x8000;  // Add SHA message padding.
    sha256_Transform(idig, g, h);
    sha256_Transform(odig, h, h);
    for (uint32_t i = 0; i < SHA256_DIGEST_LENGTH / sizeof(uint32_t); i++) {
      sum[i] ^= h[i];
    }
  }
  memcpy(h, sum, sizeof(sum));

  sha256_Transform(idig, h, h);
  sha256_Transform(odig, h, h);

  memzero(odig, sizeof(odig));
  memzero(idig, sizeof(idig));

  // Cache the authentication sum.
  for (size_t i = 0; i < SHA256_DIGEST_LENGTH / sizeof(uint32_t); i++) {
#if BYTE_ORDER == LITTLE_ENDIAN
    REVERSE32(sum[i], ((uint32_t *)authentication_sum)[i]);
#else
    ((uint32_t *)authentication_sum)[i] = sum[i];
#endif
  }

  // Check loop completion in case of a fault injection attack.
  if (secfalse != norcow_get_next(&offset, &k, &v, &l)) {
    handle_fault("loop completion check");
  }

  // Check storage authentication tag.
#if BYTE_ORDER == LITTLE_ENDIAN
  for (size_t i = 0; i < SHA256_DIGEST_LENGTH / sizeof(uint32_t); i++) {
    REVERSE32(h[i], h[i]);
  }
#endif
  if (tag_val == NULL || tag_len != STORAGE_TAG_SIZE ||
      sectrue != secequal(h, tag_val, STORAGE_TAG_SIZE)) {
    handle_fault("storage tag check");
  }

  if (*val == NULL) {
    // Check for fault injection.
    if (other_count != entry_count) {
      handle_fault("sanity check");
    }
    return secfalse;
  }
  return sectrue;
}

static secbool set_wipe_code(const uint8_t *wipe_code, size_t wipe_code_len) {
  if (wipe_code_len > MAX_WIPE_CODE_LEN ||
      wipe_code_len > UINT16_MAX - WIPE_CODE_SALT_SIZE - WIPE_CODE_TAG_SIZE) {
    return secfalse;
  }

  if (wipe_code_len == 0) {
    // This is to avoid having to check pin != PIN_EMPTY when checking the wipe
    // code.
    wipe_code = WIPE_CODE_EMPTY;
    wipe_code_len = WIPE_CODE_EMPTY_LEN;
  }

  // The format of the WIPE_CODE_DATA_KEY entry is:
  // wipe code (variable), random salt (8 bytes), authentication tag (8 bytes)
  // NOTE: We allocate extra space for the HMAC result.
  uint8_t data[(MAX_WIPE_CODE_LEN + WIPE_CODE_SALT_SIZE +
                SHA256_DIGEST_LENGTH)] = {0};
  uint8_t *salt = data + wipe_code_len;
  uint8_t *tag = salt + WIPE_CODE_SALT_SIZE;
  memcpy(data, wipe_code, wipe_code_len);

  random_buffer(salt, WIPE_CODE_SALT_SIZE);
  hmac_sha256(salt, WIPE_CODE_SALT_SIZE, wipe_code, wipe_code_len, tag);

  secbool ret =
      norcow_set(WIPE_CODE_DATA_KEY, data,
                 wipe_code_len + WIPE_CODE_SALT_SIZE + WIPE_CODE_TAG_SIZE);

  memzero(data, sizeof(data));
  return ret;
}

static secbool is_not_wipe_code(const uint8_t *pin, size_t pin_len) {
  uint8_t salt[WIPE_CODE_SALT_SIZE] = {0};
  uint8_t stored_tag[WIPE_CODE_TAG_SIZE] = {0};
  uint8_t computed_tag1[SHA256_DIGEST_LENGTH] = {0};
  uint8_t computed_tag2[SHA256_DIGEST_LENGTH] = {0};

  // Read the wipe code data from the storage.
  const void *wipe_code_data = NULL;
  uint16_t len = 0;
  if (sectrue != norcow_get(WIPE_CODE_DATA_KEY, &wipe_code_data, &len) ||
      len <= WIPE_CODE_SALT_SIZE + WIPE_CODE_TAG_SIZE) {
    handle_fault("no wipe code");
    return secfalse;
  }
  const uint8_t *wipe_code = (const uint8_t *)wipe_code_data;
  size_t wipe_code_len = len - WIPE_CODE_SALT_SIZE - WIPE_CODE_TAG_SIZE;
  memcpy(salt, (uint8_t *)wipe_code_data + wipe_code_len, sizeof(salt));
  memcpy(stored_tag,
         (uint8_t *)wipe_code_data + wipe_code_len + WIPE_CODE_SALT_SIZE,
         sizeof(stored_tag));

  // Check integrity in case of flash read manipulation attack.
  hmac_sha256(salt, WIPE_CODE_SALT_SIZE, wipe_code, wipe_code_len,
              computed_tag1);
  if (sectrue != secequal(stored_tag, computed_tag1, sizeof(stored_tag))) {
    handle_fault("wipe code tag");
    return secfalse;
  }

  // Prepare the authentication tag of the entered PIN.
  wait_random();
  hmac_sha256(salt, WIPE_CODE_SALT_SIZE, pin, pin_len, computed_tag1);

  // Recompute to check for fault injection attack.
  wait_random();
  hmac_sha256(salt, WIPE_CODE_SALT_SIZE, pin, pin_len, computed_tag2);
  memzero(salt, sizeof(salt));
  if (sectrue !=
      secequal(computed_tag1, computed_tag2, sizeof(computed_tag1))) {
    handle_fault("wipe code fault");
    return secfalse;
  }

  // Compare wipe code with the entered PIN via the authentication tag.
  wait_random();
  if (secfalse != secequal(stored_tag, computed_tag1, sizeof(stored_tag))) {
    return secfalse;
  }
  memzero(stored_tag, sizeof(stored_tag));
  return sectrue;
}

static secbool ui_progress(uint32_t elapsed_ms) {
  ui_rem -= elapsed_ms;
  if (ui_callback && ui_message) {
    uint32_t progress = 0;
    if (ui_total < 1000000) {
      progress = 1000 * (ui_total - ui_rem) / ui_total;
    } else {
      // Avoid overflow. Precise enough.
      progress = (ui_total - ui_rem) / (ui_total / 1000);
    }
    // Round the remaining time to the nearest second.
    return ui_callback((ui_rem + 500) / 1000, progress, ui_message);
  } else {
    return secfalse;
  }
}

#if !USE_OPTIGA
static void derive_kek(const uint8_t *pin, size_t pin_len,
                       const uint8_t *storage_salt, const uint8_t *ext_salt,
                       uint8_t kek[SHA256_DIGEST_LENGTH],
                       uint8_t keiv[SHA256_DIGEST_LENGTH]) {
  uint8_t salt[HARDWARE_SALT_SIZE + STORAGE_SALT_SIZE + EXTERNAL_SALT_SIZE] = {
      0};
  size_t salt_len = 0;

  memcpy(salt + salt_len, hardware_salt, HARDWARE_SALT_SIZE);
  salt_len += HARDWARE_SALT_SIZE;

  memcpy(salt + salt_len, storage_salt, STORAGE_SALT_SIZE);
  salt_len += STORAGE_SALT_SIZE;

  if (ext_salt != NULL) {
    memcpy(salt + salt_len, ext_salt, EXTERNAL_SALT_SIZE);
    salt_len += EXTERNAL_SALT_SIZE;
  }

  PBKDF2_HMAC_SHA256_CTX ctx = {0};
  pbkdf2_hmac_sha256_Init(&ctx, pin, pin_len, salt, salt_len, 1);
  for (int i = 1; i <= 5; i++) {
    pbkdf2_hmac_sha256_Update(&ctx, PIN_ITER_COUNT / 10);
    ui_progress(PIN_PBKDF2_MS / 10);
  }
  pbkdf2_hmac_sha256_Final(&ctx, kek);

  pbkdf2_hmac_sha256_Init(&ctx, pin, pin_len, salt, salt_len, 2);
  for (int i = 6; i <= 10; i++) {
    pbkdf2_hmac_sha256_Update(&ctx, PIN_ITER_COUNT / 10);
    ui_progress(PIN_PBKDF2_MS / 10);
  }
  pbkdf2_hmac_sha256_Final(&ctx, keiv);

  memzero(&ctx, sizeof(PBKDF2_HMAC_SHA256_CTX));
  memzero(&salt, sizeof(salt));
}
#endif

#if USE_OPTIGA
static void stretch_pin_optiga(const uint8_t *pin, size_t pin_len,
                               const uint8_t storage_salt[STORAGE_SALT_SIZE],
                               const uint8_t *ext_salt,
                               uint8_t stretched_pin[OPTIGA_PIN_SECRET_SIZE]) {
  // Combining the PIN with the storage salt aims to ensure that if the
  // MCU-Optiga communication is compromised, then a user with a low-entropy PIN
  // remains protected against an attacker who is not able to read the contents
  // of the MCU storage. Stretching the PIN with PBKDF2 ensures that even if
  // Optiga itself is completely compromised, it will not reduce the security
  // of the device below that of earlier Cerberus models which also use PBKDF2
  // with the same number of iterations.

  uint8_t salt[HARDWARE_SALT_SIZE + STORAGE_SALT_SIZE + EXTERNAL_SALT_SIZE] = {
      0};
  size_t salt_len = 0;

  memcpy(salt + salt_len, hardware_salt, HARDWARE_SALT_SIZE);
  salt_len += HARDWARE_SALT_SIZE;

  memcpy(salt + salt_len, storage_salt, STORAGE_SALT_SIZE);
  salt_len += STORAGE_SALT_SIZE;

  if (ext_salt != NULL) {
    memcpy(salt + salt_len, ext_salt, EXTERNAL_SALT_SIZE);
    salt_len += EXTERNAL_SALT_SIZE;
  }

  PBKDF2_HMAC_SHA256_CTX ctx = {0};
  pbkdf2_hmac_sha256_Init(&ctx, pin, pin_len, salt, salt_len, 1);
  memzero(&salt, sizeof(salt));

  for (int i = 1; i <= 10; i++) {
    pbkdf2_hmac_sha256_Update(&ctx, PIN_ITER_COUNT / 10);
    ui_progress(PIN_PBKDF2_MS / 10);
  }
  pbkdf2_hmac_sha256_Final(&ctx, stretched_pin);

  memzero(&ctx, sizeof(ctx));
}
#endif

#if USE_OPTIGA
static void derive_kek_optiga(
    const uint8_t optiga_secret[OPTIGA_PIN_SECRET_SIZE],
    uint8_t kek[SHA256_DIGEST_LENGTH], uint8_t keiv[SHA256_DIGEST_LENGTH]) {
  PBKDF2_HMAC_SHA256_CTX ctx = {0};
  pbkdf2_hmac_sha256_Init(&ctx, optiga_secret, OPTIGA_PIN_SECRET_SIZE, NULL, 0,
                          1);
  pbkdf2_hmac_sha256_Update(&ctx, 1);
  pbkdf2_hmac_sha256_Final(&ctx, kek);

  pbkdf2_hmac_sha256_Init(&ctx, optiga_secret, OPTIGA_PIN_SECRET_SIZE, NULL, 0,
                          2);
  pbkdf2_hmac_sha256_Update(&ctx, 1);
  pbkdf2_hmac_sha256_Final(&ctx, keiv);

  memzero(&ctx, sizeof(ctx));
}
#endif

static secbool __wur derive_kek_set(const uint8_t *pin, size_t pin_len,
                                    const uint8_t *storage_salt,
                                    const uint8_t *ext_salt,
                                    uint8_t kek[SHA256_DIGEST_LENGTH],
                                    uint8_t keiv[SHA256_DIGEST_LENGTH]) {
#if USE_OPTIGA
  uint8_t optiga_secret[OPTIGA_PIN_SECRET_SIZE] = {0};
  uint8_t stretched_pin[OPTIGA_PIN_SECRET_SIZE] = {0};
  stretch_pin_optiga(pin, pin_len, storage_salt, ext_salt, stretched_pin);
  int ret = optiga_pin_set(ui_progress, stretched_pin, optiga_secret);
  memzero(stretched_pin, sizeof(stretched_pin));
  if (ret != OPTIGA_SUCCESS) {
    memzero(optiga_secret, sizeof(optiga_secret));
    return secfalse;
  }
  derive_kek_optiga(optiga_secret, kek, keiv);
  memzero(optiga_secret, sizeof(optiga_secret));
#else
  derive_kek(pin, pin_len, storage_salt, ext_salt, kek, keiv);
#endif
  return sectrue;
}

static secbool __wur derive_kek_unlock(const uint8_t *pin, size_t pin_len,
                                       const uint8_t *storage_salt,
                                       const uint8_t *ext_salt,
                                       uint8_t kek[SHA256_DIGEST_LENGTH],
                                       uint8_t keiv[SHA256_DIGEST_LENGTH]) {
#if USE_OPTIGA
  uint8_t optiga_secret[OPTIGA_PIN_SECRET_SIZE] = {0};
  uint8_t stretched_pin[OPTIGA_PIN_SECRET_SIZE] = {0};
  stretch_pin_optiga(pin, pin_len, storage_salt, ext_salt, stretched_pin);
  int ret = optiga_pin_verify(ui_progress, stretched_pin, optiga_secret);
  memzero(stretched_pin, sizeof(stretched_pin));
  if (ret != OPTIGA_SUCCESS) {
    memzero(optiga_secret, sizeof(optiga_secret));
    if (ret == OPTIGA_ERR_COUNTER_EXCEEDED) {
      // Unreachable code. Wipe should have already been triggered in unlock().
      storage_wipe();
      show_pin_too_many_screen();
    }
    ensure(ret == OPTIGA_ERR_AUTH_FAIL ? sectrue : secfalse,
           "optiga_pin_verify failed");
    return secfalse;
  }
  derive_kek_optiga(optiga_secret, kek, keiv);
  memzero(optiga_secret, sizeof(optiga_secret));
#else
  derive_kek(pin, pin_len, storage_salt, ext_salt, kek, keiv);
#endif
  return sectrue;
}

static secbool set_pin(const uint8_t *pin, size_t pin_len,
                       const uint8_t *ext_salt) {
  // Encrypt the cached keys using the new PIN and set the new PVC.
  uint8_t buffer[STORAGE_SALT_SIZE + KEYS_SIZE + POLY1305_TAG_SIZE] = {0};
  uint8_t *rand_salt = buffer;
  uint8_t *ekeys = buffer + STORAGE_SALT_SIZE;
  uint8_t *pvc = buffer + STORAGE_SALT_SIZE + KEYS_SIZE;

  uint8_t kek[SHA256_DIGEST_LENGTH] = {0};
  uint8_t keiv[SHA256_DIGEST_LENGTH] = {0};
  chacha20poly1305_ctx ctx = {0};
  random_buffer(rand_salt, STORAGE_SALT_SIZE);
  ui_progress(0);
  ensure(derive_kek_set(pin, pin_len, rand_salt, ext_salt, kek, keiv),
         "derive_kek_set failed");
  rfc7539_init(&ctx, kek, keiv);
  memzero(kek, sizeof(kek));
  memzero(keiv, sizeof(keiv));
  chacha20poly1305_encrypt(&ctx, cached_keys, ekeys, KEYS_SIZE);
  rfc7539_finish(&ctx, 0, KEYS_SIZE, pvc);
  memzero(&ctx, sizeof(ctx));
  secbool ret = norcow_set(EDEK_PVC_KEY, buffer,
                           STORAGE_SALT_SIZE + KEYS_SIZE + PVC_SIZE);
  memzero(buffer, sizeof(buffer));

  if (ret == sectrue) {
    if (pin_len == 0) {
      ret = norcow_set(PIN_NOT_SET_KEY, &TRUE_BYTE, sizeof(TRUE_BYTE));
    } else {
      ret = norcow_set(PIN_NOT_SET_KEY, &FALSE_BYTE, sizeof(FALSE_BYTE));
    }
  }

  return ret;
}

/*
 * Initializes the values of VERSION_KEY, EDEK_PVC_KEY, PIN_NOT_SET_KEY and
 * PIN_LOGS_KEY using an empty PIN. This function should be called to initialize
 * freshly wiped storage.
 */
static void init_wiped_storage(void) {
  if (sectrue != initialized) {
    // We cannot initialize the storage contents if the hardware_salt is not
    // set.
    return;
  }

#if USE_OPTIGA
  ensure(optiga_random_buffer(cached_keys, sizeof(cached_keys)) ? sectrue
                                                                : secfalse,
         "optiga_random_buffer failed");
  random_xor(cached_keys, sizeof(cached_keys));
#else
  random_buffer(cached_keys, sizeof(cached_keys));
#endif
  unlocked = sectrue;
  uint32_t version = NORCOW_VERSION;
  ensure(auth_init(), "set_storage_auth_tag failed");
  ensure(storage_set_encrypted(VERSION_KEY, &version, sizeof(version)),
         "set_storage_version failed");
  ensure(norcow_set(UNAUTH_VERSION_KEY, &version, sizeof(version)),
         "set_unauth_storage_version failed");
  ensure(norcow_set(STORAGE_UPGRADED_KEY, &FALSE_WORD, sizeof(FALSE_WORD)),
         "set_storage_not_upgraded failed");
  ensure(pin_logs_init(0), "init_pin_logs failed");
  ensure(set_wipe_code(WIPE_CODE_EMPTY, WIPE_CODE_EMPTY_LEN),
         "set_wipe_code failed");

  ui_total = PIN_DERIVE_MS;
  ui_rem = ui_total;
  ui_message = PROCESSING_MSG;
  ensure(set_pin(PIN_EMPTY, PIN_EMPTY_LEN, NULL), "init_pin failed");
}

void storage_init(PIN_UI_WAIT_CALLBACK callback, const uint8_t *salt,
                  const uint16_t salt_len) {
  initialized = secfalse;
  unlocked = secfalse;
  norcow_init(&norcow_active_version);
  initialized = sectrue;
  ui_callback = callback;

  sha256_Raw(salt, salt_len, hardware_salt);

  if (norcow_active_version < NORCOW_VERSION) {
    if (sectrue != storage_upgrade()) {
      storage_wipe();
      ensure(secfalse, "storage_upgrade failed");
    }
  }

  // If there is no EDEK, then generate a random DEK and SAK and store them.
  const void *val = NULL;
  uint16_t len = 0;
  if (secfalse == norcow_get(EDEK_PVC_KEY, &val, &len)) {
    init_wiped_storage();
    storage_lock();
  }
  memzero(cached_keys, sizeof(cached_keys));
}

secbool storage_pin_fails_increase(void) {
  if (sectrue != initialized) {
    return secfalse;
  }
  return pin_fails_increase();
}

secbool storage_is_unlocked(void) {
  if (sectrue != initialized) {
    return secfalse;
  }

  return unlocked;
}

void storage_lock(void) {
  unlocked = secfalse;
  memzero(cached_keys, sizeof(cached_keys));
  memzero(authentication_sum, sizeof(authentication_sum));
}

// Returns the storage version that was used to lock the storage.
static uint32_t get_lock_version(void) {
  const void *val = NULL;
  uint16_t len = 0;
  if (sectrue != norcow_get(UNAUTH_VERSION_KEY, &val, &len) ||
      len != sizeof(uint32_t)) {
    handle_fault("no lock version");
  }

  return *(uint32_t *)val;
}

secbool check_storage_version(void) {
  uint32_t version = 0;
  uint16_t len = 0;
  if (sectrue !=
          storage_get_encrypted(VERSION_KEY, &version, sizeof(version), &len) ||
      len != sizeof(version)) {
    handle_fault("storage version check");
    return secfalse;
  }

  if (version != get_lock_version()) {
    handle_fault("storage version check");
    return secfalse;
  }

  const void *storage_upgraded = NULL;
  if (sectrue != norcow_get(STORAGE_UPGRADED_KEY, &storage_upgraded, &len) ||
      len != sizeof(TRUE_WORD)) {
    handle_fault("storage version check");
    return secfalse;
  }

  if (version > norcow_active_version) {
    // Attack: Storage was downgraded.
    storage_wipe();
    handle_fault("storage version check");
    return secfalse;
  } else if (version < norcow_active_version) {
    // Storage was upgraded.
    if (*(const uint32_t *)storage_upgraded != TRUE_WORD) {
      // Attack: The upgrade process was bypassed.
      storage_wipe();
      handle_fault("storage version check");
      return secfalse;
    }
    norcow_set(STORAGE_UPGRADED_KEY, &FALSE_WORD, sizeof(FALSE_WORD));
    storage_set_encrypted(VERSION_KEY, &norcow_active_version,
                          sizeof(norcow_active_version));
    norcow_set(UNAUTH_VERSION_KEY, &norcow_active_version,
               sizeof(norcow_active_version));
  } else {
    // Standard operation. The storage was neither upgraded nor downgraded.
    if (*(const uint32_t *)storage_upgraded != FALSE_WORD) {
      // Attack: The upgrade process was launched when it shouldn't have been.
      storage_wipe();
      handle_fault("storage version check");
      return secfalse;
    }
  }
  return sectrue;
}

static secbool decrypt_dek(const uint8_t *kek, const uint8_t *keiv) {
  const void *buffer = NULL;
  uint16_t len = 0;
  if (sectrue != initialized ||
      sectrue != norcow_get(EDEK_PVC_KEY, &buffer, &len) ||
      len != STORAGE_SALT_SIZE + KEYS_SIZE + PVC_SIZE) {
    handle_fault("no EDEK");
    return secfalse;
  }

  const uint8_t *ekeys = (const uint8_t *)buffer + STORAGE_SALT_SIZE;
  const uint32_t *pvc = (const uint32_t *)buffer +
                        (STORAGE_SALT_SIZE + KEYS_SIZE) / sizeof(uint32_t);
  _Static_assert(((STORAGE_SALT_SIZE + KEYS_SIZE) & 3) == 0, "PVC unaligned");
  _Static_assert((PVC_SIZE & 3) == 0, "PVC size unaligned");

  uint8_t keys[KEYS_SIZE] = {0};
  uint8_t tag[POLY1305_TAG_SIZE] __attribute__((aligned(sizeof(uint32_t))));
  chacha20poly1305_ctx ctx = {0};

  // Decrypt the data encryption key and the storage authentication key and
  // check the PIN verification code.
  rfc7539_init(&ctx, kek, keiv);
  chacha20poly1305_decrypt(&ctx, ekeys, keys, KEYS_SIZE);
  rfc7539_finish(&ctx, 0, KEYS_SIZE, tag);
  memzero(&ctx, sizeof(ctx));
  wait_random();
  if (secequal32(tag, pvc, PVC_SIZE) != sectrue) {
    memzero(keys, sizeof(keys));
    memzero(tag, sizeof(tag));
    return secfalse;
  }
  memcpy(cached_keys, keys, sizeof(keys));
  memzero(keys, sizeof(keys));
  memzero(tag, sizeof(tag));
  return sectrue;
}

static void ensure_not_wipe_code(const uint8_t *pin, size_t pin_len) {
  if (sectrue != is_not_wipe_code(pin, pin_len)) {
    storage_wipe();
    show_wipe_code_screen();
  }
}

static secbool unlock(const uint8_t *pin, size_t pin_len,
                      const uint8_t *ext_salt) {
  const uint8_t *unlock_pin = pin;
  size_t unlock_pin_len = pin_len;

  // In case of an upgrade from version 1 or 2, encode the PIN to the old format
  // and bump the total time of UI progress to account for the set_pin() call in
  // storage_upgrade_unlocked().
  uint32_t legacy_pin = 0;
  if (get_lock_version() <= 2) {
    ui_total += PIN_DERIVE_MS;
    ui_rem += PIN_DERIVE_MS;
    legacy_pin = pin_to_int(pin, pin_len);
    unlock_pin = (const uint8_t *)&legacy_pin;
    unlock_pin_len = sizeof(legacy_pin);
  }

  // Now we can check for wipe code.
  ensure_not_wipe_code(unlock_pin, unlock_pin_len);

  // Get the pin failure counter
  uint32_t ctr = 0;
  if (sectrue != pin_get_fails(&ctr)) {
    memzero(&legacy_pin, sizeof(legacy_pin));
    return secfalse;
  }

  // Wipe storage if too many failures
  wait_random();
  if (ctr >= PIN_MAX_TRIES) {
    storage_wipe();
    show_pin_too_many_screen();
    return secfalse;
  }

  // Sleep for 2^ctr - 1 seconds before checking the PIN.
  uint32_t wait = (1 << ctr) - 1;
  ui_total += wait * 1000;
  ui_rem += wait * 1000;
  ui_progress(0);
  for (uint32_t i = 0; i < 10 * wait; i++) {
    if (sectrue == ui_progress(100)) {
      memzero(&legacy_pin, sizeof(legacy_pin));
      return secfalse;
    }
    hal_delay(100);
  }

  // First, we increase PIN fail counter in storage, even before checking the
  // PIN.  If the PIN is correct, we reset the counter afterwards.  If not, we
  // check if this is the last allowed attempt.
  if (sectrue != storage_pin_fails_increase()) {
    return secfalse;
  }

  // Check that the PIN fail counter was incremented.
  uint32_t ctr_ck = 0;
  if (sectrue != pin_get_fails(&ctr_ck) || ctr + 1 != ctr_ck) {
    handle_fault("PIN counter increment");
    return secfalse;
  }

  // Read the random salt from EDEK_PVC_KEY and use it to derive the KEK and
  // KEIV from the PIN.
  const void *rand_salt = NULL;
  uint16_t len = 0;
  if (sectrue != initialized ||
      sectrue != norcow_get(EDEK_PVC_KEY, &rand_salt, &len) ||
      len != STORAGE_SALT_SIZE + KEYS_SIZE + PVC_SIZE) {
    memzero(&legacy_pin, sizeof(legacy_pin));
    handle_fault("no EDEK");
    return secfalse;
  }
  uint8_t kek[SHA256_DIGEST_LENGTH] = {0};
  uint8_t keiv[SHA256_DIGEST_LENGTH] = {0};

  // Check whether the entered PIN is correct.
  if (sectrue != derive_kek_unlock(unlock_pin, unlock_pin_len,
                                   (const uint8_t *)rand_salt, ext_salt, kek,
                                   keiv) ||
      sectrue != decrypt_dek(kek, keiv)) {
    memzero(&legacy_pin, sizeof(legacy_pin));
    // Wipe storage if too many failures
    wait_random();
    if (ctr + 1 >= PIN_MAX_TRIES) {
      storage_wipe();
      show_pin_too_many_screen();
    }

    // Finish the countdown. Check for ui_rem underflow.
    while (0 < ui_rem && ui_rem < ui_total) {
      ui_message = WRONG_PIN_MSG;
      if (sectrue == ui_progress(100)) {
        return secfalse;
      }
      hal_delay(100);
    }

    return secfalse;
  }
  memzero(&legacy_pin, sizeof(legacy_pin));
  memzero(kek, sizeof(kek));
  memzero(keiv, sizeof(keiv));

  // Check for storage upgrades that need to be performed after unlocking and
  // check that the authenticated version number matches the unauthenticated
  // version and norcow version.
  // NOTE: This also initializes the authentication_sum by calling
  // storage_get_encrypted() which calls auth_get().
  if (sectrue != storage_upgrade_unlocked(pin, pin_len, ext_salt) ||
      sectrue != check_storage_version()) {
    return secfalse;
  }

  unlocked = sectrue;

  // Finally set the counter to 0 to indicate success.
  return pin_fails_reset();
}

secbool storage_unlock(const uint8_t *pin, size_t pin_len,
                       const uint8_t *ext_salt) {
  if (sectrue != initialized || pin == NULL) {
    return secfalse;
  }

  ui_total = PIN_DERIVE_MS;
  ui_rem = ui_total;
  if (pin_len == 0) {
    if (ui_message == NULL) {
      ui_message = STARTING_MSG;
    } else {
      ui_message = PROCESSING_MSG;
    }
  } else {
    ui_message = VERIFYING_PIN_MSG;
  }
  return unlock(pin, pin_len, ext_salt);
}

/*
 * Finds the encrypted data stored under key and writes its length to len.
 * If val_dest is not NULL and max_len >= len, then the data is decrypted
 * to val_dest using cached_dek as the decryption key.
 */
static secbool storage_get_encrypted(const uint16_t key, void *val_dest,
                                     const uint16_t max_len, uint16_t *len) {
  const void *val_stored = NULL;

  if (sectrue != auth_get(key, &val_stored, len)) {
    return secfalse;
  }

  if (*len < CHACHA20_IV_SIZE + POLY1305_TAG_SIZE) {
    handle_fault("ciphertext length check");
    return secfalse;
  }
  *len -= CHACHA20_IV_SIZE + POLY1305_TAG_SIZE;

  if (val_dest == NULL) {
    return sectrue;
  }

  if (*len > max_len) {
    return secfalse;
  }

  const uint8_t *iv = (const uint8_t *)val_stored;
  const uint8_t *tag_stored =
      (const uint8_t *)val_stored + CHACHA20_IV_SIZE + *len;
  const uint8_t *ciphertext = (const uint8_t *)val_stored + CHACHA20_IV_SIZE;
  uint8_t tag_computed[POLY1305_TAG_SIZE] = {0};
  chacha20poly1305_ctx ctx = {0};
  rfc7539_init(&ctx, cached_dek, iv);
  rfc7539_auth(&ctx, (const uint8_t *)&key, sizeof(key));
  chacha20poly1305_decrypt(&ctx, ciphertext, (uint8_t *)val_dest, *len);
  rfc7539_finish(&ctx, sizeof(key), *len, tag_computed);
  memzero(&ctx, sizeof(ctx));

  // Verify authentication tag.
  if (secequal(tag_computed, tag_stored, POLY1305_TAG_SIZE) != sectrue) {
    memzero(val_dest, max_len);
    memzero(tag_computed, sizeof(tag_computed));
    handle_fault("authentication tag check");
    return secfalse;
  }

  memzero(tag_computed, sizeof(tag_computed));
  return sectrue;
}

secbool storage_has(const uint16_t key) {
  uint16_t len = 0;
  return storage_get(key, NULL, 0, &len);
}

/*
 * Finds the data stored under key and writes its length to len. If val_dest is
 * not NULL and max_len >= len, then the data is copied to val_dest.
 */
secbool storage_get(const uint16_t key, void *val_dest, const uint16_t max_len,
                    uint16_t *len) {
  const uint8_t app = key >> 8;
  // APP == 0 is reserved for PIN related values
  if (sectrue != initialized || app == APP_STORAGE) {
    return secfalse;
  }

  // If the top bit of APP is set, then the value is not encrypted and can be
  // read from a locked device.
  if ((app & FLAG_PUBLIC) != 0) {
    const void *val_stored = NULL;
    if (sectrue != norcow_get(key, &val_stored, len)) {
      return secfalse;
    }
    if (val_dest == NULL) {
      return sectrue;
    }
    if (*len > max_len) {
      return secfalse;
    }
    memcpy(val_dest, val_stored, *len);
    return sectrue;
  } else {
    if (sectrue != unlocked) {
      return secfalse;
    }
    return storage_get_encrypted(key, val_dest, max_len, len);
  }
}

/*
 * Encrypts the data at val using cached_dek as the encryption key and stores
 * the ciphertext under key.
 */
static secbool storage_set_encrypted(const uint16_t key, const void *val,
                                     const uint16_t len) {
  if (len > UINT16_MAX - CHACHA20_IV_SIZE - POLY1305_TAG_SIZE) {
    return secfalse;
  }

  // Preallocate space on the flash storage.
  if (sectrue !=
      auth_set(key, NULL, CHACHA20_IV_SIZE + POLY1305_TAG_SIZE + len)) {
    return secfalse;
  }

  // Write the IV to the flash.
  uint8_t buffer[CHACHA20_BLOCK_SIZE] = {0};
  random_buffer(buffer, CHACHA20_IV_SIZE);

  if (sectrue != norcow_update_bytes(key, buffer, CHACHA20_IV_SIZE)) {
    return secfalse;
  }
  // Encrypt all blocks except for the last one.
  chacha20poly1305_ctx ctx = {0};
  rfc7539_init(&ctx, cached_dek, buffer);
  rfc7539_auth(&ctx, (const uint8_t *)&key, sizeof(key));
  size_t i = 0;
  for (i = 0; i + CHACHA20_BLOCK_SIZE < len; i += CHACHA20_BLOCK_SIZE) {
    chacha20poly1305_encrypt(&ctx, ((const uint8_t *)val) + i, buffer,
                             CHACHA20_BLOCK_SIZE);
    if (sectrue != norcow_update_bytes(key, buffer, CHACHA20_BLOCK_SIZE)) {
      memzero(&ctx, sizeof(ctx));
      memzero(buffer, sizeof(buffer));
      return secfalse;
    }
  }

  // Encrypt final block and compute message authentication tag.
  chacha20poly1305_encrypt(&ctx, ((const uint8_t *)val) + i, buffer, len - i);
  secbool ret = norcow_update_bytes(key, buffer, len - i);
  if (sectrue == ret) {
    rfc7539_finish(&ctx, sizeof(key), len, buffer);
    ret = norcow_update_bytes(key, buffer, POLY1305_TAG_SIZE);
  }
  memzero(&ctx, sizeof(ctx));
  memzero(buffer, sizeof(buffer));
  return ret;
}

secbool storage_set(const uint16_t key, const void *val, const uint16_t len) {
  const uint8_t app = key >> 8;

  // APP == 0 is reserved for PIN related values
  if (sectrue != initialized || app == APP_STORAGE) {
    return secfalse;
  }

  if (sectrue != unlocked && (app & FLAGS_WRITE) != FLAGS_WRITE) {
    return secfalse;
  }

  secbool ret = secfalse;
  if ((app & FLAG_PUBLIC) != 0) {
    ret = norcow_set(key, val, len);
  } else {
    ret = storage_set_encrypted(key, val, len);
  }
  return ret;
}

secbool storage_delete(const uint16_t key) {
  const uint8_t app = key >> 8;

  // APP == 0 is reserved for storage related values
  if (sectrue != initialized || app == APP_STORAGE) {
    return secfalse;
  }

  if (sectrue != unlocked && (app & FLAGS_WRITE) != FLAGS_WRITE) {
    return secfalse;
  }

  secbool ret = norcow_delete(key);
  if (sectrue == ret) {
    ret = auth_update(key);
  }
  return ret;
}

secbool storage_set_counter(const uint16_t key, const uint32_t count) {
  const uint8_t app = key >> 8;
  if ((app & FLAG_PUBLIC) == 0) {
    return secfalse;
  }

  // APP == 0 is reserved for PIN related values
  if (sectrue != initialized || app == APP_STORAGE) {
    return secfalse;
  }

  if (sectrue != unlocked && (app & FLAGS_WRITE) != FLAGS_WRITE) {
    return secfalse;
  }

  return norcow_set_counter(key, count);
}

secbool storage_next_counter(const uint16_t key, uint32_t *count) {
  const uint8_t app = key >> 8;

  if ((app & FLAG_PUBLIC) == 0) {
    return secfalse;
  }

  // APP == 0 is reserved for PIN related values
  if (sectrue != initialized || app == APP_STORAGE ||
      (app & FLAG_PUBLIC) == 0) {
    return secfalse;
  }

  if (sectrue != unlocked && (app & FLAGS_WRITE) != FLAGS_WRITE) {
    return secfalse;
  }

  return norcow_next_counter(key, count);
}

secbool storage_has_pin(void) {
  if (sectrue != initialized) {
    return secfalse;
  }

  const void *val = NULL;
  uint16_t len = 0;
  if (sectrue != norcow_get(PIN_NOT_SET_KEY, &val, &len) ||
      (len > 0 && *(uint8_t *)val != FALSE_BYTE)) {
    return secfalse;
  }
  return sectrue;
}

uint32_t storage_get_pin_rem(void) {
  if (sectrue != initialized) {
    return 0;
  }

  uint32_t ctr_mcu = 0;
  if (sectrue != pin_get_fails(&ctr_mcu)) {
    return 0;
  }

#if USE_OPTIGA
  // Synchronize counters in case they diverged.
  uint32_t ctr_optiga = 0;
  ensure(
      optiga_pin_get_fails(&ctr_optiga) == OPTIGA_SUCCESS ? sectrue : secfalse,
      "optiga_pin_get_fails failed");

  while (ctr_mcu < ctr_optiga) {
    storage_pin_fails_increase();
    ctr_mcu++;
  }

  if (ctr_optiga < ctr_mcu) {
    ensure(optiga_pin_fails_increase(ctr_mcu - ctr_optiga) == OPTIGA_SUCCESS
               ? sectrue
               : secfalse,
           "optiga_pin_fails_increase failed");
  }
#endif

  return PIN_MAX_TRIES - ctr_mcu;
}

secbool storage_change_pin(const uint8_t *oldpin, size_t oldpin_len,
                           const uint8_t *newpin, size_t newpin_len,
                           const uint8_t *old_ext_salt,
                           const uint8_t *new_ext_salt) {
  if (sectrue != initialized || oldpin == NULL || newpin == NULL) {
    return secfalse;
  }

  ui_total = 2 * PIN_DERIVE_MS;
  ui_rem = ui_total;
  ui_message =
      (oldpin_len != 0 && newpin_len == 0) ? VERIFYING_PIN_MSG : PROCESSING_MSG;

  if (sectrue != unlock(oldpin, oldpin_len, old_ext_salt)) {
    return secfalse;
  }

  // Fail if the new PIN is the same as the wipe code.
  if (sectrue != is_not_wipe_code(newpin, newpin_len)) {
    return secfalse;
  }

  return set_pin(newpin, newpin_len, new_ext_salt);
}

void storage_ensure_not_wipe_code(const uint8_t *pin, size_t pin_len) {
  // If we are unlocking the storage during upgrade from version 2 or lower,
  // then encode the PIN to the old format.
  uint32_t legacy_pin = 0;
  if (get_lock_version() <= 2) {
    legacy_pin = pin_to_int(pin, pin_len);
    pin = (const uint8_t *)&legacy_pin;
    pin_len = sizeof(legacy_pin);
  }

  ensure_not_wipe_code(pin, pin_len);
  memzero(&legacy_pin, sizeof(legacy_pin));
}

secbool storage_has_wipe_code(void) {
  if (sectrue != initialized || sectrue != unlocked) {
    return secfalse;
  }

  return is_not_wipe_code(WIPE_CODE_EMPTY, WIPE_CODE_EMPTY_LEN);
}

secbool storage_change_wipe_code(const uint8_t *pin, size_t pin_len,
                                 const uint8_t *ext_salt,
                                 const uint8_t *wipe_code,
                                 size_t wipe_code_len) {
  if (sectrue != initialized || pin == NULL || wipe_code == NULL ||
      (pin_len != 0 && pin_len == wipe_code_len &&
       memcmp(pin, wipe_code, pin_len) == 0)) {
    return secfalse;
  }

  ui_total = PIN_DERIVE_MS;
  ui_rem = ui_total;
  ui_message =
      (pin_len != 0 && wipe_code_len == 0) ? VERIFYING_PIN_MSG : PROCESSING_MSG;

  secbool ret = secfalse;
  if (sectrue == unlock(pin, pin_len, ext_salt)) {
    ret = set_wipe_code(wipe_code, wipe_code_len);
  }
  return ret;
}

void storage_wipe(void) {
  norcow_wipe();
  norcow_active_version = NORCOW_VERSION;
  memzero(authentication_sum, sizeof(authentication_sum));
  memzero(cached_keys, sizeof(cached_keys));
  init_wiped_storage();
}

static void __handle_fault(const char *msg, const char *file, int line,
                           const char *func) {
  static secbool in_progress = secfalse;

  // If fault handling is already in progress, then we are probably facing a
  // fault injection attack, so wipe.
  if (secfalse != in_progress) {
    storage_wipe();
    __fatal_error("Fault detected", msg, file, line, func);
  }

  // We use the PIN fail counter as a fault counter. Increment the counter,
  // check that it was incremented and halt.
  in_progress = sectrue;
  uint32_t ctr = 0;
  if (sectrue != pin_get_fails(&ctr)) {
    storage_wipe();
    __fatal_error("Fault detected", msg, file, line, func);
  }

  if (sectrue != storage_pin_fails_increase()) {
    storage_wipe();
    __fatal_error("Fault detected", msg, file, line, func);
  }

  uint32_t ctr_new = 0;
  if (sectrue != pin_get_fails(&ctr_new) || ctr + 1 != ctr_new) {
    storage_wipe();
  }
  __fatal_error("Fault detected", msg, file, line, func);
}

/*
 * Reads the PIN fail counter in version 0 format. Returns the current number of
 * failed PIN entries.
 */
static secbool v0_pin_get_fails(uint32_t *ctr) {
  const uint16_t V0_PIN_FAIL_KEY = 0x0001;
  // The PIN_FAIL_KEY points to an area of words, initialized to
  // 0xffffffff (meaning no PIN failures).  The first non-zero word
  // in this area is the current PIN failure counter.  If  PIN_FAIL_KEY
  // has no configuration or is empty, the PIN failure counter is 0.
  // We rely on the fact that flash allows to clear bits and we clear one
  // bit to indicate PIN failure.  On success, the word is set to 0,
  // indicating that the next word is the PIN failure counter.

  // Find the current pin failure counter
  const void *val = NULL;
  uint16_t len = 0;
  if (secfalse != norcow_get(V0_PIN_FAIL_KEY, &val, &len)) {
    for (unsigned int i = 0; i < len / sizeof(uint32_t); i++) {
      uint32_t word = ((const uint32_t *)val)[i];
      if (word != 0) {
        *ctr = hamming_weight(~word);
        return sectrue;
      }
    }
  }

  // No PIN failures
  *ctr = 0;
  return sectrue;
}

// Legacy conversion of PIN to the uint32 scheme that was used prior to storage
// version 3.
static uint32_t pin_to_int(const uint8_t *pin, size_t pin_len) {
  if (pin_len > V0_MAX_PIN_LEN) {
    return 0;
  }

  uint32_t val = 1;
  size_t i = 0;
  for (i = 0; i < pin_len; ++i) {
    if (pin[i] < '0' || pin[i] > '9') {
      return 0;
    }
    val = 10 * val + pin[i] - '0';
  }

  return val;
}

// Legacy conversion of wipe code from the uint32 scheme that was used prior to
// storage version 3.
static char *int_to_wipe_code(uint32_t val) {
  static char wipe_code[V0_MAX_PIN_LEN + 1] = {0};
  size_t pos = sizeof(wipe_code) - 1;
  wipe_code[pos] = '\0';

  // Handle the special representation of an empty wipe code.
  if (val == V2_WIPE_CODE_EMPTY) {
    return &wipe_code[pos];
  }

  if (val == V0_PIN_EMPTY) {
    return NULL;
  }

  // Convert a non-empty wipe code.
  while (val != 1) {
    if (pos == 0) {
      return NULL;
    }
    pos--;
    wipe_code[pos] = '0' + (val % 10);
    val /= 10;
  }
  return &wipe_code[pos];
}

static secbool storage_upgrade(void) {
  // Storage version 0: plaintext norcow
  // Storage version 1: encrypted norcow
  // Storage version 2: adds 9 digit wipe code
  // Storage version 3: adds variable length PIN and wipe code
  // Storage version 4: changes data structure of encrypted data

  const uint16_t V0_PIN_KEY = 0x0000;
  const uint16_t V0_PIN_FAIL_KEY = 0x0001;
  uint16_t key = 0;
  uint16_t len = 0;
  const void *val = NULL;
  secbool ret = secfalse;

  if (norcow_active_version == 0) {
    random_buffer(cached_keys, sizeof(cached_keys));

    // Initialize the storage authentication tag.
    auth_init();

    // Set the new storage version number.
    uint32_t version = 1;
    if (sectrue !=
        storage_set_encrypted(VERSION_KEY, &version, sizeof(version))) {
      return secfalse;
    }

    // Set EDEK_PVC_KEY and PIN_NOT_SET_KEY.
    ui_total = PIN_DERIVE_MS;
    ui_rem = ui_total;
    ui_message = PROCESSING_MSG;
    secbool found = norcow_get(V0_PIN_KEY, &val, &len);
    if (sectrue == found && *(const uint32_t *)val != V0_PIN_EMPTY) {
      set_pin((const uint8_t *)val, len, NULL);
    } else {
      set_pin((const uint8_t *)&V0_PIN_EMPTY, sizeof(V0_PIN_EMPTY), NULL);
      ret = norcow_set(PIN_NOT_SET_KEY, &TRUE_BYTE, sizeof(TRUE_BYTE));
    }

    // Convert PIN failure counter.
    uint32_t fails = 0;
    v0_pin_get_fails(&fails);
    pin_logs_init(fails);

    // Copy the remaining entries (encrypting the protected ones).
    uint32_t offset = 0;
    while (sectrue == norcow_get_next(&offset, &key, &val, &len)) {
      if (key == V0_PIN_KEY || key == V0_PIN_FAIL_KEY) {
        continue;
      }

      if (((key >> 8) & FLAG_PUBLIC) != 0) {
        ret = norcow_set(key, val, len);
      } else {
        ret = storage_set_encrypted(key, val, len);
      }

      if (sectrue != ret) {
        return secfalse;
      }
    }

    unlocked = secfalse;
    memzero(cached_keys, sizeof(cached_keys));
  } else {
    // Copy all entries.
    uint32_t offset = 0;
    while (sectrue == norcow_get_next(&offset, &key, &val, &len)) {
      if (norcow_active_version < 4) {
        // Change data structure for encrypted entries.
        const uint8_t app = key >> 8;

        if (((app & FLAG_PUBLIC) == 0) &&
            !(key == PIN_LOGS_KEY || key == EDEK_PVC_KEY ||
              key == PIN_NOT_SET_KEY || key == STORAGE_TAG_KEY ||
              key == WIPE_CODE_DATA_KEY || key == STORAGE_UPGRADED_KEY ||
              key == UNAUTH_VERSION_KEY)) {
          if (sectrue != norcow_set(key, NULL, len)) {
            return secfalse;
          }
          if (sectrue !=
              norcow_update_bytes(key, ((uint8_t *)val), CHACHA20_IV_SIZE)) {
            return secfalse;
          }
          if (sectrue !=
              norcow_update_bytes(
                  key, ((uint8_t *)val) + CHACHA20_IV_SIZE + POLY1305_TAG_SIZE,
                  len - CHACHA20_IV_SIZE - POLY1305_TAG_SIZE)) {
            return secfalse;
          }
          if (sectrue !=
              norcow_update_bytes(key, ((uint8_t *)val) + CHACHA20_IV_SIZE,
                                  POLY1305_TAG_SIZE)) {
            return secfalse;
          }
        } else {
          if (sectrue != norcow_set(key, val, len)) {
            return secfalse;
          }
        }
      } else {
        if (sectrue != norcow_set(key, val, len)) {
          return secfalse;
        }
      }
    }
  }

  // Set wipe code.
  if (norcow_active_version <= 1) {
    if (sectrue != set_wipe_code(WIPE_CODE_EMPTY, WIPE_CODE_EMPTY_LEN)) {
      return secfalse;
    }
  }

  if (norcow_active_version <= 2) {
    // Set UNAUTH_VERSION_KEY, so that it matches VERSION_KEY.
    uint32_t version = 1;

    // The storage may have gone through an upgrade to version 2 without having
    // been unlocked. We can tell by looking at STORAGE_UPGRADED_KEY.
    if (sectrue == norcow_get(STORAGE_UPGRADED_KEY, &val, &len) &&
        len == sizeof(FALSE_WORD) && *((uint32_t *)val) == FALSE_WORD) {
      version = 2;
    }

    if (sectrue != norcow_set(UNAUTH_VERSION_KEY, &version, sizeof(version))) {
      return secfalse;
    }
  }

  norcow_set(STORAGE_UPGRADED_KEY, &TRUE_WORD, sizeof(TRUE_WORD));

  norcow_active_version = NORCOW_VERSION;
  return norcow_upgrade_finish();
}

static secbool storage_upgrade_unlocked(const uint8_t *pin, size_t pin_len,
                                        const uint8_t *ext_salt) {
  uint32_t version = 0;
  uint16_t len = 0;
  if (sectrue !=
          storage_get_encrypted(VERSION_KEY, &version, sizeof(version), &len) ||
      len != sizeof(version)) {
    handle_fault("storage version check");
    return secfalse;
  }

  secbool ret = sectrue;
  if (version <= 2) {
    // Upgrade EDEK_PVC_KEY from the old uint32 PIN scheme to the new
    // variable-length PIN scheme.
    if (sectrue != set_pin(pin, pin_len, ext_salt)) {
      return secfalse;
    }
  }

  if (version == 2) {
    // Upgrade WIPE_CODE_DATA_KEY from the old uint32 scheme to the new
    // variable-length scheme.
    const void *wipe_code_data = NULL;
    if (sectrue != norcow_get(WIPE_CODE_DATA_KEY, &wipe_code_data, &len) ||
        len < sizeof(uint32_t)) {
      handle_fault("no wipe code");
      return secfalse;
    }

    char *wipe_code = int_to_wipe_code(*(uint32_t *)wipe_code_data);
    if (wipe_code == NULL) {
      handle_fault("invalid wipe code");
      return secfalse;
    }

    size_t wipe_code_len = strnlen(wipe_code, V0_MAX_PIN_LEN);
    ret = set_wipe_code((const uint8_t *)wipe_code, wipe_code_len);
    memzero(wipe_code, wipe_code_len);
  }

  // nothing to do for upgrading to version 4

  return ret;
}
