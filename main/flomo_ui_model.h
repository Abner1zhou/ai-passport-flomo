// main/flomo_ui_model.h —— 三键输入 → 应用动作的映射表,纯逻辑、主机可测。
// 设备端 flomo_ui.c 把 bsp 按键事件转成本枚举后查表,保证交互语义可穷举测试。
#pragma once

#include <stdbool.h>

// 与 bsp_btn_t 一一对应,但不包含 ESP 头,便于主机编译。
typedef enum {
    FLOMO_BTN_UP = 0,
    FLOMO_BTN_DOWN,
    FLOMO_BTN_OK,
} flomo_btn_t;

typedef enum {
    FLOMO_EV_CLICK = 0,
    FLOMO_EV_LONG,
} flomo_ev_t;

typedef enum {
    FLOMO_SCREEN_HOME = 0,     // 主页:状态 + 队列数 + 录音提示
    FLOMO_SCREEN_RECORD,       // 录音中
    FLOMO_SCREEN_PROGRESS,     // 转文字/发送中(不可打断,按键忽略)
    FLOMO_SCREEN_QUEUE,        // 待传队列
    FLOMO_SCREEN_SETTINGS,     // 设置(配网/清配置/关机)
    FLOMO_SCREEN_PROVISION,    // BluFi 配网进行中
    FLOMO_SCREEN_COUNT,
} flomo_screen_t;

typedef enum {
    FLOMO_ACTION_NONE = 0,
    FLOMO_ACTION_START_RECORD,   // HOME: 长按 OK
    FLOMO_ACTION_STOP_RECORD,    // RECORD: 单击 OK(结束并进入上传)
    FLOMO_ACTION_CANCEL_RECORD,  // RECORD: 单击 UP/DOWN(丢弃)
    FLOMO_ACTION_NEXT_PAGE,      // HOME: 单击 DOWN
    FLOMO_ACTION_PREV_PAGE,      // HOME: 单击 UP
    FLOMO_ACTION_QUEUE_RETRY,    // QUEUE: 单击 OK(立即重传队首)
    FLOMO_ACTION_QUEUE_DELETE,   // QUEUE: 长按 OK(删除选中项)
    FLOMO_ACTION_QUEUE_CURSOR,   // QUEUE: 单击 UP/DOWN(移动选中,-1/+1)
    FLOMO_ACTION_SETTINGS_ENTER, // SETTINGS: 单击 OK(执行选中项)
    FLOMO_ACTION_SETTINGS_CURSOR,// SETTINGS: 单击 UP/DOWN
    FLOMO_ACTION_PROVISION_EXIT, // PROVISION: 单击 OK(中止配网返回)
    FLOMO_ACTION_BACK,           // QUEUE/SETTINGS: 长按 UP(返回主页)
} flomo_action_t;

// 查表。progress 屏忽略一切输入;未定义组合返回 NONE。
flomo_action_t flomo_ui_model_step(flomo_screen_t screen,
                                   flomo_btn_t btn, flomo_ev_t ev);
