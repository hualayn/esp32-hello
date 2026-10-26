/**
 * @file    web.c
 * @brief   Minimal HTTP server on the ESP32 soft AP (192.168.4.1).
 *
 *          GET  /        -> page with a text input + submit button
 *          POST /submit  -> takes the input and shows it on the OLED
 */
#include "web.h"
#include "u8g2_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web";
static httpd_handle_t s_server;

/* SSID shown on the OLED header (keep in sync with main.c WIFI_SSID) */
#define AP_SSID "esp32-test"
#define AP_URL  "http://192.168.4.1"

/* wqy12 中文字形 12px 见方、步进也是 12px，字字紧贴；每个字符后加 1px 空隙更清晰 */
#define CJK_CHAR_GAP 1

/* ---------- OLED display (u8g2) ---------- */

/* 返回一个 UTF-8 字符占用的字节数（1..4） */
static int utf8_char_len(uint8_t b)
{
    if (b < 0x80)            return 1;
    if ((b & 0xE0) == 0xC0)  return 2;
    if ((b & 0xF0) == 0xE0)  return 3;
    if ((b & 0xF8) == 0xF0)  return 4;
    return 1;
}

/*
 * 屏幕布局（128x64）：
 *   第 1 行  14: SSID            (6x12 字体)
 *   第 2 行  28: 网关地址        (6x12 字体)
 *   第 3/4 行 48/60: 用户输入    (wqy12 中文字库，UTF-8，满 128px 自动换行)
 * 没有输入时显示"你好，世界！"
 */
void oled_show_text(const char *text)
{
    u8g2_t *d = oled_u8g2_get();
    u8g2_ClearBuffer(d);

    /* 前两行：热点信息（6x12 英文字体；新版 u8g2 的完整 ASCII 版叫 6x12_tr，
       6x12_tn 只有 18 个字形——空格+数字+少量标点，没有字母，不能用） */
    u8g2_SetFont(d, u8g2_font_6x12_tr);
    u8g2_DrawStr(d, 34, 14, AP_SSID);
    u8g2_DrawStr(d, 10, 28, AP_URL);

    /* 后两行：中文（wqy12 字库，直接喂 UTF-8 字符串） */
    u8g2_SetFont(d, u8g2_font_wqy12_t_gb2312);
    const char *s = (text != NULL && text[0] != '\0') ? text : "你好，世界！";
    int x = 0, y = 48;
    while (*s)
    {
        int n = utf8_char_len((uint8_t)*s);
        char one[5] = {0};
        memcpy(one, s, n);
        int w = u8g2_GetUTF8Width(d, one);   /* 该字符的宽度（CJK=12, 英文≈6） */
        if (w <= 0) {                        /* 字库里没有的字，跳过 */
            s += n;
            continue;
        }
        if (x + w + CJK_CHAR_GAP > 128) {  /* 换行（含间距），最多两行 */
            x = 0;
            y += 12;
            if (y > 60) {
                break;
            }
        }
        u8g2_DrawUTF8(d, x, y, one);
        x += w + CJK_CHAR_GAP;
        s += n;
    }
    u8g2_SendBuffer(d);
}

/*
 * 温度监控画面布局（128x64）：
 *   第 1 行  14: 状态（低温/正常/超温），wqy12 中文，居中
 *   第 2 行  36: 温度数值 "25.3 C"，6x18 大字，居中
 *   第 3 行  60: 小提示文字（如 "AO=GPIO4"）
 */
void oled_show_temp_status(const char *status, float temp_c, bool is_valid)
{
    u8g2_t *d = oled_u8g2_get();
    u8g2_ClearBuffer(d);

    /* 第一行：状态（wqy12 中文，12px 字高） */
    u8g2_SetFont(d, u8g2_font_wqy12_t_gb2312);
    int sw = u8g2_GetUTF8Width(d, status);
    u8g2_DrawUTF8(d, (128 - sw) / 2, 14, status);

    /* 第二行：温度数值（6x18 数字字体，居中） */
    char buf[24];
    if (is_valid) {
        snprintf(buf, sizeof(buf), "%.1f C", (double)temp_c);
    } else {
        snprintf(buf, sizeof(buf), "---- C");
    }
    u8g2_SetFont(d, u8g2_font_logisoso16_tf);
    int tw = u8g2_GetStrWidth(d, buf);
    u8g2_DrawStr(d, (128 - tw) / 2, 38, buf);

    /* 第三行：小提示（6x12 字体，居中） */
    u8g2_SetFont(d, u8g2_font_6x12_tr);
    const char *hint = "AO=GPIO4";
    int hw = u8g2_GetStrWidth(d, hint);
    u8g2_DrawStr(d, (128 - hw) / 2, 58, hint);

    u8g2_SendBuffer(d);
}

/* ---------- HTML pages (UTF-8) ---------- */

static const char INDEX_HTML[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>ESP32 OLED</title>"
    "</head><body>"
    "<h3>ESP32 OLED 显示输入</h3>"
    "<form method=\"POST\" action=\"/submit\">"
    "<input type=\"text\" name=\"text\" maxlength=\"32\" "
    "placeholder=\"输入文字（最多32字符）\">"
    "<button type=\"submit\">提交</button>"
    "</form>"
    "</body></html>";

static const char OK_HTML[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>已更新</title>"
    "</head><body>"
    "<h3>已显示到屏幕</h3>"
    "<p>文本: %s</p>"
    "<p><a href=\"/\">返回</a></p>"
    "</body></html>";

/* ---------- helpers ---------- */

/* Extract the "text" field from a form-encoded POST body.
 * Handles '+' (space) and %XX URL-encoding. */
static int form_get_text(const char *body, char *out, size_t out_len)
{
    const char *p = strstr(body, "text=");
    if (p == NULL)
    {
        return -1;
    }
    p += 5; /* skip "text=" */
    size_t i = 0;
    while (*p != '\0' && *p != '&' && i + 1 < out_len)
    {
        if (*p == '+')
        {
            out[i++] = ' ';
            p++;
        }
        else if (p[0] == '%' && p[1] != '\0' && p[2] != '\0')
        {
            char hex[3] = { p[1], p[2], '\0' };
            out[i++] = (char)strtol(hex, NULL, 16);
            p += 3;
        }
        else
        {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return 0;
}

/* Escape HTML special characters so user input cannot break the page. */
static void html_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    while (*in != '\0' && o + 7 < out_len) /* worst case "&lt;" = 4 chars */
    {
        if (*in == '<')
        {
            memcpy(out + o, "&lt;", 4);
            o += 4;
        }
        else if (*in == '>')
        {
            memcpy(out + o, "&gt;", 4);
            o += 4;
        }
        else if (*in == '&')
        {
            memcpy(out + o, "&amp;", 5);
            o += 5;
        }
        else
        {
            out[o++] = *in;
        }
        in++;
    }
    out[o] = '\0';
}

/* ---------- URI handlers ---------- */

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/*
 * 注意：这些缓冲用 static 放在全局数据区，而不是 httpd 任务的栈上。
 * httpd 任务默认栈只有 4KB，四个数组加起来 ~1.8KB，加上 httpd 自身帧和
 * u8g2 渲染的开销，很容易把栈撑爆 → 任务崩溃 → 设备重启 → 手机掉线。
 * httpd 的 handler 都跑在同一个任务里，串行执行，static 是安全的。
 */
static char s_body[384];
static char s_text[128];
static char s_escaped[512];
static char s_page[768];

static esp_err_t submit_post(httpd_req_t *req)
{
    /* 1. read POST body */
    char *body = s_body;
    size_t total = req->content_len;
    if (total == 0 || total >= sizeof(s_body))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    size_t received = 0;
    while (received < total)
    {
        int len = httpd_req_recv(req, body + received, total - received);
        if (len <= 0)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            return ESP_FAIL;
        }
        received += (size_t)len;
    }
    body[received] = '\0';

    /* 2. parse the "text" field */
    char *text = s_text;
    if (form_get_text(body, text, sizeof(s_text)) != 0 || text[0] == '\0')
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing text");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "received text: %s", text);

    /* 3. show it on the OLED */
    oled_show_text(text);

    /* 4. respond with a tiny confirmation page */
    char *escaped = s_escaped;
    html_escape(text, escaped, sizeof(s_escaped));
    char *page = s_page;
    snprintf(page, sizeof(s_page), OK_HTML, escaped);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ---------- server start ---------- */

int web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return -1;
    }

    httpd_uri_t index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get,
    };
    httpd_register_uri_handler(s_server, &index);

    httpd_uri_t submit = {
        .uri = "/submit",
        .method = HTTP_POST,
        .handler = submit_post,
    };
    httpd_register_uri_handler(s_server, &submit);

    ESP_LOGI(TAG, "web server started on %s", AP_URL);
    return 0;
}
