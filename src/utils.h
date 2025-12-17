#pragma once


// Генеруємо простий унікальний ідентифікатор на основі esp_random() + часу + MAC
static String generateIdentifier() {
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uint64_t now = (uint64_t)esp_timer_get_time(); // мікросекунди
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  char buf[64];
  snprintf(buf, sizeof(buf), "%08X%08X%08llX%02X%02X", r1, r2, (unsigned long long)now, mac[4], mac[5]);
  // перетворимо в більш зручний формат (групи по 4)
  String s = String(buf);
  // uppercase
  s.toUpperCase();
  return s;
}