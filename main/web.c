/**
 * @file    web.c
 * @brief   Minimal HTTP server on the ESP32 soft AP (192.168.4.1).
 *
 *          GET  /        -> page with a text input + submit button
 *          POST /submit  -> takes the input and shows it on the OLED
 */
#include "web.h"
#include "oled_i2c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web";
static httpd_handle_t s_server;

/* SSID shown on the OLED header (keep in sync with main.c WIFI_SSID) */
#define AP_SSID "esp32-test"
#define AP_URL  "http://192.168.4.1"

/* ---------- OLED display ---------- */

void oled_show_text(const char *text)
{
    OLED_Clear();
    /* line 1 (y=0): SSID, centered (10 chars x 6px = 60px -> x=34) */
    OLED_ShowString(34, 0, AP_SSID);
    /* line 2 (y=8): gateway URL, centered (18 chars x 6px = 108px -> x=10) */
    OLED_ShowString(10, 8, AP_URL);
    /* line 3+ (y=24): user input in 16x16 CJK font (8 chars/line, wraps) */
    if (text != NULL && text[0] != '\0')
    {
        OLED_ShowString16(0, 24, text);
    }
    OLED_Refresh();
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

static esp_err_t submit_post(httpd_req_t *req)
{
    /* 1. read POST body */
    char body[384];
    size_t total = req->content_len;
    if (total == 0 || total >= sizeof(body))
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
    char text[128];
    if (form_get_text(body, text, sizeof(text)) != 0 || text[0] == '\0')
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing text");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "received text: %s", text);

    /* 3. show it on the OLED */
    oled_show_text(text);

    /* 4. respond with a tiny confirmation page */
    char escaped[512];
    html_escape(text, escaped, sizeof(escaped));
    char page[768];
    snprintf(page, sizeof(page), OK_HTML, escaped);
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
