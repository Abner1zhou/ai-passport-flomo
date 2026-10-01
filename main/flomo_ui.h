// main/flomo_ui.h —— flomo 客户端应用层:重设计的独立 UI + 应用编排。
// 不使用基线 demo 测试菜单/ui_pixel 外壳(衍生应用强制重设计规则)。
// 依赖已在 app_main 完成初始化:display/LVGL、store、config、wifi、uploader、recorder。
#pragma once

#include "bsp_button.h"
#include "esp_err.h"

// 创建主页并启动 1s 状态定时器。持 LVGL 锁调用。
esp_err_t flomo_ui_start(void);

// 按键入口(main 的输入任务调用;内部查 flomo_ui_model 后分发)。
void flomo_ui_key(bsp_btn_t btn, bsp_btn_ev_t ev);
