/**
 * @file    wifi.h
 * @brief   ESP32-S3 WiFi AP (access point / hotspot) helper.
 *
 *          Runs the chip as a standalone soft AP (no router needed):
 *          it broadcasts an SSID, phones/laptops connect to it, and the
 *          ESP32 answers on its default address 192.168.4.1.
 */
#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>

/**
 * @brief Start WiFi in AP mode and block until the AP is broadcasting.
 * @param ssid Broadcast SSID (1..32 chars).
 * @param pass WPA2 pre-shared key (8..63 chars); NULL or "" = open network.
 * @retval 0 AP is up (address available via wifi_ap_ip()).
 * @retval -1 driver init / configuration / start failure.
 * @retval -2 AP did not start within 10 s.
 */
int wifi_ap_start(const char *ssid, const char *pass);

/** Dotted-quad IP of the AP interface (default 192.168.4.1). */
const char *wifi_ap_ip(void);

/** Number of clients currently associated with the AP. */
int wifi_ap_client_count(void);

#endif /* WIFI_H */
