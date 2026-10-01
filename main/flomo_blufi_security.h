// main/flomo_blufi_security.h —— BLUFI 安全协商(DHM 密钥交换 + AES-CFB128 + MD5)。
// 移植自 demo/blufi-provisioning 分支 main/demo_blufi_security.h(源自 ESP-IDF 5.5
// BLUFI security 示例):仅重命名公开符号 demo_blufi_* → flomo_blufi_security_*,
// 算法与错误路径保持一致。
#pragma once

#include <stdbool.h>
#include <stdint.h>

void flomo_blufi_security_negotiate(uint8_t *data, int len, uint8_t **output_data,
                                    int *output_len, bool *need_free);
int flomo_blufi_security_encrypt(uint8_t iv8, uint8_t *data, int len);
int flomo_blufi_security_decrypt(uint8_t iv8, uint8_t *data, int len);
uint16_t flomo_blufi_security_checksum(uint8_t iv8, uint8_t *data, int len);
int flomo_blufi_security_init(void);
void flomo_blufi_security_deinit(void);
