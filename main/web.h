/**
 * @file    web.h
 * @brief   Minimal HTTP server for the ESP32 soft AP (192.168.4.1).
 *
 *          Serves a tiny page with a text input; on submit the text is
 *          shown on the OLED.
 */
#ifndef WEB_H
#define WEB_H

/**
 * @brief Start the HTTP server on the AP interface (port 80).
 *        GET  /        -> page with a text input + submit button
 *        POST /submit  -> takes the input and shows it on the OLED
 * @retval 0 success, -1 failure.
 */
int web_server_start(void);

/**
 * @brief Redraw the OLED: two header lines (SSID + gateway URL) plus
 *        the given text on the third line ("" = clear third line).
 */
void oled_show_text(const char *text);

/**
 * @brief 温度监控画面：顶部一行状态（低温/正常/超温），下方显示当前温度值。
 * @param status  状态文字（UTF-8 中文，如 "低温"、"正常"、"超温"）
 * @param temp_c  当前温度（摄氏度）；is_valid 为 false 时显示 "----"
 * @param is_valid  ADC 读数是否有效
 */
void oled_show_temp_status(const char *status, float temp_c, bool is_valid);

#endif /* WEB_H */
