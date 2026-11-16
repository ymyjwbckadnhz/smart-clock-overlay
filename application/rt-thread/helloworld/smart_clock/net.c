/*
 * Smart clock network layer: WiFi + DNS + SNTP + HTTP data sources.
 */
#include "smart_clock.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#include <lwip/sockets.h>
#include <lwip/api.h>
#include <lwip/dns.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <lwip/ip_addr.h>

#include <wifi/wifi_conf.h>
#include <cJSON.h>
#include <rtdevice.h>
#include <fal.h>

#define SC_HTTP_BUF     32768
#define SC_TAG          "[SC] "

sc_data_t g_sc;

static struct rt_mutex sc_mutex;

#define WIFI_CFG_MAGIC 0x57494643u /* "WIFC" */

struct wifi_cfg_store {
    uint32_t magic;
    char ssid[33];
    char pass[65];
    uint32_t sum;
};

static uint32_t wifi_cfg_sum(const struct wifi_cfg_store *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t v = 0x12345678u;
    size_t i;

    for (i = 0; i < sizeof(*s) - sizeof(s->sum); i++)
        v = v * 33u + p[i];
    return v;
}

static void wifi_cfg_load(void)
{
    const struct fal_partition *part = fal_partition_find("data");
    struct wifi_cfg_store s;

    if (!part)
        return;
    if (fal_partition_read(part, 0, (uint8_t *)&s, sizeof(s)) < 0)
        return;
    if (s.magic != WIFI_CFG_MAGIC || s.sum != wifi_cfg_sum(&s))
        return;
    s.ssid[sizeof(s.ssid) - 1] = 0;
    s.pass[sizeof(s.pass) - 1] = 0;
    if (s.ssid[0])
        snprintf(g_sc.wifi_ssid, sizeof(g_sc.wifi_ssid), "%s", s.ssid);
    snprintf(g_sc.wifi_pass, sizeof(g_sc.wifi_pass), "%s", s.pass);
}

static int wifi_cfg_save(void)
{
    const struct fal_partition *part = fal_partition_find("data");
    struct wifi_cfg_store s;

    if (!part)
        return -1;

    memset(&s, 0, sizeof(s));
    s.magic = WIFI_CFG_MAGIC;
    sc_lock();
    snprintf(s.ssid, sizeof(s.ssid), "%s", g_sc.wifi_ssid);
    snprintf(s.pass, sizeof(s.pass), "%s", g_sc.wifi_pass);
    sc_unlock();
    s.sum = wifi_cfg_sum(&s);

    fal_partition_erase(part, 0, 4096);
    if (fal_partition_write(part, 0, (uint8_t *)&s, sizeof(s)) < 0)
        return -1;
    return 0;
}

void sc_lock(void)
{
    rt_mutex_take(&sc_mutex, RT_WAITING_FOREVER);
}

void sc_unlock(void)
{
    rt_mutex_release(&sc_mutex);
}

/* ------------------------------------------------------------------ */
/* HTTP client                                                        */
/* ------------------------------------------------------------------ */

static char  http_buf[SC_HTTP_BUF];
static char *http_body;
static int   http_body_len;

static int resolve_host(const char *host, ip_addr_t *ip)
{
    if (netconn_gethostbyname(host, ip) != ERR_OK)
        return -1;
    return 0;
}

static char *ci_find(char *hay, const char *needle)
{
    size_t n = strlen(needle);
    char *p;

    for (p = hay; *p; p++) {
        if (strncasecmp(p, needle, n) == 0)
            return p;
    }
    return NULL;
}

static int dechunk(char *body, int len)
{
    char *r = body, *w = body, *end = body + len;

    while (r < end) {
        char *e;
        long sz = strtol(r, &e, 16);

        if (e == r || sz < 0)
            break;
        r = strstr(r, "\r\n");
        if (!r)
            break;
        r += 2;
        if (r + sz > end)
            sz = end - r;
        if (sz > 0) {
            memmove(w, r, sz);
            w += sz;
            r += sz;
        }
        if (r + 1 < end && r[0] == '\r' && r[1] == '\n')
            r += 2;
        if (sz == 0)
            break;
    }
    *w = 0;
    return (int)(w - body);
}

/*
 * GET http://host/path, response body is stored in http_body/http_body_len.
 * Return 0 when HTTP 200 and a body was received.
 */
static int http_get(const char *host, const char *path, int timeout_ms)
{
    ip_addr_t ip;
    struct sockaddr_in sa;
    struct timeval tv;
    char req[512];
    char *he, *body, *cl;
    int s, n, total = 0, code = 0;

    http_body = NULL;
    http_body_len = 0;

    if (resolve_host(host, &ip) != 0) {
        rt_kprintf(SC_TAG "DNS failed: %s\r\n", host);
        return -1;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    sa.sin_addr.s_addr = ip4_addr_get_u32(ip_2_ip4(&ip));

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        closesocket(s);
        rt_kprintf(SC_TAG "connect failed: %s\r\n", host);
        return -1;
    }

    n = snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                 "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36\r\n"
                 "Accept: application/json, text/plain, */*\r\n"
                 "Referer: http://www.ithome.com/\r\n"
                 "Connection: close\r\n\r\n",
                 path, host);
    if (send(s, req, n, 0) != n) {
        closesocket(s);
        return -1;
    }

    while (total < (int)sizeof(http_buf) - 1) {
        n = recv(s, http_buf + total, sizeof(http_buf) - 1 - total, 0);
        if (n <= 0)
            break;
        total += n;
    }
    closesocket(s);
    http_buf[total] = 0;
    if (total <= 0)
        return -1;

    he = strstr(http_buf, "\r\n\r\n");
    if (!he)
        return -1;

    body = he + 4;

    /* status code */
    {
        char *sp = strchr(http_buf, ' ');
        if (sp)
            code = atoi(sp + 1);
    }
    if (code != 200) {
        rt_kprintf(SC_TAG "%s -> HTTP %d\r\n", host, code);
        return -1;
    }

    *he = 0;
    cl = ci_find(http_buf, "content-length:");
    if (ci_find(http_buf, "transfer-encoding: chunked")) {
        *he = '\r';
        http_body_len = dechunk(body, total - (int)(body - http_buf));
        http_body = body;
        return 0;
    }
    *he = '\r';

    http_body_len = total - (int)(body - http_buf);
    if (cl) {
        int want = atoi(cl + 15);
        if (want >= 0 && want < http_body_len)
            http_body_len = want;
    }
    body[http_body_len] = 0;
    http_body = body;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Weather                                                            */
/* ------------------------------------------------------------------ */

static const char *wmo_text(int code)
{
    switch (code) {
    case 0:  return "晴";
    case 1:  return "少云";
    case 2:  return "多云";
    case 3:  return "阴";
    case 45: case 48: return "雾";
    case 51: case 53: case 55: return "毛毛雨";
    case 56: case 57: return "冻雨";
    case 61: return "小雨";
    case 63: return "中雨";
    case 65: return "大雨";
    case 66: case 67: return "冻雨";
    case 71: return "小雪";
    case 73: return "中雪";
    case 75: return "大雪";
    case 77: return "雪粒";
    case 80: return "阵雨";
    case 81: return "强阵雨";
    case 82: return "暴雨";
    case 85: return "阵雪";
    case 86: return "强阵雪";
    case 95: return "雷阵雨";
    case 96: case 99: return "雷暴冰雹";
    default: return "未知";
    }
}

static const char *week_name(int y, int m, int d)
{
    static const char *names[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int w;

    if (m < 3)
        y -= 1;
    w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
    if (w < 0)
        w += 7;
    return names[w];
}

static int fetch_weather(void)
{
    double lat = 39.9042, lon = 116.4074;
    char path[320];
    char city[48] = "北京";
    cJSON *root, *cur, *daily, *it;
    int i, n;
    time_t now;
    struct tm tmv;

    /* 1. locate by public IP */
    if (http_get("ip-api.com",
                 "/json/?fields=status,country,regionName,city,lat,lon&lang=zh-CN",
                 6000) == 0) {
        root = cJSON_Parse(http_body);
        if (root) {
            cJSON *st = cJSON_GetObjectItem(root, "status");
            if (st && st->valuestring && strcmp(st->valuestring, "success") == 0) {
                cJSON *c = cJSON_GetObjectItem(root, "city");
                cJSON *la = cJSON_GetObjectItem(root, "lat");
                cJSON *lo = cJSON_GetObjectItem(root, "lon");
                if (c && c->valuestring)
                    snprintf(city, sizeof(city), "%s", c->valuestring);
                if (la && lo) {
                    lat = la->valuedouble;
                    lon = lo->valuedouble;
                }
            }
            cJSON_Delete(root);
        }
    }

    snprintf(path, sizeof(path),
             "/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
             "weather_code,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min"
             "&timezone=Asia%%2FShanghai&forecast_days=4",
             lat, lon);

    if (http_get("api.open-meteo.com", path, 10000) != 0) {
        g_sc.weather_state = 2;
        return -1;
    }

    root = cJSON_Parse(http_body);
    if (!root) {
        g_sc.weather_state = 2;
        return -1;
    }

    cur = cJSON_GetObjectItem(root, "current");
    if (!cur) {
        cJSON_Delete(root);
        g_sc.weather_state = 2;
        return -1;
    }

    sc_lock();
    snprintf(g_sc.city, sizeof(g_sc.city), "%s", city);

    it = cJSON_GetObjectItem(cur, "temperature_2m");
    g_sc.temp = it ? (int)(it->valuedouble + (it->valuedouble >= 0 ? 0.5 : -0.5)) : 0;
    it = cJSON_GetObjectItem(cur, "apparent_temperature");
    g_sc.feels = it ? (int)(it->valuedouble + (it->valuedouble >= 0 ? 0.5 : -0.5)) : 0;
    it = cJSON_GetObjectItem(cur, "relative_humidity_2m");
    g_sc.humidity = it ? (int)it->valuedouble : 0;
    it = cJSON_GetObjectItem(cur, "wind_speed_10m");
    g_sc.wind = it ? (int)(it->valuedouble + 0.5) : 0;
    it = cJSON_GetObjectItem(cur, "weather_code");
    g_sc.code = it ? (int)it->valuedouble : 0;
    snprintf(g_sc.weather_text, sizeof(g_sc.weather_text), "%s", wmo_text(g_sc.code));

    daily = cJSON_GetObjectItem(root, "daily");
    g_sc.day_cnt = 0;
    if (daily) {
        cJSON *tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *tmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON *dc = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *days = cJSON_GetObjectItem(daily, "time");
        n = cJSON_GetArraySize(tmax);
        if (n > 4)
            n = 4;
        for (i = 0; i < n; i++) {
            cJSON *a = cJSON_GetArrayItem(tmax, i);
            cJSON *b = cJSON_GetArrayItem(tmin, i);
            cJSON *d = dc ? cJSON_GetArrayItem(dc, i) : NULL;
            cJSON *ds = days ? cJSON_GetArrayItem(days, i) : NULL;
            g_sc.day_max[i] = a ? (int)(a->valuedouble + 0.5) : 0;
            g_sc.day_min[i] = b ? (int)(b->valuedouble + 0.5) : 0;
            g_sc.day_code[i] = d ? (int)d->valuedouble : 0;
            if (ds && ds->valuestring) {
                int y = 0, m = 0, dd = 0;
                sscanf(ds->valuestring, "%d-%d-%d", &y, &m, &dd);
                snprintf(g_sc.day_name[i], sizeof(g_sc.day_name[i]), "%s",
                         "今天");
                if (i > 0 && y > 0)
                    snprintf(g_sc.day_name[i], sizeof(g_sc.day_name[i]), "%s",
                             week_name(y, m, dd));
            } else {
                snprintf(g_sc.day_name[i], sizeof(g_sc.day_name[i]), "%s",
                         i == 0 ? "今天" : (i == 1 ? "明天" : (i == 2 ? "后天" : "第4天")));
            }
        }
        g_sc.day_cnt = n;
    }

    now = g_sc.time_synced ? g_sc.utc_epoch + SC_TZ_OFFSET : time(NULL);
    gmtime_r(&now, &tmv);
    snprintf(g_sc.weather_updated, sizeof(g_sc.weather_updated), "%02d:%02d",
             tmv.tm_hour, tmv.tm_min);
    g_sc.weather_state = 1;
    sc_unlock();

    cJSON_Delete(root);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tech news (Sina RSS)                                               */
/* ------------------------------------------------------------------ */

static void html_trim(char *s)
{
    char *p = s;
    char *d;
    int n;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);

    n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;

    /* decode a few common entities */
    d = s;
    while (*s) {
        if (strncmp(s, "&amp;", 5) == 0) { *d++ = '&'; s += 5; }
        else if (strncmp(s, "&lt;", 4) == 0) { *d++ = '<'; s += 4; }
        else if (strncmp(s, "&gt;", 4) == 0) { *d++ = '>'; s += 4; }
        else if (strncmp(s, "&quot;", 6) == 0) { *d++ = '"'; s += 6; }
        else if (strncmp(s, "&#39;", 5) == 0) { *d++ = '\''; s += 5; }
        else if (strncmp(s, "&nbsp;", 6) == 0) { *d++ = ' '; s += 6; }
        else *d++ = *s++;
    }
    *d = 0;
}

static int fetch_news(void)
{
    cJSON *root, *list, *item, *t;
    int i, n, cnt = 0;
    time_t now;
    struct tm tmv;

    if (http_get("api.ithome.com", "/json/newslist/news", 10000) != 0) {
        g_sc.news_state = 2;
        return -1;
    }

    root = cJSON_Parse(http_body);
    if (!root) {
        rt_kprintf(SC_TAG "news json fail: %.140s\r\n", http_body ? http_body : "");
        g_sc.news_state = 2;
        return -1;
    }

    list = cJSON_GetObjectItem(root, "newslist");
    n = cJSON_GetArraySize(list);

    sc_lock();
    g_sc.news_count = 0;
    for (i = 0; i < n && cnt < 8; i++) {
        item = cJSON_GetArrayItem(list, i);
        t = cJSON_GetObjectItem(item, "title");
        if (t && t->valuestring && t->valuestring[0]) {
            snprintf(g_sc.news[cnt], sizeof(g_sc.news[cnt]), "%s", t->valuestring);
            html_trim(g_sc.news[cnt]);
            if (g_sc.news[cnt][0])
                cnt++;
        }
    }
    g_sc.news_count = cnt;
    now = g_sc.time_synced ? g_sc.utc_epoch + SC_TZ_OFFSET : time(NULL);
    gmtime_r(&now, &tmv);
    snprintf(g_sc.news_updated, sizeof(g_sc.news_updated), "%02d:%02d",
             tmv.tm_hour, tmv.tm_min);
    g_sc.news_state = cnt > 0 ? 1 : 2;
    sc_unlock();

    cJSON_Delete(root);
    return cnt > 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Stock indices (Tencent quote)                                       */
/* ------------------------------------------------------------------ */

static const char *stock_vars[SC_IDX_MAX] = {
    "v_sh000001=", "v_sz399001=", "v_sz399006=", "v_sh000300=",
    "v_usDJI=", "v_usIXIC=", "v_usINX=", "v_usNDX="
};
static const char *stock_names[SC_IDX_MAX] = {
    "上证指数", "深证成指", "创业板指", "沪深300",
    "道琼斯", "纳斯达克", "标普500", "纳指100"
};

static void parse_stock(const char *var, const char *name, sc_index_t *out)
{
    char *v, *e;
    char tmp[512];
    char *f[40];
    int n = 0, len;
    char *p;

    memset(out, 0, sizeof(*out));
    out->name = name;

    v = strstr(http_body, var);
    if (!v)
        return;
    v = strchr(v, '"');
    if (!v)
        return;
    v++;
    e = strchr(v, '"');
    if (!e)
        return;
    len = (int)(e - v);
    if (len > (int)sizeof(tmp) - 1)
        len = sizeof(tmp) - 1;
    memcpy(tmp, v, len);
    tmp[len] = 0;

    /*
     * Tencent quotes have an empty field (index 29).  strtok() would merge
     * consecutive delimiters and shift the change/percent fields, so split
     * manually and keep empty fields.
     */
    p = tmp;
    while (n < 40) {
        f[n++] = p;
        e = strchr(p, '~');
        if (!e)
            break;
        *e = 0;
        p = e + 1;
    }
    if (n < 33)
        return;

    out->price = (int)(atof(f[3]) * 100 + (atof(f[3]) >= 0 ? 0.5 : -0.5));
    out->chg = (int)(atof(f[31]) * 100 + (atof(f[31]) >= 0 ? 0.5 : -0.5));
    out->pct = (int)(atof(f[32]) * 100 + (atof(f[32]) >= 0 ? 0.5 : -0.5));
}

static int fetch_stocks(void)
{
    int i;

    if (http_get("qt.gtimg.cn",
                 "/q=sh000001,sz399001,sz399006,sh000300,usDJI,usIXIC,usINX,usNDX",
                 8000) != 0) {
        g_sc.stock_state = 2;
        return -1;
    }

    sc_lock();
    for (i = 0; i < SC_IDX_MAX; i++)
        parse_stock(stock_vars[i], stock_names[i], &g_sc.idx[i]);
    g_sc.idx_count = SC_IDX_MAX;
    g_sc.stock_state = g_sc.idx[0].price > 0 ? 1 : 2;
    sc_unlock();
    return g_sc.stock_state == 1 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* SNTP                                                               */
/* ------------------------------------------------------------------ */

static time_t ntp_query(const char *host)
{
    ip_addr_t ip;
    struct sockaddr_in sa;
    struct timeval tv = { 3, 0 };
    unsigned char pkt[48];
    uint32_t secs;
    int s, n;

    if (resolve_host(host, &ip) != 0)
        return 0;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(123);
    sa.sin_addr.s_addr = ip4_addr_get_u32(ip_2_ip4(&ip));

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x1B; /* LI=0, VN=3, mode=3 (client) */

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        closesocket(s);
        return 0;
    }
    if (send(s, pkt, sizeof(pkt), 0) != (int)sizeof(pkt)) {
        closesocket(s);
        return 0;
    }
    n = recv(s, pkt, sizeof(pkt), 0);
    closesocket(s);
    if (n < 48)
        return 0;

    secs = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
           ((uint32_t)pkt[42] << 8) | pkt[43];
    if (secs < 2208988800UL)
        return 0;
    return (time_t)(secs - 2208988800UL);
}

static int sync_ntp(void)
{
    time_t t;
    static const char *servers[] = {
        "ntp.aliyun.com", "cn.pool.ntp.org", "time.windows.com"
    };
    int i;
    struct tm tmv;

    for (i = 0; i < 3; i++) {
        t = ntp_query(servers[i]);
        if (t > 0) {
            sc_lock();
            g_sc.utc_epoch = t;
            g_sc.utc_tick = rt_tick_get_millisecond();
            g_sc.time_synced = 1;
            sc_unlock();

            /* keep the RT-Thread RTC in local time as well */
            t += SC_TZ_OFFSET;
            gmtime_r(&t, &tmv);
            set_date(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
            set_time(tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

            rt_kprintf(SC_TAG "NTP ok via %s\r\n", servers[i]);
            return 0;
        }
    }
    rt_kprintf(SC_TAG "NTP failed\r\n");
    return -1;
}

/* ------------------------------------------------------------------ */
/* WiFi                                                               */
/* ------------------------------------------------------------------ */

static int wait_for_ip(int ms)
{
    int i;

    for (i = 0; i < ms / 100; i++) {
        struct netif *nif = netif_default;
        if (nif && ip4_addr_get_u32(netif_ip4_addr(nif)) != 0)
            return 0;
        rt_thread_mdelay(100);
    }
    return -1;
}

static void set_dns(void)
{
    ip_addr_t d;

    IP_ADDR4(&d, 223, 5, 5, 5);
    dns_setserver(0, &d);
    IP_ADDR4(&d, 119, 29, 29, 29);
    dns_setserver(1, &d);
}

static int wait_wifi_ready(int ms)
{
    int i;

    for (i = 0; i < ms / 200; i++) {
        if (wifi_is_up(RTW_STA_INTERFACE))
            return 0;
        rt_thread_mdelay(200);
    }
    return -1;
}

static int wifi_join(void)
{
    int ret;
    char ssid[33], pass[65];

    sc_lock();
    snprintf(ssid, sizeof(ssid), "%s", g_sc.wifi_ssid);
    snprintf(pass, sizeof(pass), "%s", g_sc.wifi_pass);
    sc_unlock();

    /*
     * The Realtek driver performs wifi_on() by itself during boot.  Calling
     * wifi_on() here as well races with that init and can crash the SDIO
     * probe, so only wait for the driver to become ready.
     */
    if (wait_wifi_ready(20000) != 0) {
        rt_kprintf(SC_TAG "wifi driver not ready\r\n");
        return -1;
    }

    ret = wifi_connect(ssid, RTW_SECURITY_WPA2_AES_PSK,
                       pass, (int)strlen(ssid), (int)strlen(pass),
                       0, NULL);
    if (ret != RTW_SUCCESS) {
        rt_kprintf(SC_TAG "wifi connect failed (%d)\r\n", ret);
        return -1;
    }

    set_dns();

    if (wait_for_ip(4000) != 0) {
        struct netif *nif = netif_default;
        if (nif) {
            rt_kprintf(SC_TAG "start DHCP manually\r\n");
            dhcp_start(nif);
            wait_for_ip(8000);
        }
    }

    {
        struct netif *nif = netif_default;
        if (!nif || ip4_addr_get_u32(netif_ip4_addr(nif)) == 0) {
            rt_kprintf(SC_TAG "no IP address\r\n");
            return -1;
        }
        sc_lock();
        snprintf(g_sc.ip, sizeof(g_sc.ip), "%s", ip4addr_ntoa(netif_ip4_addr(nif)));
        sc_unlock();
    }

    {
        int rssi = 0;
        if (wifi_get_rssi(&rssi) == 0) {
            sc_lock();
            g_sc.rssi = rssi;
            sc_unlock();
        }
    }

    rt_kprintf(SC_TAG "wifi connected, IP %s\r\n", g_sc.ip);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                      */
/* ------------------------------------------------------------------ */

static void wifi_clear_ip(void)
{
    struct netif *nif = netif_default;
    ip_addr_t zero;

    if (!nif)
        return;
    dhcp_stop(nif);
    IP_ADDR4(&zero, 0, 0, 0, 0);
    netif_set_ipaddr(nif, &zero);
    netif_set_gw(nif, &zero);
    netif_set_netmask(nif, &zero);
}

static void net_thread_entry(void *arg)
{
    int tick = 0;

    (void)arg;

    /* let the WiFi driver finish its asynchronous boot init first */
    rt_thread_mdelay(3000);

    while (1) {
        if (g_sc.wifi_reconnect) {
            g_sc.wifi_reconnect = 0;
            rt_kprintf(SC_TAG "wifi reconfigure\r\n");
            wifi_disconnect();
            wifi_clear_ip();
            g_sc.wifi_state = 0;
            rt_thread_mdelay(800);
        }

        if (g_sc.wifi_state != 1) {
            g_sc.wifi_state = 0;
            if (wifi_join() == 0) {
                g_sc.wifi_state = 1;
                tick = 0;
            } else {
                g_sc.wifi_state = 2;
                rt_thread_mdelay(5000);
                continue;
            }
        }

        if (!g_sc.time_synced)
            sync_ntp();

        if (tick % 10 == 0 || (g_sc.weather_state != 1 && tick % 5 == 0))
            fetch_weather();
        if (tick % 10 == 0 || (g_sc.news_state != 1 && tick % 5 == 0))
            fetch_news();
        if (tick % 1 == 0 || g_sc.stock_state != 1)
            fetch_stocks();

        tick++;
        {
            int i;
            for (i = 0; i < 60; i++) {
                rt_thread_mdelay(1000);
                if (g_sc.wifi_reconnect)
                    break;
            }
        }
    }
}

void sc_net_start(void)
{
    rt_thread_t tid;

    rt_mutex_init(&sc_mutex, "sc_lock", RT_IPC_FLAG_PRIO);

    memset(&g_sc, 0, sizeof(g_sc));
    snprintf(g_sc.wifi_ssid, sizeof(g_sc.wifi_ssid), "%s", SC_WIFI_SSID_DEFAULT);
    snprintf(g_sc.wifi_pass, sizeof(g_sc.wifi_pass), "%s", SC_WIFI_PASS_DEFAULT);
    g_sc.brightness = 80;
    g_sc.wifi_state = 0;
    wifi_cfg_load();

    tid = rt_thread_create("sc_net", net_thread_entry, RT_NULL, 16384, 22, 10);
    if (tid)
        rt_thread_startup(tid);
}

int sc_wifi_apply(const char *ssid, const char *pass)
{
    size_t sl, pl;

    if (!ssid || !ssid[0])
        return -1;
    sl = strlen(ssid);
    pl = pass ? strlen(pass) : 0;
    if (sl > 32 || pl > 64)
        return -1;

    sc_lock();
    snprintf(g_sc.wifi_ssid, sizeof(g_sc.wifi_ssid), "%s", ssid);
    snprintf(g_sc.wifi_pass, sizeof(g_sc.wifi_pass), "%s", pass ? pass : "");
    g_sc.wifi_reconnect = 1;
    g_sc.wifi_state = 0;
    sc_unlock();

    wifi_cfg_save();
    return 0;
}

static void sc_status(int argc, char **argv)
{
    int i;

    (void)argc;
    (void)argv;

    sc_lock();
    rt_kprintf("wifi=%d ip=%s rssi=%d ntp=%d epoch=%ld\n",
               g_sc.wifi_state, g_sc.ip, g_sc.rssi, g_sc.time_synced,
               (long)g_sc.utc_epoch);
    rt_kprintf("weather=%d city=%s now=%dC %s feels=%d hum=%d wind=%d cnt=%d\n",
               g_sc.weather_state, g_sc.city, g_sc.temp, g_sc.weather_text,
               g_sc.feels, g_sc.humidity, g_sc.wind, g_sc.day_cnt);
    for (i = 0; i < g_sc.day_cnt; i++)
        rt_kprintf("  day%d %s %d/%d code=%d\n", i, g_sc.day_name[i],
                   g_sc.day_max[i], g_sc.day_min[i], g_sc.day_code[i]);
    rt_kprintf("news=%d count=%d updated=%s\n",
               g_sc.news_state, g_sc.news_count, g_sc.news_updated);
    for (i = 0; i < g_sc.news_count; i++)
        rt_kprintf("  [%d] %s\n", i, g_sc.news[i]);
    rt_kprintf("stock=%d\n", g_sc.stock_state);
    for (i = 0; i < SC_IDX_MAX; i++)
        rt_kprintf("  %s %d.%02d %+d.%02d %+d.%02d%%\n", g_sc.idx[i].name,
                   g_sc.idx[i].price / 100, abs(g_sc.idx[i].price % 100),
                   g_sc.idx[i].chg / 100, abs(g_sc.idx[i].chg % 100),
                   g_sc.idx[i].pct / 100, abs(g_sc.idx[i].pct % 100));
    rt_kprintf("brightness=%d\n", g_sc.brightness);
    sc_unlock();
}
MSH_CMD_EXPORT(sc_status, show smart clock data);

static void sc_wifi_cmd(int argc, char **argv)
{
    if (argc < 2) {
        rt_kprintf("usage: sc_wifi_cmd <ssid> [password]\n");
        return;
    }
    if (sc_wifi_apply(argv[1], argc > 2 ? argv[2] : "") == 0)
        rt_kprintf("wifi config applied: %s\n", argv[1]);
    else
        rt_kprintf("invalid wifi config\n");
}
MSH_CMD_EXPORT(sc_wifi_cmd, set WiFi ssid/password and reconnect);

static void sc_news_test(int argc, char **argv)
{
    int r;

    (void)argc;
    (void)argv;
    r = fetch_news();
    rt_kprintf("news fetch=%d state=%d count=%d\n",
               r, g_sc.news_state, g_sc.news_count);
    if (http_body)
        rt_kprintf("body: %.200s\n", http_body);
}
MSH_CMD_EXPORT(sc_news_test, force fetch ITHome news and print result);

