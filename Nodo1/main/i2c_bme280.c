#include "i2c_bme280.h"
#include "esp_log.h"
 
const char *TAG = "BME280";
int32_t dig_T1, dig_T2, dig_T3;
int32_t t_fine;
static esp_err_t read_Byte(i2c_master_dev_handle_t i2c_dev, uint8_t address, uint8_t subAddress, uint8_t sz_rxBuff, uint8_t *dest) {
    esp_err_t ret;
    uint8_t write_buf[1] = {subAddress};

    if (dest == NULL || sz_rxBuff == 0) {/*buff null */
        return ESP_ERR_INVALID_ARG; // manejo de err
    }
    // envio de dir del reg a leer
    ret = i2c_master_transmit(i2c_dev, write_buf, sizeof(write_buf), -1);
    if (ret != ESP_OK) {
        return ret; // Retorna en caso de error
    }
    //leer dato del dispositivo
    ret = i2c_master_receive(i2c_dev, dest, sz_rxBuff, -1);
    return ret;
}
static esp_err_t write_Byte(i2c_master_dev_handle_t i2c_dev, uint8_t address, uint8_t subAddress, uint8_t data) {
    uint8_t write_buf[2] = {subAddress, data};  
    esp_err_t ret;

    ret = i2c_master_transmit(i2c_dev, write_buf, sizeof(write_buf), -1);
    if (ret != ESP_OK) {
        //char msg[] = "Conexion Terminada, El Periferico No Responde\n";
        //UART_puts(UART_NUM_0, msg);
        ESP_LOGI(TAG, "Conexion Terminada, El Periferico No Responde\n");
        while (1) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
    return ret;
}

void bme280_calibration(i2c_master_dev_handle_t i2c_dev){
    uint8_t data[6];
    uint8_t reg_addr = 0x88;
    esp_err_t ret;
    ret = i2c_master_transmit(i2c_dev, &reg_addr, 1, -1);
    if (ret != ESP_OK) {
        return;
    }
    ret = i2c_master_receive(i2c_dev,data,sizeof(data),-1);
    if (ret != ESP_OK) {
        return;
    }
    dig_T1 = (uint16_t)(data[0] | (data[1] << 8));
    dig_T2 = ( int16_t)(data[2] | (data[3] << 8));
    dig_T3 = ( int16_t)(data[4] | (data[5] << 8));
}

int32_t receive_temp(int32_t adc_T) {
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;  
    T = (t_fine * 5 + 128) >> 8;
    return T;
}
int32_t readBME280Temperature(i2c_master_dev_handle_t i2c_dev) {//modo normal
    uint8_t rawData[3];  // 20-bit temperature data
    esp_err_t ret;
    vTaskDelay(pdMS_TO_TICKS(165));
    // Leer los 3 bytes de temperatura a partir de 0xFA
    ret = read_Byte(i2c_dev, BME_280_SLAVE, 0xFA, 3, rawData);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"Error al leer la temperatura\n");
        //UART_puts(UART_NUM_0,"Error al leer la temperatura\n");
        return 0; 
    }
    //ESP_ERROR_CHECK(write_Byte(i2c_dev,BME_280_SLAVE, BME280_CTRL_MEAS, ctrl_meas));//para forzado
    // Construir adc_T desde los 3 bytes leídos
    int32_t adc_T = (int32_t)(((uint32_t)rawData[0] << 12) | ((uint32_t)rawData[1] << 4) | (rawData[2] >> 4));
    return adc_T;
}
void init_bme280(i2c_master_dev_handle_t i2c_dev){
    ESP_ERROR_CHECK(write_Byte(i2c_dev, BME_280_SLAVE, BME280_RESET,0xB6));
    vTaskDelay(100 / portTICK_PERIOD_MS); 
    //Configurar el sensor 
    ESP_ERROR_CHECK(write_Byte(i2c_dev, BME_280_SLAVE, BME280_CTRL_HUM, 0x01));
    ESP_ERROR_CHECK(write_Byte(i2c_dev, BME_280_SLAVE, BME280_CTRL_MEAS, (1 << 5 ) | (0  << 2) | (0x03) ));
    ESP_ERROR_CHECK(write_Byte(i2c_dev, BME_280_SLAVE, BME280_CONFIG, (2<<5) | (0<<2)));
    bme280_calibration(i2c_dev);
}