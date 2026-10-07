#include "oled_display.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <material_symbols.h>
#include <noto_emoji.h>

#define TAG "OledDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_1);
LV_FONT_DECLARE(font_noto_emoji_30_1);

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_1);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_1);

    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Note: SetupUI() should be called by Application::Initialize(), not in constructor
    // to ensure lvgl objects are created after the display is fully initialized.
}

void OledDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called
    if (height_ == 64) {
        SetupUI_128x64();
    } else {
        SetupUI_128x32();
    }
}

OledDisplay::~OledDisplay() {
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }

    bool is_128x64_layout = (top_bar_ != nullptr);
    if (status_bar_ != nullptr && is_128x64_layout) {
        status_label_ = nullptr;
        notification_label_ = nullptr;
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        network_label_ = nullptr;
        mute_label_ = nullptr;
        battery_label_ = nullptr;
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        if (!is_128x64_layout) {
            status_label_ = nullptr;
            notification_label_ = nullptr;
            network_label_ = nullptr;
            mute_label_ = nullptr;
            battery_label_ = nullptr;
        }
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void OledDisplay::Unlock() { lvgl_port_unlock(); }

void OledDisplay::SetChatMessage(const char* role, const char* content) {
    // Face only: never show reply text on the OLED
    (void)role;
    (void)content;
}

void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Layer 2: Status bar - for center text labels (status text / clock) */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content: face only, no reply text */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(content_, LV_FLEX_ALIGN_CENTER, 0);

    content_left_ = lv_obj_create(content_);
    lv_obj_set_size(content_left_, LV_HOR_RES, LV_PCT(100));  // face gets the full width
    lv_obj_set_style_pad_all(content_left_, 0, 0);
    lv_obj_set_style_border_width(content_left_, 0, 0);
    lv_obj_set_style_radius(content_left_, 0, 0);
    lv_obj_set_scrollbar_mode(content_left_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(content_left_, LV_OBJ_FLAG_SCROLLABLE);

    emotion_label_ = lv_label_create(content_left_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);

    // content_right_ and chat_message_label_ are intentionally not created.

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

void OledDisplay::SetupUI_128x32() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_column(container_, 0, 0);

    /* Emotion label on the left side */
    content_ = lv_obj_create(container_);
    lv_obj_set_size(content_, 32, 32);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);

    /* Right side */
    side_bar_ = lv_obj_create(container_);
    lv_obj_set_size(side_bar_, width_ - 32, 32);
    lv_obj_set_flex_flow(side_bar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(side_bar_, 0, 0);
    lv_obj_set_style_border_width(side_bar_, 0, 0);
    lv_obj_set_style_radius(side_bar_, 0, 0);
    lv_obj_set_style_pad_row(side_bar_, 0, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(side_bar_);
    lv_obj_set_size(status_bar_, width_ - 32, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_obj_set_style_pad_left(status_label_, 2, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_pad_left(notification_label_, 2, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    // Kept (empty) so the layout stays the same; SetChatMessage() never fills it.
    chat_message_label_ = lv_label_create(side_bar_);
    lv_obj_set_size(chat_message_label_, width_ - 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(chat_message_label_, 2, 0);
    lv_label_set_text(chat_message_label_, "");
}

void OledDisplay::CreateFaceUI() {
    if (face_container_ != nullptr) {
        return;
    }

    // Hide the old font-based emotion
    if (emotion_label_ != nullptr) {
        lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    }

    face_container_ = lv_obj_create(content_left_);
    lv_obj_set_size(face_container_, 64, 48);
    lv_obj_set_style_bg_opa(face_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_container_, 0, 0);
    lv_obj_set_style_pad_all(face_container_, 0, 0);
    lv_obj_set_style_radius(face_container_, 0, 0);
    lv_obj_set_scrollbar_mode(face_container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(face_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(face_container_);

    auto make_eye = [&]() {
        lv_obj_t* e = lv_obj_create(face_container_);
        lv_obj_set_style_radius(e, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(e, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(e, 0, 0);
        lv_obj_set_style_pad_all(e, 0, 0);
        lv_obj_remove_flag(e, LV_OBJ_FLAG_SCROLLABLE);
        return e;
    };
    face_left_eye_ = make_eye();
    face_right_eye_ = make_eye();

    // Curved mouth (arc): smile / frown / "O"
    face_mouth_ = lv_arc_create(face_container_);
    lv_obj_set_style_bg_opa(face_mouth_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_mouth_, 0, 0);
    lv_obj_set_style_pad_all(face_mouth_, 0, 0);
    lv_obj_remove_style(face_mouth_, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(face_mouth_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(face_mouth_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(face_mouth_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(face_mouth_, 0, LV_PART_INDICATOR);  // hide indicator
    lv_arc_set_rotation(face_mouth_, 0);
    lv_arc_set_range(face_mouth_, 0, 1);
    lv_arc_set_value(face_mouth_, 0);

    // Straight mouth (bar)
    face_mouth_2_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(face_mouth_2_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(face_mouth_2_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(face_mouth_2_, 0, 0);
    lv_obj_set_style_radius(face_mouth_2_, 1, 0);
    lv_obj_set_style_pad_all(face_mouth_2_, 0, 0);
    lv_obj_remove_flag(face_mouth_2_, LV_OBJ_FLAG_SCROLLABLE);

    DrawFace("neutral");
}

void OledDisplay::ClearFace() {
    if (face_left_eye_) {
        lv_obj_remove_flag(face_left_eye_, LV_OBJ_FLAG_HIDDEN);
    }
    if (face_right_eye_) {
        lv_obj_remove_flag(face_right_eye_, LV_OBJ_FLAG_HIDDEN);
    }
    if (face_mouth_) {
        lv_obj_add_flag(face_mouth_, LV_OBJ_FLAG_HIDDEN);
    }
    if (face_mouth_2_) {
        lv_obj_add_flag(face_mouth_2_, LV_OBJ_FLAG_HIDDEN);
    }
}

void OledDisplay::DrawFace(const char* emotion) {
    if (face_container_ == nullptr) {
        return;
    }

    ClearFace();
    if (emotion == nullptr || emotion[0] == '\0') {
        emotion = "neutral";
    }

    auto eyes = [&](int w, int h, int y, int lx = 14, int rx = 42) {
        lv_obj_set_size(face_left_eye_, w, h);
        lv_obj_set_size(face_right_eye_, w, h);
        lv_obj_set_pos(face_left_eye_, lx, y);
        lv_obj_set_pos(face_right_eye_, rx, y);
    };
    auto arc = [&](int size, int x, int y, int start, int end, int width) {
        lv_obj_set_size(face_mouth_, size, size);
        lv_obj_set_pos(face_mouth_, x, y);
        lv_arc_set_bg_angles(face_mouth_, start, end);
        lv_obj_set_style_arc_width(face_mouth_, width, LV_PART_MAIN);
        lv_obj_remove_flag(face_mouth_, LV_OBJ_FLAG_HIDDEN);
    };
    auto bar = [&](int w, int h, int x, int y) {
        lv_obj_set_size(face_mouth_2_, w, h);
        lv_obj_set_pos(face_mouth_2_, x, y);
        lv_obj_remove_flag(face_mouth_2_, LV_OBJ_FLAG_HIDDEN);
    };
    auto is = [&](std::initializer_list<const char*> names) {
        for (auto n : names) {
            if (strcmp(emotion, n) == 0) {
                return true;
            }
        }
        return false;
    };

    if (is({"happy", "laughing", "funny", "loving", "cool", "relaxed", "delicious", "kissy",
            "confident", "silly"})) {
        eyes(10, 6, 8, 13, 41);
        arc(28, 18, 10, 20, 160, 3);  // smile
    } else if (is({"winking"})) {
        eyes(8, 8, 8);
        lv_obj_set_size(face_left_eye_, 10, 2);  // one eye closed
        lv_obj_set_pos(face_left_eye_, 13, 11);
        arc(28, 18, 10, 20, 160, 3);
    } else if (is({"sad", "crying", "embarrassed"})) {
        eyes(8, 8, 10);
        arc(28, 18, 28, 200, 340, 3);  // frown
    } else if (is({"surprised", "shocking"})) {
        eyes(12, 12, 4, 12, 40);
        arc(14, 25, 26, 0, 360, 3);  // "O" mouth
    } else if (is({"angry"})) {
        eyes(12, 4, 9, 12, 40);
        bar(24, 4, 20, 30);
    } else if (is({"sleepy"})) {
        eyes(10, 2, 11, 13, 41);
        bar(10, 4, 27, 30);
    } else {
        // neutral, thinking, confused, anything unknown
        eyes(8, 8, 8);
        bar(22, 3, 21, 30);
    }
}

void OledDisplay::SetEmotion(const char* emotion) {
    ESP_LOGI(TAG, "OLED emotion: %s", emotion ? emotion : "null");

    DisplayLockGuard lock(this);

    if (content_left_ == nullptr) {
        return;
    }

    if (face_container_ == nullptr) {
        CreateFaceUI();
    }

    if (emotion == nullptr || emotion[0] == '\0') {
        DrawFace("neutral");
        return;
    }

    DrawFace(emotion);
}

void OledDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
}

void OledDisplay::SetPowerSaveMode(bool on) {
    if (panel_) {
        Settings settings("wifi", false);
        if (settings.GetBool("power_save_display_off", false)) {
            esp_lcd_panel_disp_on_off(panel_, !on);
        }
    }
    LvglDisplay::SetPowerSaveMode(on);
}