#ifndef i2c_bme280_h_
#define i2c_bme280_h_
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

#define BME_280_SLAVE 0xF6
#define BME280_CTRL_MEAS 0xF4
#define BME280_CTRL_HUM 0xF2
#define BME280_RESET 0xE0
#define BME280_CONFIG 0xF5
/**/
extern int32_t dig_T1, dig_T2, dig_T3;

extern uint8_t  dig_H1;
extern int16_t  dig_H2, dig_H3, dig_H4, dig_H5;
extern int8_t   dig_H6;

extern int32_t  t_fine;

/*funciones para lectura de datos*/
int32_t readBME280Temperature(i2c_master_dev_handle_t i2c_dev);
void bme280_calibration(i2c_master_dev_handle_t i2c_dev);
/**/
uint32_t bme280_compensate_H_int32(int32_t adc_H);
uint32_t readBME280Humidity(i2c_master_dev_handle_t i2c_dev);
void bme280_calibration(i2c_master_dev_handle_t i2c_dev);
void init_bme280(i2c_master_dev_handle_t i2c_dev);
#endif 