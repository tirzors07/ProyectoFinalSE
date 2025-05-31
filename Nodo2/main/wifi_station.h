#ifndef WIFI_STATION_H
#define WIFI_STATION_H
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/i2c_slave.h"
#include "driver/i2c_master.h"
/* Configuración Wi-Fi */
#define EXAMPLE_ESP_WIFI_SSID       "Mr_Robot"
#define EXAMPLE_ESP_WIFI_PASS       "admini123"
#define EXAMPLE_ESP_MAXIMUM_RETRY   5
#define AP_UPDATE_URL               "http://192.168.4.1/update2"
#define MY_MON_POLL_MS   100000
/* Bits de evento */
#define MY_WIFI_CONNECTED_BIT  (1 << 0)
#define MY_WIFI_FAIL_BIT       (1 << 1)
#define MY_MONITOR_BIT_SEND (1 << 0)//bit para saber si enviar datos al ap o no.
#define I2C_SLAVE_SCL_IO GPIO_NUM_25
#define I2C_SLAVE_SDA_IO GPIO_NUM_33
#define I2C_MASTER_SCL 22 
#define I2C_MASTER_SDA 21
#define I2C_SLAVE_ADDR 0x29 /*addres del sensor*/
#define I2C_SLAVE_ADDRESS 0x76//del sensor
#define HISTORY_LEN 11
#define VALUE_STR_LEN 8 // 00.00\0
#define TOKEN_STRING ','
#define SAMPLE_DELAY_MS 250
extern EventGroupHandle_t event_group_monitor; 
esp_err_t i2c_master_init(i2c_master_dev_handle_t *dev_handle);
/* Prototipos públicos */
void wifi_init_sta(void);
void send_hum_i2c_task(void *pvParams);
void monitor_state_task(void *pvParams);
#endif // WIFI_STATION_H
