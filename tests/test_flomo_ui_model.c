// tests/test_flomo_ui_model.c —— 三键映射穷举:所有 (屏,键,事件) 组合的语义。
#include <assert.h>

#include "flomo_ui_model.h"

int main(void)
{
    // HOME:长按 OK 录音;单击 DOWN/UP 翻页;其他组合无动作。
    assert(flomo_ui_model_step(FLOMO_SCREEN_HOME, FLOMO_BTN_OK, FLOMO_EV_LONG) ==
           FLOMO_ACTION_START_RECORD);
    assert(flomo_ui_model_step(FLOMO_SCREEN_HOME, FLOMO_BTN_DOWN, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_NEXT_PAGE);
    assert(flomo_ui_model_step(FLOMO_SCREEN_HOME, FLOMO_BTN_UP, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_PREV_PAGE);
    assert(flomo_ui_model_step(FLOMO_SCREEN_HOME, FLOMO_BTN_OK, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_NONE);
    assert(flomo_ui_model_step(FLOMO_SCREEN_HOME, FLOMO_BTN_DOWN, FLOMO_EV_LONG) ==
           FLOMO_ACTION_NONE);

    // RECORD:单击 OK 结束;UP/DOWN 单击取消;长按无动作(防手抖误停)。
    assert(flomo_ui_model_step(FLOMO_SCREEN_RECORD, FLOMO_BTN_OK, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_STOP_RECORD);
    assert(flomo_ui_model_step(FLOMO_SCREEN_RECORD, FLOMO_BTN_UP, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_CANCEL_RECORD);
    assert(flomo_ui_model_step(FLOMO_SCREEN_RECORD, FLOMO_BTN_DOWN, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_CANCEL_RECORD);
    assert(flomo_ui_model_step(FLOMO_SCREEN_RECORD, FLOMO_BTN_OK, FLOMO_EV_LONG) ==
           FLOMO_ACTION_NONE);

    // PROGRESS:上传流程不可打断。
    for (int b = 0; b <= FLOMO_BTN_OK; b++) {
        for (int e = 0; e <= FLOMO_EV_LONG; e++) {
            assert(flomo_ui_model_step(FLOMO_SCREEN_PROGRESS,
                                       (flomo_btn_t)b, (flomo_ev_t)e) ==
                   FLOMO_ACTION_NONE);
        }
    }

    // QUEUE:OK 单击重传/长按删除;UP/DOWN 移动光标;长按 UP 返回。
    assert(flomo_ui_model_step(FLOMO_SCREEN_QUEUE, FLOMO_BTN_OK, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_QUEUE_RETRY);
    assert(flomo_ui_model_step(FLOMO_SCREEN_QUEUE, FLOMO_BTN_OK, FLOMO_EV_LONG) ==
           FLOMO_ACTION_QUEUE_DELETE);
    assert(flomo_ui_model_step(FLOMO_SCREEN_QUEUE, FLOMO_BTN_UP, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_QUEUE_CURSOR);
    assert(flomo_ui_model_step(FLOMO_SCREEN_QUEUE, FLOMO_BTN_DOWN, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_QUEUE_CURSOR);
    assert(flomo_ui_model_step(FLOMO_SCREEN_QUEUE, FLOMO_BTN_UP, FLOMO_EV_LONG) ==
           FLOMO_ACTION_BACK);

    // SETTINGS:同 QUEUE 的导航结构。
    assert(flomo_ui_model_step(FLOMO_SCREEN_SETTINGS, FLOMO_BTN_OK, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_SETTINGS_ENTER);
    assert(flomo_ui_model_step(FLOMO_SCREEN_SETTINGS, FLOMO_BTN_DOWN, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_SETTINGS_CURSOR);
    assert(flomo_ui_model_step(FLOMO_SCREEN_SETTINGS, FLOMO_BTN_UP, FLOMO_EV_LONG) ==
           FLOMO_ACTION_BACK);

    // PROVISION:单击 OK 中止;其余忽略。
    assert(flomo_ui_model_step(FLOMO_SCREEN_PROVISION, FLOMO_BTN_OK, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_PROVISION_EXIT);
    assert(flomo_ui_model_step(FLOMO_SCREEN_PROVISION, FLOMO_BTN_DOWN, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_NONE);

    // 越界屏幕安全返回 NONE。
    assert(flomo_ui_model_step(FLOMO_SCREEN_COUNT, FLOMO_BTN_OK, FLOMO_EV_CLICK) ==
           FLOMO_ACTION_NONE);
    return 0;
}
