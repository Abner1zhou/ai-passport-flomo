// main/flomo_config.c —— 配置的 NVS 存取(设备端)。解析逻辑在 flomo_config_json.c。
#include "flomo_config.h"

#include "esp_log.h"
#include "nvs.h"

#include <string.h>

static const char *TAG = "flomo_cfg";

// NVS 命名空间与键。asr_key 是敏感值:只写 NVS,任何日志都不得打印其内容。
#define CFG_NS "flomo"
#define K_WEBHOOK  "webhook"
#define K_ASR_URL  "asr_url"
#define K_ASR_KEY  "asr_key"
#define K_ASR_MODEL "asr_model"
#define K_LANGUAGE "language"

static esp_err_t read_str(nvs_handle_t h, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;
    esp_err_t err = nvs_get_str(h, key, dst, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        dst[0] = '\0';
        return ESP_OK;              // 键不存在 = 保持调用方给的默认值语义由 load 处理
    }
    return err;
}

esp_err_t flomo_config_load(flomo_config_t *cfg)
{
    flomo_config_defaults(cfg);
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;   // 从未配置:全默认
    if (err != ESP_OK) return err;

    // 读失败(含截断风险)时退回默认,不让半份配置挡住启动。
    if ((err = read_str(h, K_WEBHOOK, cfg->webhook, sizeof(cfg->webhook))) == ESP_OK &&
        (err = read_str(h, K_ASR_URL, cfg->asr_url, sizeof(cfg->asr_url))) == ESP_OK &&
        (err = read_str(h, K_ASR_KEY, cfg->asr_key, sizeof(cfg->asr_key))) == ESP_OK &&
        (err = read_str(h, K_ASR_MODEL, cfg->asr_model, sizeof(cfg->asr_model))) == ESP_OK) {
        err = read_str(h, K_LANGUAGE, cfg->language, sizeof(cfg->language));
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "配置读取失败: %s,使用默认", esp_err_to_name(err));
        flomo_config_defaults(cfg);
    }
    return ESP_OK;
}

esp_err_t flomo_config_save(const flomo_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if ((err = nvs_set_str(h, K_WEBHOOK, cfg->webhook)) == ESP_OK &&
        (err = nvs_set_str(h, K_ASR_URL, cfg->asr_url)) == ESP_OK &&
        (err = nvs_set_str(h, K_ASR_KEY, cfg->asr_key)) == ESP_OK &&
        (err = nvs_set_str(h, K_ASR_MODEL, cfg->asr_model)) == ESP_OK &&
        (err = nvs_set_str(h, K_LANGUAGE, cfg->language)) == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "配置保存失败: %s", esp_err_to_name(err));
    return err;
}

esp_err_t flomo_config_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
