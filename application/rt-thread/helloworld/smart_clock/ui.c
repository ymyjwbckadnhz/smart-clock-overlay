/*
 * Smart clock UI - five pages: clock / weather / tech news / indices / settings
 */
#include "smart_clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include <rtdevice.h>

LV_FONT_DECLARE(sc_font_body);
LV_FONT_DECLARE(sc_font_title);
LV_FONT_DECLARE(sc_font_icon);

extern int lvgl_thread_init(void);

/* Built-in 2bpp Noto Serif body font */
static const lv_font_t *body_font = &sc_font_body;

/* VSCode Dark+ inspired palette */
#define C_BG        0x1E1E1E
#define C_NAV       0x252526
#define C_CARD      0x252526
#define C_CARD_ACT  0x37373D
#define C_LINE      0x3C3C3C
#define C_TEXT      0xD4D4D4
#define C_MUTED     0x858585
#define C_ACCENT    0x569CD6
#define C_GREEN     0x3DDC97
#define C_RED       0xFF3B30
#define C_YELLOW    0xDCDCAA
#define C_BLUE      0x569CD6
#define C_LIGHT     0xD4D4D4
#define C_SELECT    0x094771

static const char *nav_names[5] = { "时钟", "天气", "新闻", "股指", "设置" };
static const char *nav_icons[5] = {
    LV_SYMBOL_HOME, LV_SYMBOL_TINT, LV_SYMBOL_LIST,
    LV_SYMBOL_BARS, LV_SYMBOL_SETTINGS
};

static lv_obj_t *tv;
static lv_obj_t *nav_btn[5];
static lv_obj_t *nav_icon[5];
static lv_obj_t *nav_lbl[5];
static int cur_page = -1;

/* status bar */
static lv_obj_t *sb_wifi, *sb_ntp, *sb_time;

/* clock page */
static lv_obj_t *clk_time, *clk_date, *clk_week, *clk_net, *clk_ntp;

/* weather page */
static lv_obj_t *wx_city, *wx_temp, *wx_text, *wx_detail, *wx_updated, *wx_icon;
static lv_obj_t *wx_day_name[4], *wx_day_text[4], *wx_day_range[4];

/* news page */
static lv_obj_t *news_idx[8], *news_title[8], *news_updated, *news_hint;

/* stock page */
static lv_obj_t *stk_name[SC_IDX_MAX], *stk_price[SC_IDX_MAX], *stk_chg[SC_IDX_MAX];

/* settings page */
static lv_obj_t *set_status, *ta_ssid, *ta_pass, *kb, *kb_modal;
static lv_obj_t *kb_edit, *kb_active;
static lv_obj_t *set_br_pct, *set_br_slider;

static char last_news_ts[24] = "";
static int  last_news_count = -1;
static int  last_wx_code = -999;

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static lv_obj_t *mk_obj(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = mk_obj(parent, x, y, w, h, C_CARD, 18);

    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_LINE), 0);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          uint32_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *mk_button(lv_obj_t *parent, int x, int y, int w, int h,
                           const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = mk_obj(parent, x, y, w, h, C_CARD_ACT, 14);
    lv_obj_t *l = mk_label(b, text, body_font, C_TEXT, 0, (h - 24) / 2);

    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

static void set_text(lv_obj_t *l, const char *text)
{
    const char *old = lv_label_get_text(l);

    if (!old || strcmp(old, text) != 0)
        lv_label_set_text(l, text);
}

static void fmt_price(char *out, int v)
{
    int f = v % 100;

    if (f < 0)
        f = -f;
    snprintf(out, 24, "%d.%02d", v / 100, f);
}

static void fmt_signed(char *out, int v)
{
    int f = v % 100;

    if (f < 0)
        f = -f;
    snprintf(out, 24, "%s%d.%02d", v < 0 ? "-" : "+", v < 0 ? -(v / 100) : v / 100, f);
}

static void page_title(lv_obj_t *parent, const char *title, const char *sub)
{
    mk_label(parent, title, &sc_font_title, C_TEXT, 28, 14);
    mk_label(parent, sub, body_font, C_MUTED, 28, 44);
    mk_obj(parent, 28, 76, 864, 1, C_LINE, 0);
}

/* ------------------------------------------------------------------ */
/* navigation                                                         */
/* ------------------------------------------------------------------ */

static void nav_set_active(int idx)
{
    int i;

    for (i = 0; i < 5; i++) {
        lv_obj_set_style_bg_color(nav_btn[i],
                                  lv_color_hex(i == idx ? C_CARD_ACT : C_NAV), 0);
        lv_obj_set_style_text_color(nav_icon[i],
                                    lv_color_hex(i == idx ? C_ACCENT : C_MUTED), 0);
        lv_obj_set_style_text_color(nav_lbl[i],
                                    lv_color_hex(i == idx ? C_TEXT : C_MUTED), 0);
    }
}

static void nav_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    lv_tileview_set_tile_by_index(tv, idx, 0, LV_ANIM_ON);
    nav_set_active(idx);
}

static void build_nav(lv_obj_t *scr)
{
    int i;
    lv_obj_t *rail = mk_obj(scr, 0, 0, 104, 600, C_NAV, 0);
    lv_obj_t *title;

    lv_obj_set_style_border_width(rail, 1, 0);
    lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(rail, lv_color_hex(C_LINE), 0);

    title = mk_label(rail, "智能\n面板", &sc_font_title, C_TEXT, 0, 18);
    lv_obj_set_width(title, 104);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    for (i = 0; i < 5; i++) {
        lv_obj_t *b = mk_obj(rail, 8, 104 + i * 92, 88, 80, C_NAV, 16);

        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, nav_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        nav_icon[i] = mk_label(b, nav_icons[i], &lv_font_montserrat_24, C_MUTED, 0, 6);
        lv_obj_set_width(nav_icon[i], 88);
        lv_obj_set_style_text_align(nav_icon[i], LV_TEXT_ALIGN_CENTER, 0);

        nav_lbl[i] = mk_label(b, nav_names[i], body_font, C_MUTED, 0, 44);
        lv_obj_set_width(nav_lbl[i], 88);
        lv_obj_set_style_text_align(nav_lbl[i], LV_TEXT_ALIGN_CENTER, 0);

        nav_btn[i] = b;
    }
}

/* ------------------------------------------------------------------ */
/* clock page                                                         */
/* ------------------------------------------------------------------ */

static void build_clock(lv_obj_t *tile)
{
    lv_obj_t *c1, *c2, *c3;

    page_title(tile, "时钟", "NTP 网络授时 · 北京时间 UTC+8");

    clk_time = mk_label(tile, "--:--:--", &lv_font_montserrat_48, C_ACCENT, 40, 116);
    lv_obj_set_width(clk_time, 440);

    clk_date = mk_label(tile, "----年--月--日", &sc_font_title, C_TEXT, 520, 122);
    clk_week = mk_label(tile, "星期--", body_font, C_MUTED, 520, 162);

    mk_obj(tile, 492, 118, 1, 86, C_LINE, 0);

    c1 = mk_card(tile, 28, 280, 272, 150);
    mk_label(c1, "网络", body_font, C_MUTED, 20, 16);
    clk_net = mk_label(c1, "正在连接 ...", body_font, C_TEXT, 20, 50);
    lv_obj_set_width(clk_net, 232);
    lv_label_set_long_mode(clk_net, LV_LABEL_LONG_WRAP);

    c2 = mk_card(tile, 318, 280, 272, 150);
    mk_label(c2, "授时", body_font, C_MUTED, 20, 16);
    clk_ntp = mk_label(c2, "等待 NTP ...", body_font, C_TEXT, 20, 50);
    lv_obj_set_width(clk_ntp, 232);
    lv_label_set_long_mode(clk_ntp, LV_LABEL_LONG_WRAP);

    c3 = mk_card(tile, 608, 280, 284, 150);
    mk_label(c3, "时区", body_font, C_MUTED, 20, 16);
    mk_label(c3, "Asia/Shanghai\nUTC+08:00", body_font, C_TEXT, 20, 50);

    mk_label(tile, "时间由 ntp.aliyun.com 自动校准，每 10 分钟刷新", body_font,
             C_MUTED, 28, 468);
}

/* ------------------------------------------------------------------ */
/* weather page                                                       */
/* ------------------------------------------------------------------ */

static void weather_icon_set(int code)
{
    const char *g = "☁";
    uint32_t color = 0xD4D4D4;

    if (code == 0) {
        g = "☀";
        color = C_YELLOW;
    } else if (code == 1 || code == 2) {
        g = "⛅";
        color = C_YELLOW;
    } else if (code == 3) {
        g = "☁";
        color = 0xD4D4D4;
    } else if (code == 45 || code == 48) {
        g = "🌫";
        color = 0xD4D4D4;
    } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        g = "☔";
        color = C_BLUE;
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        g = "❄";
        color = 0x9CDCFE;
    } else if (code >= 95) {
        g = "⚡";
        color = C_YELLOW;
    }

    lv_label_set_text(wx_icon, g);
    lv_obj_set_style_text_color(wx_icon, lv_color_hex(color), 0);
}

static void build_weather(lv_obj_t *tile)
{
    int i;

    page_title(tile, "天气", "本地天气 · Open-Meteo");

    wx_updated = mk_label(tile, "更新 --:--", body_font, C_MUTED, 700, 18);
    lv_obj_set_width(wx_updated, 192);
    lv_obj_set_style_text_align(wx_updated, LV_TEXT_ALIGN_RIGHT, 0);

    wx_city = mk_label(tile, "定位中 ...", body_font, C_TEXT, 28, 92);
    wx_temp = mk_label(tile, "--°", &lv_font_montserrat_48, C_ACCENT, 28, 124);
    wx_text = mk_label(tile, "---", body_font, C_TEXT, 220, 150);
    wx_detail = mk_label(tile, "体感 --° · 湿度 --% · 风速 -- km/h",
                         body_font, C_MUTED, 28, 206);

    wx_icon = mk_label(tile, "☁", &sc_font_icon, 0xD4D4D4, 300, 82);
    lv_obj_set_width(wx_icon, 120);
    lv_obj_set_style_text_align(wx_icon, LV_TEXT_ALIGN_CENTER, 0);

    for (i = 0; i < 4; i++) {
        lv_obj_t *c = mk_card(tile, 28 + i * 216, 300, 200, 158);

        wx_day_name[i] = mk_label(c, i == 0 ? "今天" : "---", &sc_font_title, C_TEXT, 18, 14);
        wx_day_text[i] = mk_label(c, "---", body_font, C_MUTED, 18, 54);
        wx_day_range[i] = mk_label(c, "--°/--°", &sc_font_title, C_TEXT, 18, 94);
    }

    mk_label(tile, "数据来源 Open-Meteo，每 10 分钟刷新", body_font, C_MUTED, 28, 480);
}

/* ------------------------------------------------------------------ */
/* news page                                                          */
/* ------------------------------------------------------------------ */

static void build_news(lv_obj_t *tile)
{
    int i;

    page_title(tile, "科技热点", "IT之家滚动新闻 · 自动刷新");

    news_updated = mk_label(tile, "更新 --:--", body_font, C_MUTED, 700, 18);
    lv_obj_set_width(news_updated, 192);
    lv_obj_set_style_text_align(news_updated, LV_TEXT_ALIGN_RIGHT, 0);

    news_hint = mk_label(tile, "正在获取新闻 ...", body_font, C_MUTED, 28, 96);

    for (i = 0; i < 8; i++) {
        int y = 90 + i * 54;

        news_idx[i] = mk_label(tile, "0", &lv_font_montserrat_16, C_ACCENT, 28, y + 14);
        lv_obj_set_width(news_idx[i], 26);
        lv_obj_set_style_text_align(news_idx[i], LV_TEXT_ALIGN_CENTER, 0);

        news_title[i] = mk_label(tile, "", body_font, C_TEXT, 66, y + 12);
        lv_obj_set_width(news_title[i], 800);
        lv_label_set_long_mode(news_title[i], LV_LABEL_LONG_DOT);
        lv_obj_add_flag(news_title[i], LV_OBJ_FLAG_HIDDEN);

        if (i < 7)
            mk_obj(tile, 66, y + 48, 800, 1, C_LINE, 0);
    }
}

/* ------------------------------------------------------------------ */
/* stock page                                                         */
/* ------------------------------------------------------------------ */

static void build_stocks(lv_obj_t *tile)
{
    int i;

    page_title(tile, "股指行情", "A股 / 美股 · 每分钟刷新");

    for (i = 0; i < SC_IDX_MAX; i++) {
        int x = 28 + (i % 4) * 222;
        int y = 90 + (i / 4) * 212;
        lv_obj_t *c = mk_card(tile, x, y, 206, 192);

        stk_name[i] = mk_label(c, "---", body_font, C_MUTED, 14, 12);
        stk_price[i] = mk_label(c, "----.--", &lv_font_montserrat_28, C_TEXT, 12, 48);
        stk_chg[i] = mk_label(c, "--", body_font, C_MUTED, 14, 130);

        mk_obj(c, 14, 168, 178, 1, C_LINE, 0);
    }
}

/* ------------------------------------------------------------------ */
/* settings page                                                      */
/* ------------------------------------------------------------------ */

static void brightness_apply(int v)
{
    struct rt_device_pwm *pwm;

    if (v < 5)
        v = 5;
    if (v > 100)
        v = 100;

    pwm = (struct rt_device_pwm *)rt_device_find("pwm");
    if (pwm) {
        rt_pwm_set(pwm, 2, 1000000, 10000 * v);
        rt_pwm_enable(pwm, 2);
    }

    sc_lock();
    g_sc.brightness = v;
    sc_unlock();
}

static void set_br_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(set_br_slider);
    char buf[16];

    (void)e;
    snprintf(buf, sizeof(buf), "%d%%", v);
    lv_label_set_text(set_br_pct, buf);
    brightness_apply(v);
}

static void set_br_preset_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);

    lv_slider_set_value(set_br_slider, v, LV_ANIM_ON);
    set_br_cb(NULL);
}

static void input_cursor_hide(lv_obj_t *ta)
{
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
}

static void input_cursor_style(lv_obj_t *ta)
{
    lv_obj_set_style_bg_color(ta, lv_color_hex(C_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 1, LV_PART_CURSOR);
    lv_obj_set_style_border_color(ta, lv_color_hex(C_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_radius(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_width(ta, 2, LV_PART_CURSOR);
    lv_obj_set_style_bg_color(ta, lv_color_hex(C_SELECT), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(ta, lv_color_hex(C_TEXT), LV_PART_SELECTED);
}

static void input_blink_stop(lv_obj_t *ta)
{
    if (!ta)
        return;
    if (!((lv_textarea_t *)ta)->cursor.show &&
        !lv_obj_has_state(ta, LV_STATE_FOCUSED))
        return;
    lv_anim_delete(ta, NULL);
    ((lv_textarea_t *)ta)->cursor.show = 0;
    lv_obj_remove_state(ta, LV_STATE_FOCUSED);
    lv_obj_invalidate(ta);
}

static void kb_close(void)
{
    if (kb_edit && kb_active)
        lv_textarea_set_text(kb_active, lv_textarea_get_text(kb_edit));

    if (kb_modal) {
        if (kb)
            lv_keyboard_set_textarea(kb, NULL);
        lv_obj_delete_async(kb_modal);
        kb_modal = NULL;
        kb = NULL;
        kb_edit = NULL;
    }

    input_blink_stop(ta_ssid);
    input_blink_stop(ta_pass);
    kb_active = NULL;
}

static void kb_close_cb(lv_event_t *e)
{
    (void)e;
    kb_close();
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
        kb_close();
}

static void kb_open(lv_obj_t *ta)
{
    lv_obj_t *edit;

    kb_close();

    kb_active = ta;

    kb_modal = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(kb_modal, 0, 260);
    lv_obj_set_size(kb_modal, 1024, 340);
    lv_obj_set_style_bg_color(kb_modal, lv_color_hex(C_NAV), 0);
    lv_obj_set_style_bg_opa(kb_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb_modal, 1, 0);
    lv_obj_set_style_border_side(kb_modal, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(kb_modal, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_radius(kb_modal, 0, 0);
    lv_obj_set_style_pad_all(kb_modal, 0, 0);
    lv_obj_add_flag(kb_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(kb_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(kb_modal);

    edit = lv_textarea_create(kb_modal);
    lv_obj_set_pos(edit, 20, 6);
    lv_obj_set_size(edit, 760, 44);
    lv_textarea_set_one_line(edit, true);
    lv_textarea_set_max_length(edit, 64);
    lv_textarea_set_text(edit, lv_textarea_get_text(ta));
    lv_obj_set_style_bg_color(edit, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(edit, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(edit, 1, 0);
    lv_obj_set_style_border_color(edit, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_radius(edit, 8, 0);
    lv_obj_set_style_text_color(edit, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_text_font(edit, body_font, 0);
    lv_obj_set_style_pad_all(edit, 8, 0);
    input_cursor_style(edit);
    kb_edit = edit;

    mk_button(kb_modal, 860, 4, 144, 42, "关闭", kb_close_cb, NULL);

    kb = lv_keyboard_create(kb_modal);
    lv_obj_set_size(kb, 984, 286);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, edit);
    lv_obj_add_state(edit, LV_STATE_FOCUSED);
    lv_obj_send_event(edit, LV_EVENT_FOCUSED, NULL);
    input_blink_stop(ta_ssid);
    input_blink_stop(ta_pass);
    lv_obj_set_style_bg_color(kb, lv_color_hex(C_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x2D2D2D), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(C_LINE), LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(C_SELECT),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
}

static void ta_event_cb(lv_event_t *e)
{
    kb_open(lv_event_get_target(e));
}

static void settings_connect_cb(lv_event_t *e)
{
    const char *ssid;
    const char *pass;

    (void)e;

    /* copy the edit-bar content back to the real fields first */
    kb_close();
    ssid = lv_textarea_get_text(ta_ssid);
    pass = lv_textarea_get_text(ta_pass);

    if (sc_wifi_apply(ssid, pass) != 0) {
        set_text(set_status, "WiFi 名称不能为空（最长 32 字节）");
        return;
    }

    set_text(set_status, "正在连接，请稍候 ...");
}

static void style_textarea(lv_obj_t *ta)
{
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_text_font(ta, body_font, 0);
    lv_obj_set_style_pad_all(ta, 8, 0);
    input_cursor_hide(ta);
    /* only the edit bar above the keyboard may own the cursor */
    lv_obj_remove_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_FOCUSED, NULL);
    input_blink_stop(ta);
}

static void build_settings(lv_obj_t *tile)
{
    static const int presets[3] = { 20, 50, 80 };
    lv_obj_t *wifi, *bright;
    lv_obj_t *ta;
    int i;

    page_title(tile, "设置", "WiFi 连接 · 屏幕亮度");

    wifi = mk_card(tile, 28, 90, 864, 232);

    set_status = mk_label(wifi, "WiFi 连接中 ...", body_font, C_MUTED, 20, 14);
    lv_obj_set_width(set_status, 824);

    mk_label(wifi, "名称", body_font, C_TEXT, 20, 64);
    ta = lv_textarea_create(wifi);
    ta_ssid = ta;
    lv_obj_set_pos(ta, 100, 54);
    lv_obj_set_size(ta, 280, 52);
    lv_textarea_set_max_length(ta, 32);
    lv_textarea_set_text(ta, g_sc.wifi_ssid[0] ? g_sc.wifi_ssid : "");
    style_textarea(ta);

    mk_label(wifi, "密码", body_font, C_TEXT, 20, 126);
    ta = lv_textarea_create(wifi);
    ta_pass = ta;
    lv_obj_set_pos(ta, 100, 116);
    lv_obj_set_size(ta, 280, 52);
    lv_textarea_set_max_length(ta, 64);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_password_bullet(ta, "*");
    lv_textarea_set_password_show_time(ta, 0);
    lv_textarea_set_text(ta, g_sc.wifi_pass);
    style_textarea(ta);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);

    mk_button(wifi, 520, 54, 130, 54, "连接", settings_connect_cb, NULL);
    mk_label(wifi, "修改 WiFi 后点“连接”，配置会写入 Flash，重启后保持",
             body_font, C_MUTED, 20, 186);

    bright = mk_card(tile, 28, 338, 864, 164);

    mk_label(bright, "亮度", body_font, C_TEXT, 20, 16);
    set_br_pct = mk_label(bright, "80%", &lv_font_montserrat_28, C_ACCENT, 700, 10);
    lv_obj_set_width(set_br_pct, 140);
    lv_obj_set_style_text_align(set_br_pct, LV_TEXT_ALIGN_RIGHT, 0);

    set_br_slider = lv_slider_create(bright);
    lv_obj_set_pos(set_br_slider, 90, 66);
    lv_obj_set_size(set_br_slider, 620, 16);
    lv_slider_set_range(set_br_slider, 5, 100);
    lv_slider_set_value(set_br_slider, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(set_br_slider, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(set_br_slider, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(set_br_slider, lv_color_hex(C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_radius(set_br_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(set_br_slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(set_br_slider, set_br_cb, LV_EVENT_VALUE_CHANGED, NULL);

    for (i = 0; i < 3; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", presets[i]);
        mk_button(bright, 90 + i * 120, 104, 100, 44, buf, set_br_preset_cb,
                  (void *)(intptr_t)presets[i]);
    }
}

/* ------------------------------------------------------------------ */
/* pages / timer                                                      */
/* ------------------------------------------------------------------ */

static void build_pages(void)
{
    lv_obj_t *tile;
    int i;

    tv = lv_tileview_create(lv_screen_active());
    lv_obj_set_pos(tv, 104, 52);
    lv_obj_set_size(tv, 920, 548);
    lv_obj_set_style_bg_color(tv, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_style_pad_all(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    for (i = 0; i < 5; i++) {
        tile = lv_tileview_add_tile(tv, i, 0, LV_DIR_HOR);
        lv_obj_set_style_bg_color(tile, lv_color_hex(C_BG), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    }

    build_clock(lv_obj_get_child(tv, 0));
    build_weather(lv_obj_get_child(tv, 1));
    build_news(lv_obj_get_child(tv, 2));
    build_stocks(lv_obj_get_child(tv, 3));
    build_settings(lv_obj_get_child(tv, 4));
}

static void build_status_bar(lv_obj_t *scr)
{
    lv_obj_t *bar = mk_obj(scr, 104, 0, 920, 52, C_BG, 0);

    sb_wifi = mk_label(bar, "WiFi 连接中 ...", body_font, C_MUTED, 24, 14);
    sb_ntp = mk_label(bar, "NTP 等待", body_font, C_MUTED, 460, 14);
    sb_time = mk_label(bar, "--:--", &lv_font_montserrat_24, C_TEXT, 790, 10);
    lv_obj_set_width(sb_time, 104);
    lv_obj_set_style_text_align(sb_time, LV_TEXT_ALIGN_RIGHT, 0);
}

static void update_status_bar(void)
{
    char buf[128];
    time_t now;
    struct tm tmv;

    if (g_sc.wifi_state == 1) {
        snprintf(buf, sizeof(buf), "WiFi %s · %s", g_sc.wifi_ssid, g_sc.ip);
        set_text(sb_wifi, buf);
        lv_obj_set_style_text_color(sb_wifi, lv_color_hex(C_LIGHT), 0);
    } else {
        set_text(sb_wifi, g_sc.wifi_state == 2 ? "WiFi 连接失败" : "WiFi 连接中 ...");
        lv_obj_set_style_text_color(sb_wifi, lv_color_hex(C_MUTED), 0);
    }

    if (g_sc.time_synced) {
        set_text(sb_ntp, "NTP 已同步 · UTC+8");
        lv_obj_set_style_text_color(sb_ntp, lv_color_hex(C_LIGHT), 0);

        now = g_sc.utc_epoch + (rt_tick_get_millisecond() - g_sc.utc_tick) / 1000
              + SC_TZ_OFFSET;
        gmtime_r(&now, &tmv);
        snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        set_text(sb_time, buf);
    } else {
        set_text(sb_ntp, "NTP 等待");
        lv_obj_set_style_text_color(sb_ntp, lv_color_hex(C_MUTED), 0);
    }
}

static void update_clock_page(void)
{
    char buf[128];
    time_t now;
    struct tm tmv;
    static const char *week[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };

    if (!g_sc.time_synced) {
        set_text(clk_time, "--:--:--");
        set_text(clk_date, "等待 NTP 授时 ...");
        set_text(clk_week, "");
        return;
    }

    now = g_sc.utc_epoch + (rt_tick_get_millisecond() - g_sc.utc_tick) / 1000 + SC_TZ_OFFSET;
    gmtime_r(&now, &tmv);

    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    set_text(clk_time, buf);

    snprintf(buf, sizeof(buf), "%d年%02d月%02d日", tmv.tm_year + 1900,
             tmv.tm_mon + 1, tmv.tm_mday);
    set_text(clk_date, buf);
    set_text(clk_week, week[tmv.tm_wday]);
}

static void update_clock_cards(void)
{
    char buf[160];

    if (g_sc.wifi_state == 1)
        snprintf(buf, sizeof(buf), "%s 已连接\nIP %s\n信号 %d dBm",
                 g_sc.wifi_ssid, g_sc.ip, g_sc.rssi);
    else
        snprintf(buf, sizeof(buf), "%s %s", g_sc.wifi_ssid,
                 g_sc.wifi_state == 2 ? "连接失败" : "连接中 ...");
    set_text(clk_net, buf);

    if (g_sc.time_synced)
        snprintf(buf, sizeof(buf), "已同步\nntp.aliyun.com\n开机自动校准");
    else
        snprintf(buf, sizeof(buf), "等待同步 ...");
    set_text(clk_ntp, buf);
}

static void update_weather_page(void)
{
    char buf[128];
    int i;

    if (g_sc.weather_state == 1) {
        snprintf(buf, sizeof(buf), "%d°", g_sc.temp);
        set_text(wx_temp, buf);
        set_text(wx_city, g_sc.city);
        set_text(wx_text, g_sc.weather_text);
        if (g_sc.code != last_wx_code) {
            last_wx_code = g_sc.code;
            weather_icon_set(g_sc.code);
        }
        snprintf(buf, sizeof(buf), "体感 %d° · 湿度 %d%% · 风速 %d km/h",
                 g_sc.feels, g_sc.humidity, g_sc.wind);
        set_text(wx_detail, buf);
        snprintf(buf, sizeof(buf), "更新 %s", g_sc.weather_updated);
        set_text(wx_updated, buf);

        for (i = 0; i < 4; i++) {
            if (i >= g_sc.day_cnt)
                break;
            set_text(wx_day_name[i], g_sc.day_name[i]);
            set_text(wx_day_text[i], g_sc.day_code[i] >= 71 ? "有雪" :
                     (g_sc.day_code[i] >= 51 ? "有雨" :
                      (g_sc.day_code[i] == 0 ? "晴" : "多云")));
            snprintf(buf, sizeof(buf), "%d°/%d°", g_sc.day_min[i], g_sc.day_max[i]);
            set_text(wx_day_range[i], buf);
        }
    } else if (g_sc.weather_state == 2) {
        set_text(wx_city, "天气获取失败");
        set_text(wx_text, "---");
        set_text(wx_detail, "请检查网络后等待自动重试");
        set_text(wx_updated, "更新 --:--");
    }
}

static void update_news_page(void)
{
    int i;

    if (g_sc.news_state != 1 || g_sc.news_count <= 0) {
        if (g_sc.news_state == 2)
            set_text(news_hint, "新闻获取失败，稍后自动重试 ...");
        else
            set_text(news_hint, "正在获取新闻 ...");
        lv_obj_remove_flag(news_hint, LV_OBJ_FLAG_HIDDEN);
        for (i = 0; i < 8; i++)
            lv_obj_add_flag(news_title[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(news_hint, LV_OBJ_FLAG_HIDDEN);

    if (strcmp(last_news_ts, g_sc.news_updated) == 0 && last_news_count == g_sc.news_count)
        return;

    snprintf(last_news_ts, sizeof(last_news_ts), "%s", g_sc.news_updated);
    last_news_count = g_sc.news_count;

    {
        char buf[32];
        snprintf(buf, sizeof(buf), "更新 %s", g_sc.news_updated);
        set_text(news_updated, buf);
    }

    for (i = 0; i < 8; i++) {
        if (i < g_sc.news_count) {
            char idx[8];
            snprintf(idx, sizeof(idx), "%d", i + 1);
            set_text(news_idx[i], idx);
            set_text(news_title[i], g_sc.news[i]);
            lv_obj_remove_flag(news_title[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(news_title[i], LV_OBJ_FLAG_HIDDEN);
            set_text(news_idx[i], "");
        }
    }
}

static void update_stocks_page(void)
{
    char p[24], c[24], pc[24], buf[64];
    int i;

    for (i = 0; i < SC_IDX_MAX; i++) {
        if (g_sc.stock_state != 1 || g_sc.idx[i].price == 0) {
            set_text(stk_name[i], g_sc.stock_state == 2 ? "获取失败" : "加载中 ...");
            set_text(stk_price[i], "----.--");
            set_text(stk_chg[i], "--");
            continue;
        }

        set_text(stk_name[i], g_sc.idx[i].name);
        fmt_price(p, g_sc.idx[i].price);
        set_text(stk_price[i], p);
        fmt_signed(c, g_sc.idx[i].chg);
        fmt_signed(pc, g_sc.idx[i].pct);
        snprintf(buf, sizeof(buf), "%s (%s%%)", c, pc);
        set_text(stk_chg[i], buf);
        lv_obj_set_style_text_color(stk_chg[i],
                                    lv_color_hex(g_sc.idx[i].chg >= 0 ? C_RED : C_GREEN), 0);
        lv_obj_set_style_text_color(stk_price[i],
                                    lv_color_hex(g_sc.idx[i].chg >= 0 ? C_RED : C_GREEN), 0);
    }
}

static void update_settings_page(void)
{
    char buf[160];

    if (g_sc.wifi_state == 1) {
        snprintf(buf, sizeof(buf), "已连接 · %s · %s · %d dBm",
                 g_sc.wifi_ssid, g_sc.ip, g_sc.rssi);
        lv_obj_set_style_text_color(set_status, lv_color_hex(C_LIGHT), 0);
    } else if (g_sc.wifi_state == 2) {
        snprintf(buf, sizeof(buf), "连接失败 · 请检查名称/密码后重试");
        lv_obj_set_style_text_color(set_status, lv_color_hex(C_RED), 0);
    } else {
        snprintf(buf, sizeof(buf), "正在连接 %s ...", g_sc.wifi_ssid);
        lv_obj_set_style_text_color(set_status, lv_color_hex(C_MUTED), 0);
    }
    set_text(set_status, buf);

    snprintf(buf, sizeof(buf), "%d%%", g_sc.brightness);
    set_text(set_br_pct, buf);
}

static void ui_timer_cb(lv_timer_t *t)
{
    int page;

    (void)t;

    sc_lock();

    update_status_bar();
    update_clock_page();
    update_clock_cards();
    update_weather_page();
    update_news_page();
    update_stocks_page();
    update_settings_page();

    if (!kb_modal) {
        input_blink_stop(ta_ssid);
        input_blink_stop(ta_pass);
    }

    page = lv_obj_get_index(lv_tileview_get_tile_act(tv));
    if (page != cur_page) {
        cur_page = page;
        nav_set_active(page);
    }

    sc_unlock();
}

void lv_user_gui_init(void)
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_nav(scr);
    build_status_bar(scr);
    build_pages();
    nav_set_active(0);

    brightness_apply(80);

    lv_timer_create(ui_timer_cb, 500, NULL);
}

static int smart_clock_init(void)
{
    sc_net_start();
    lvgl_thread_init();
    return 0;
}
INIT_LATE_APP_EXPORT(smart_clock_init);

static void sc_bright_cmd(int argc, char **argv)
{
    int v;

    if (argc != 2) {
        rt_kprintf("usage: sc_bright_cmd <5..100>\n");
        return;
    }
    v = atoi(argv[1]);
    brightness_apply(v);
    if (set_br_slider)
        lv_slider_set_value(set_br_slider, v, LV_ANIM_OFF);
    rt_kprintf("brightness -> %d%%\n", v);
}
MSH_CMD_EXPORT(sc_bright_cmd, set backlight brightness 5..100);

