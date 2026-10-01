// main/flomo_ui_model.c —— 见 flomo_ui_model.h。
#include "flomo_ui_model.h"

flomo_action_t flomo_ui_model_step(flomo_screen_t screen,
                                   flomo_btn_t btn, flomo_ev_t ev)
{
    switch (screen) {
    case FLOMO_SCREEN_HOME:
        if (ev == FLOMO_EV_LONG && btn == FLOMO_BTN_OK) return FLOMO_ACTION_START_RECORD;
        if (ev == FLOMO_EV_CLICK && btn == FLOMO_BTN_DOWN) return FLOMO_ACTION_NEXT_PAGE;
        if (ev == FLOMO_EV_CLICK && btn == FLOMO_BTN_UP) return FLOMO_ACTION_PREV_PAGE;
        return FLOMO_ACTION_NONE;
    case FLOMO_SCREEN_RECORD:
        // 录音中时间敏感:单击即结束,UP/DOWN 取消,长按不占用(防误触继续录)。
        if (ev == FLOMO_EV_CLICK && btn == FLOMO_BTN_OK) return FLOMO_ACTION_STOP_RECORD;
        if (ev == FLOMO_EV_CLICK &&
            (btn == FLOMO_BTN_UP || btn == FLOMO_BTN_DOWN)) {
            return FLOMO_ACTION_CANCEL_RECORD;
        }
        return FLOMO_ACTION_NONE;
    case FLOMO_SCREEN_PROGRESS:
        return FLOMO_ACTION_NONE;      // 上传流程短且不可打断;失败自动回主页
    case FLOMO_SCREEN_QUEUE:
        if (ev == FLOMO_EV_CLICK && btn == FLOMO_BTN_OK) return FLOMO_ACTION_QUEUE_RETRY;
        if (ev == FLOMO_EV_LONG && btn == FLOMO_BTN_OK) return FLOMO_ACTION_QUEUE_DELETE;
        if (ev == FLOMO_EV_CLICK &&
            (btn == FLOMO_BTN_UP || btn == FLOMO_BTN_DOWN)) {
            return FLOMO_ACTION_QUEUE_CURSOR;
        }
        if (ev == FLOMO_EV_LONG && btn == FLOMO_BTN_UP) return FLOMO_ACTION_BACK;
        return FLOMO_ACTION_NONE;
    case FLOMO_SCREEN_SETTINGS:
        if (ev == FLOMO_EV_CLICK && btn == FLOMO_BTN_OK) return FLOMO_ACTION_SETTINGS_ENTER;
        if (ev == FLOMO_EV_CLICK &&
            (btn == FLOMO_BTN_UP || btn == FLOMO_BTN_DOWN)) {
            return FLOMO_ACTION_SETTINGS_CURSOR;
        }
        if (ev == FLOMO_EV_LONG && btn == FLOMO_BTN_UP) return FLOMO_ACTION_BACK;
        return FLOMO_ACTION_NONE;
    case FLOMO_SCREEN_PROVISION:
        if (ev == FLOMO_EV_CLICK && btn == FLOMO_BTN_OK) return FLOMO_ACTION_PROVISION_EXIT;
        return FLOMO_ACTION_NONE;
    default:
        return FLOMO_ACTION_NONE;
    }
}
