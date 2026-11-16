#ifndef SMART_CLOCK_H
#define SMART_CLOCK_H

#include <rtthread.h>
#include <stdbool.h>
#include <time.h>

#define SC_WIFI_SSID_DEFAULT "YOUR_WIFI_SSID"
#define SC_WIFI_PASS_DEFAULT "YOUR_WIFI_PASSWORD"
#define SC_TZ_OFFSET  (8 * 3600)
#define SC_IDX_MAX    8

typedef struct {
    const char *name;
    int price;   /* x100 */
    int chg;     /* x100 */
    int pct;     /* x100 */
} sc_index_t;

typedef struct {
    /* network */
    int  wifi_state;            /* 0 connecting, 1 connected, 2 failed */
    int  wifi_reconnect;        /* request reconnect with new credentials */
    char wifi_ssid[33];
    char wifi_pass[65];
    char ip[24];
    int  rssi;

    /* time */
    int      time_synced;
    time_t   utc_epoch;
    rt_tick_t utc_tick;

    /* weather */
    int  weather_state;         /* 0 none, 1 ok, 2 failed */
    char city[48];
    char weather_text[32];
    int  temp, feels, humidity, wind;
    int  code;
    int  day_cnt;
    int  day_min[4], day_max[4], day_code[4];
    char day_name[4][16];
    char weather_updated[24];

    /* news */
    int  news_state;
    int  news_count;
    char news[8][128];
    char news_updated[24];

    /* stock */
    int stock_state;
    int idx_count;
    sc_index_t idx[SC_IDX_MAX];

    /* brightness */
    int brightness;
} sc_data_t;

extern sc_data_t g_sc;

void sc_lock(void);
void sc_unlock(void);
void sc_net_start(void);
int  sc_wifi_apply(const char *ssid, const char *pass);

#endif
