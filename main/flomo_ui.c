// main/flomo_ui.c —— flomo 客户端 UI 与应用编排,见 flomo_ui.h。
//
// 线程模型:
//  - 按键:main 的输入任务调 flomo_ui_key() → 模型查表 → 动作(全部短操作,
//    网络/录音请求只发通知)。
//  - 状态:1s lv_timer 刷新 Wi-Fi/电量/队列数;录音 tick、上传结果、BluFi 状态
//    来自各自任务,经本文件的少量原子状态 + 各自持锁刷新标签。
//  - 熄屏:15s 无按键 backlight 0,任意按键恢复(功耗约定)。
#include "flomo_ui.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flomo_blufi.h"
#include "flomo_config.h"
#include "flomo_recorder.h"
#include "flomo_store.h"
#include "flomo_ui_model.h"
#include "flomo_uploader.h"
#include "flomo_websrv.h"
#include "flomo_wifi.h"

#include "stdio.h"
#include "string.h"

static const char *TAG = "flomo_ui";

#define UI_IDLE_DIM_MS 15000

LV_FONT_DECLARE(flomo_font_16);
LV_FONT_DECLARE(flomo_font_24);

static flomo_screen_t s_screen = FLOMO_SCREEN_HOME;
static lv_obj_t *s_scr;
static lv_obj_t *s_title;       // 顶部:页面标题(左) + 电量(右上角)
static lv_obj_t *s_battery;
static lv_obj_t *s_body;        // 中部:页面主内容标签
static lv_obj_t *s_hint;        // 底部:按键提示
static lv_timer_t *s_timer;
static int s_settings_cursor;
static volatile int32_t s_rec_sec;
static volatile int32_t s_rec_level;
static volatile flomo_blufi_state_t s_blufi_state;
static char s_blufi_info[48];
static int64_t s_result_hold_until;
static flomo_wifi_state_t s_wifi_state = FLOMO_WIFI_OFF;
static int64_t s_last_key_ms;

// 页面主文案(仅这些进入字库清单;新文案必须同步 assets/fonts/flomo_strings.txt)。
static const char *TXT_APP_TITLE = "浮墨速记";
static const char *TXT_HINT_HOME = "按住 OK 录音";
static const char *TXT_WIFI_OFF  = "未配置";
static const char *TXT_WIFI_PROV = "配网中…";
static const char *TXT_WIFI_CONN = "连接中…";
static const char *TXT_WIFI_ON   = "已连接";
static const char *TXT_WIFI_FAIL = "连接失败";
static const char *TXT_REC_RUN   = "录音中";
static const char *TXT_REC_STOP = "OK 结束";
static const char *TXT_REC_CAN  = "上键取消";
static const char *TXT_ASR      = "转文字中…";
static const char *TXT_DONE     = "已同步";
static const char *TXT_FAIL_Q   = "失败·已排队";
static const char *TXT_QUEUE    = "待传队列";
static const char *TXT_EMPTY    = "空";
static const char *TXT_RETRY    = "OK 重传";
static const char *TXT_DEL      = "长按删除";
static const char *TXT_SETTINGS = "设置";
static const char *TXT_PROV     = "开始配网";
static const char *TXT_CLEAR    = "清除配置";
static const char *TXT_CLEARED  = "已清除";
static const char *TXT_BACK     = "返回";
static const char *TXT_POWER_OFF = "关机";
static const char *TXT_LOW_BAT  = "电量低";

static const char *SETTINGS_ITEMS[] = { NULL, NULL, NULL };   // 运行时填充

// ---- 基础绘制 ------------------------------------------------------------

// 全新布局:顶部标题条 + 右上角电量,中部大内容区,底部提示条。
// 中文一律显式选 flomo 字体;数字/英文混排也在该字体内(含 ASCII)。
static void screen_base_create(const char *title)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_pad_all(s_scr, 10, 0);

    s_title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_title, &flomo_font_24, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xE8ECE8), 0);
    lv_label_set_text(s_title, title);
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_battery = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_battery, &flomo_font_16, 0);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(0x9AB0A0), 0);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, 0, 4);

    s_body = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_body, &flomo_font_16, 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(0xD8E2D8), 0);
    lv_obj_set_width(s_body, 216);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_body, LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(s_body, "");

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &flomo_font_16, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x7C8C80), 0);
    lv_obj_set_width(s_hint, 216);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(s_hint, "");

    lv_screen_load(s_scr);
}

static void battery_refresh(void)
{
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_obj_set_hidden(s_battery, true);   // 读失败:优雅隐藏
        return;
    }
    lv_obj_set_hidden(s_battery, false);
    lv_label_set_text_fmt(s_battery, "%d%%", soc);
}

// ---- 各页面 --------------------------------------------------------------

static void home_refresh(void)
{
    char line[96];
    const char *wifi = TXT_WIFI_OFF;
    if (s_blufi_state == FLOMO_BLUFI_ADVERTISING ||
        s_blufi_state == FLOMO_BLUFI_BLE_CONNECTED ||
        s_blufi_state == FLOMO_BLUFI_WIFI_CONNECTING) {
        wifi = TXT_WIFI_PROV;
    } else if (s_wifi_state == FLOMO_WIFI_CONNECTED) {
        wifi = TXT_WIFI_ON;
    } else if (s_wifi_state == FLOMO_WIFI_CONNECTING) {
        wifi = TXT_WIFI_CONN;
    } else if (flomo_wifi_has_credentials()) {
        wifi = TXT_WIFI_FAIL;
    }
    size_t queued = flomo_store_count();
    int soc = bsp_battery_soc();
    const char *low = (soc >= 0 && soc < 15) ? TXT_LOW_BAT : "";
    snprintf(line, sizeof(line), "%s\n\n%zu 条待传%s%s",
             wifi, queued, low[0] ? "\n" : "", low);
    lv_label_set_text(s_body, line);
    lv_label_set_text(s_hint, TXT_HINT_HOME);
}

static void record_refresh(void)
{
    char line[64];
    snprintf(line, sizeof(line), "%s\n\n%02d:%02d\n\n%ld%%",
             TXT_REC_RUN, (int)(s_rec_sec / 60), (int)(s_rec_sec % 60),
             (long)s_rec_level);
    lv_label_set_text(s_body, line);
    char hint[48];
    snprintf(hint, sizeof(hint), "%s  %s", TXT_REC_STOP, TXT_REC_CAN);
    lv_label_set_text(s_hint, hint);
}

static void queue_refresh(void)
{
    flomo_memo_t memo;
    char line[96];
    if (flomo_store_peek_oldest(&memo)) {
        size_t queued = flomo_store_count();
        uint32_t secs = memo.data_bytes / (FLOMO_RECORDER_SAMPLE_HZ * 2);
        // 纯数字 + 库存文案,避免字库外字符。
        snprintf(line, sizeof(line), "%zu 条待传\n\n%02lu:%02lu\n%u/5",
                 queued, (unsigned long)(secs / 60), (unsigned long)(secs % 60),
                 (unsigned)memo.attempts);
    } else {
        snprintf(line, sizeof(line), "%s", TXT_EMPTY);
    }
    lv_label_set_text(s_body, line);
    char hint[48];
    snprintf(hint, sizeof(hint), "%s  %s", TXT_RETRY, TXT_DEL);
    lv_label_set_text(s_hint, hint);
}

static void settings_refresh(void)
{
    char line[128];
    snprintf(line, sizeof(line), "> %s\n  %s\n  %s",
             SETTINGS_ITEMS[s_settings_cursor],
             SETTINGS_ITEMS[(s_settings_cursor + 1) % 3],
             SETTINGS_ITEMS[(s_settings_cursor + 2) % 3]);
    lv_label_set_text(s_body, line);
    lv_label_set_text(s_hint, TXT_BACK);
}

static void provision_refresh(void)
{
    const char *st = TXT_WIFI_PROV;
    if (s_blufi_state == FLOMO_BLUFI_BLE_CONNECTED) st = TXT_WIFI_PROV;
    if (s_blufi_state == FLOMO_BLUFI_WIFI_CONNECTED) st = TXT_WIFI_ON;
    if (s_blufi_state == FLOMO_BLUFI_FAILED) st = TXT_WIFI_FAIL;
    char line[112];
    snprintf(line, sizeof(line), "%s\n\n%s\n\n%s", st, s_blufi_info, TXT_BACK);
    lv_label_set_text(s_body, line);
    lv_label_set_text(s_hint, TXT_BACK);
}

static void page_open(flomo_screen_t screen)
{
    s_screen = screen;
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    switch (screen) {
    case FLOMO_SCREEN_HOME:
        screen_base_create(TXT_APP_TITLE);
        home_refresh();
        break;
    case FLOMO_SCREEN_RECORD:
        screen_base_create(TXT_APP_TITLE);
        record_refresh();
        break;
    case FLOMO_SCREEN_PROGRESS:
        screen_base_create(TXT_APP_TITLE);
        break;
    case FLOMO_SCREEN_QUEUE:
        screen_base_create(TXT_QUEUE);
        queue_refresh();
        break;
    case FLOMO_SCREEN_SETTINGS:
        screen_base_create(TXT_SETTINGS);
        settings_refresh();
        break;
    case FLOMO_SCREEN_PROVISION:
        screen_base_create(TXT_PROV);
        provision_refresh();
        break;
    default:
        break;
    }
    battery_refresh();
}

// ---- 回调(录音/上传/配网/网络任务上下文)--------------------------------

static void progress_show(const char *text)
{
    if (s_screen != FLOMO_SCREEN_PROGRESS) page_open(FLOMO_SCREEN_PROGRESS);
    lv_label_set_text(s_body, text);
}

static void on_rec_tick(uint32_t sec, uint8_t level, void *user)
{
    (void)user;
    s_rec_sec = (int32_t)sec;
    s_rec_level = (int32_t)level;
    if (s_screen == FLOMO_SCREEN_RECORD && bsp_lvgl_lock(100)) {
        record_refresh();
        bsp_lvgl_unlock();
    }
}

static void on_rec_done(const char *path, void *user)
{
    (void)path;
    (void)user;
    if (bsp_lvgl_lock(500)) {
        progress_show(TXT_ASR);
        bsp_lvgl_unlock();
    }
    flomo_uploader_kick();
}

static void on_upload_result(flomo_sync_result_t result, const char *detail, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;
    switch (result) {
    case FLOMO_SYNC_OK:
        if (s_screen == FLOMO_SCREEN_PROGRESS) lv_label_set_text(s_body, TXT_DONE);
        break;
    case FLOMO_SYNC_QUEUED_RETRY:
    case FLOMO_SYNC_DROPPED:
        if (s_screen == FLOMO_SCREEN_PROGRESS) lv_label_set_text(s_body, TXT_FAIL_Q);
        break;
    default:
        break;
    }
    (void)detail;
    // 结果停留由 1s 定时器在 3s 后拉回主页(见 tick)。
    s_result_hold_until = esp_timer_get_time() / 1000 + 3000;
    bsp_lvgl_unlock();
}

static void on_blufi_state(flomo_blufi_state_t state, const char *ssid,
                           const char *ip, void *user)
{
    (void)user;
    s_blufi_state = state;
    s_blufi_info[0] = '\0';
    if (ssid) snprintf(s_blufi_info, sizeof(s_blufi_info), "%s", ssid);
    if (ip) snprintf(s_blufi_info + strlen(s_blufi_info),
                     sizeof(s_blufi_info) - strlen(s_blufi_info), " %s", ip);
    if (state == FLOMO_BLUFI_WIFI_CONNECTED) {
        // 配网完成:停 BluFi 释放 BLE,回主页;Wi-Fi 由 flomo_wifi 维持。
        flomo_blufi_stop();
        if (bsp_lvgl_lock(500)) {
            page_open(FLOMO_SCREEN_HOME);
            bsp_lvgl_unlock();
        }
    }
}

static void on_config_saved(void *user)
{
    (void)user;
    flomo_config_t cfg;
    if (flomo_config_load(&cfg) == ESP_OK) {
        flomo_uploader_set_config(&cfg);
        flomo_uploader_kick();
    }
}

static size_t on_queue_count(void *user)
{
    (void)user;
    return flomo_store_count();
}

static void on_wifi_state(flomo_wifi_state_t state, const char *ip, void *user)
{
    (void)user;
    s_wifi_state = state;
    if (state == FLOMO_WIFI_CONNECTED && ip) {
        flomo_websrv_start(&(flomo_websrv_hooks_t){
            .on_config_saved = on_config_saved,
            .queue_count = on_queue_count,
            .user = NULL,
        });
        flomo_uploader_kick();
    }
}

// ---- 动作 ----------------------------------------------------------------

static void action_start_record(void)
{
    if (flomo_recorder_start(on_rec_done, on_rec_tick, NULL) == ESP_OK) {
        s_rec_sec = 0;
        s_rec_level = 0;
        page_open(FLOMO_SCREEN_RECORD);
    } else {
        // 存储满/音频失败:留在主页,队列页会给出上下文。
        ESP_LOGW(TAG, "record start failed");
    }
}

static void action_power_off(void)
{
    // 手动关机:deep sleep。板卡无已验证的按键唤醒电路(见 demo_low_power 注释),
    // 唤醒 = 复位键/重新上电,此限制已写入应用文档。
    bsp_battery_sleep();
    bsp_audio_sleep();
    bsp_audio_prepare_deep_sleep();
    if (bsp_lvgl_lock(1000)) {
        bsp_display_prepare_deep_sleep();
        esp_deep_sleep_start();
    }
    esp_restart();      // prepare 后未睡成:重启恢复外设
}

static void dispatch(flomo_action_t action)
{
    switch (action) {
    case FLOMO_ACTION_START_RECORD:
        action_start_record();
        break;
    case FLOMO_ACTION_STOP_RECORD:
        flomo_recorder_stop();
        break;
    case FLOMO_ACTION_CANCEL_RECORD:
        flomo_recorder_cancel();
        page_open(FLOMO_SCREEN_HOME);
        break;
    case FLOMO_ACTION_NEXT_PAGE:
        page_open(s_screen == FLOMO_SCREEN_HOME ? FLOMO_SCREEN_QUEUE
                                                : FLOMO_SCREEN_SETTINGS);
        break;
    case FLOMO_ACTION_PREV_PAGE:
        page_open(s_screen == FLOMO_SCREEN_QUEUE ? FLOMO_SCREEN_HOME
                                                 : FLOMO_SCREEN_SETTINGS);
        break;
    case FLOMO_ACTION_QUEUE_RETRY:
        flomo_uploader_kick();
        break;
    case FLOMO_ACTION_QUEUE_DELETE: {
        flomo_memo_t memo;
        if (flomo_store_peek_oldest(&memo)) {
            flomo_store_remove(&memo);
            queue_refresh();
        }
        break;
    }
    case FLOMO_ACTION_QUEUE_CURSOR:
    case FLOMO_ACTION_SETTINGS_CURSOR:
        if (s_screen == FLOMO_SCREEN_SETTINGS) {
            s_settings_cursor = (s_settings_cursor + 1) % 3;
            settings_refresh();
        }
        break;
    case FLOMO_ACTION_SETTINGS_ENTER:
        if (s_settings_cursor == 0) {
            s_blufi_state = FLOMO_BLUFI_OFF;
            page_open(FLOMO_SCREEN_PROVISION);
            flomo_blufi_start(on_blufi_state, NULL);
        } else if (s_settings_cursor == 1) {
            flomo_config_clear();
            flomo_wifi_forget();
            flomo_config_t cfg;
            flomo_config_defaults(&cfg);
            flomo_uploader_set_config(&cfg);
            lv_label_set_text(s_body, TXT_CLEARED);
        } else {
            action_power_off();
        }
        break;
    case FLOMO_ACTION_BACK:
    case FLOMO_ACTION_PROVISION_EXIT:
        if (s_blufi_state != FLOMO_BLUFI_OFF) {
            flomo_blufi_stop();
            s_blufi_state = FLOMO_BLUFI_OFF;
        }
        page_open(FLOMO_SCREEN_HOME);
        break;
    default:
        break;
    }
}

// ---- 定时器与入口 --------------------------------------------------------

static void tick(lv_timer_t *timer)
{
    (void)timer;
    battery_refresh();
    int64_t now = esp_timer_get_time() / 1000;
    if (now - s_last_key_ms > UI_IDLE_DIM_MS) {
        bsp_display_backlight(0);
    }
    if (s_screen == FLOMO_SCREEN_HOME) home_refresh();
    if (s_screen == FLOMO_SCREEN_QUEUE) queue_refresh();
    if (s_screen == FLOMO_SCREEN_PROVISION) provision_refresh();
    if (s_screen == FLOMO_SCREEN_PROGRESS && s_result_hold_until &&
        now >= s_result_hold_until) {
        s_result_hold_until = 0;
        page_open(FLOMO_SCREEN_HOME);
    }
    // 周期性补传:队列非空且空闲时每分钟 kick 一轮(覆盖退避窗口后的重试)。
    static int64_t last_kick;
    if (flomo_store_count() > 0 && now - last_kick > 60000) {
        last_kick = now;
        flomo_uploader_kick();
    }
}

void flomo_ui_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    flomo_btn_t b = (flomo_btn_t)btn;
    flomo_ev_t e = (ev == BSP_BTN_LONG) ? FLOMO_EV_LONG : FLOMO_EV_CLICK;
    flomo_action_t action = flomo_ui_model_step(s_screen, b, e);
    s_last_key_ms = esp_timer_get_time() / 1000;
    bsp_display_backlight(100);
    if (action == FLOMO_ACTION_NONE) return;
    if (!bsp_lvgl_lock(250)) return;
    dispatch(action);
    bsp_lvgl_unlock();
}

esp_err_t flomo_ui_start(void)
{
    SETTINGS_ITEMS[0] = TXT_PROV;
    SETTINGS_ITEMS[1] = TXT_CLEAR;
    SETTINGS_ITEMS[2] = TXT_POWER_OFF;

    flomo_config_t cfg;
    flomo_config_load(&cfg);
    flomo_uploader_set_config(&cfg);

    flomo_uploader_init(on_upload_result, NULL);
    flomo_recorder_init();
    flomo_wifi_start(on_wifi_state, NULL);

    s_last_key_ms = esp_timer_get_time() / 1000;
    page_open(FLOMO_SCREEN_HOME);
    s_timer = lv_timer_create(tick, 1000, NULL);
    return ESP_OK;
}
