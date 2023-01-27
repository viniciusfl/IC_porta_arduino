#include <common.h>
#include <Arduino.h>

int logmessage(const char* format, va_list ap) {
    int count;
    char buf[512];
    buf[0] = 0;
    count = vsnprintf(buf, 512, format, ap);
    Serial.print(buf);
    return count;
}

void initlog() {
    // Do not change this! Instead, define the desired level in log_conf.h
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Log messages will be processed by the function defined above
    esp_log_set_vprintf(logmessage);
}
