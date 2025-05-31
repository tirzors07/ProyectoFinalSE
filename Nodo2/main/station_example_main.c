#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi_station.h"
void app_main(void)
{
    event_group_monitor = xEventGroupCreate();
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    /* Inicializa Wi-Fi y lanza la tarea */
    wifi_init_sta();
    xTaskCreate(monitor_state_task,"MONITOR_STATE",4096, NULL, 5, NULL);
    xTaskCreate(send_hum_i2c_task,"I2C_SEND_HUM_TASK",4096, NULL, 5, NULL);
}