// main/main.c —— flomo 语音备忘录客户端入口。
// 衍生应用:不使用基线 demo 菜单;初始化顺序 = I2C → 显示/LVGL → 按键输入 →
// 电池 → littlefs 队列 → 应用 UI(内部再拉起 Wi-Fi/录音/上传)。
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "flomo_store.h"

#include "flomo_ui.h"

static const char *TAG = "main";

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
} key_event_t;

static QueueHandle_t s_key_queue;
static TaskHandle_t s_key_task;
static volatile bool s_input_ready;

// 按键回调运行于共享 esp_timer 任务:只入队,立即返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_input_ready) return;
    key_event_t item = { .btn = btn, .ev = ev };
    xQueueSend(s_key_queue, &item, 0);
}

static void input_task(void *arg)
{
    (void)arg;
    for (;;) {
        key_event_t item;
        if (xQueueReceive(s_key_queue, &item, portMAX_DELAY) == pdTRUE) {
            flomo_ui_key(item.btn, item.ev);
        }
    }
}

static esp_err_t input_init(void)
{
    s_key_queue = xQueueCreate(8, sizeof(key_event_t));
    if (!s_key_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "flomo_key", 3072, NULL, 5, &s_key_task) != pdPASS) {
        vQueueDelete(s_key_queue);
        s_key_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = bsp_button_init(on_key, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    s_input_ready = true;
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "flomo 客户端启动");
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "唤醒原因: %d", (int)wakeup);
    }

    bsp_i2c_init();

    // NVS 与队列存储必须先于 UI:配置读取/录音分配都依赖它们。
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s(配置/凭证不可用)",
                 esp_err_to_name(nvs_err));
    }
    if (flomo_store_init() != ESP_OK) {
        ESP_LOGW(TAG, "队列存储不可用:录音将失败,配网/设置仍可用");
    }

    // 显示是本应用唯一 UI 载体:失败即打日志退出,不做降级。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,无法继续");
        return;
    }
    bsp_display_backlight(100);

    // 电池/按键单项失败不阻塞应用:电量角标自动隐藏,按键失败则记录。
    bsp_battery_init();
    if (input_init() != ESP_OK) {
        ESP_LOGE(TAG, "输入初始化失败,应用不可操作");
    }

    if (bsp_lvgl_lock(1000)) {
        flomo_ui_start();
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "LVGL 锁超时,UI 未启动");
    }
}
