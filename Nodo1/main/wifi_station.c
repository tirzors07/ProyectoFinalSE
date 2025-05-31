#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi_station.h"
#include "i2c_bme280.h"

/* Internos */
const char *AP_MONITOR_URL = "http://192.168.4.1/monitor"; // el sta hace get al AP para saber si seguir sensando o no
static const char *TAG = "WIFI_STATION";
static EventGroupHandle_t s_wifi_event_group;
EventGroupHandle_t event_group_monitor;
static int s_retry_num = 0;
/*i2c */
esp_err_t i2c_master_init(i2c_master_dev_handle_t *dev_handle){
    i2c_master_bus_config_t i2c_mst_config_t = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL,
        .sda_io_num = I2C_MASTER_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_master_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config_t, &i2c_master_handle));
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_SLAVE_ADDRESS,
        .scl_speed_hz = 400000,
    };
    //
    return i2c_master_bus_add_device(i2c_master_handle, &dev_cfg, dev_handle);
}
static void event_handler(void *arg, esp_event_base_t base,int32_t id, void *data){
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START){
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED){
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY){
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect -> %d", s_retry_num);
        }
        else{
            xEventGroupSetBits(s_wifi_event_group, MY_WIFI_FAIL_BIT);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&e->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, MY_WIFI_CONNECTED_BIT);
    }
}
void wifi_init_sta(void){
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    //
    //llamar a modo ahorro de energia  sleep modem
    ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_MIN_MODEM) );
    //    
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        MY_WIFI_CONNECTED_BIT | MY_WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY);

    if (bits & MY_WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to SSID:%s", EXAMPLE_ESP_WIFI_SSID);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to connect");
    }
}
void monitor_state_task(void *pvParams)
{
    // wait for wifi conected
    xEventGroupWaitBits(s_wifi_event_group, MY_WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi listo, arrancando polling de /monitor");
    for (;;)
    {
        esp_http_client_config_t cfg = {
            .url               = AP_MONITOR_URL,
            .method            = HTTP_METHOD_GET,
            .timeout_ms        = 5000,
            .keep_alive_enable = false,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            ESP_LOGE(TAG, "no se pudo inicializar HTTP client");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        //abrir conexion y leer headers
        if (esp_http_client_open(client, 0) != ESP_OK) {
            ESP_LOGE(TAG, "fallo esp_http_client_open");
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int content_length = esp_http_client_fetch_headers(client);
        int status_code    = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Codigo HTTP: %d, contenido: %d bytes", status_code, content_length);

        if (status_code == 200 && content_length > 0) {
            //leer todo el buffer
            char *buffer = malloc(content_length + 1);
            if (buffer) {
                int to_read = content_length;
                int read_len = esp_http_client_read(client, buffer, to_read);
                if (read_len > 0) {
                    buffer[read_len] = '\0';
                    ESP_LOGI(TAG, "Respuesta completa: '%s'", buffer);
                    //procesar lo recibido
                    if (strncmp(buffer, "ON", 2) == 0) {
                        xEventGroupSetBits(event_group_monitor, MY_MONITOR_BIT_SEND);
                    } else if (strncmp(buffer, "OFF", 3) == 0) {
                        xEventGroupClearBits(event_group_monitor, MY_MONITOR_BIT_SEND);
                    } else {
                        ESP_LOGE(TAG, "Valor inesperado: '%s'", buffer);
                    }
                } else {
                    ESP_LOGW(TAG, "No se leyo cuerpo, esp_http_client_read devolvio %d", read_len);
                }
                free(buffer);
            } else {
                ESP_LOGE(TAG, "Sin memoria para buffer de %d bytes", content_length);
            }
        } else {
            ESP_LOGW(TAG, "HTTP %d o contenido inesperado", status_code);
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void send_temp_i2c_task(void *pvParams)
{
    int32_t rawT, t_int;
    /*usamos memoria dinamica para evitar situaciones como overflow en la tarea */
    float *temp_array = (float *)malloc(sizeof(float) * HISTORY_LEN); //[HISTORY_LEN];
    char *payload;
    //inicializar i2c y eso como master
    i2c_master_dev_handle_t i2c_dev;
    ESP_ERROR_CHECK(i2c_master_init(&i2c_dev));
    init_bme280(i2c_dev);

    while (1)
    {
        //Esperar bit de  monitor_state_task
        xEventGroupWaitBits(event_group_monitor, MY_MONITOR_BIT_SEND, pdTRUE, pdFALSE, portMAX_DELAY);
        // HISTORY_LEN mediciones con retardo entre cada una
        for (int8_t i = 0; i < HISTORY_LEN; i++) {
            rawT = readBME280Temperature(i2c_dev);
            t_int = receive_temp(rawT);
            temp_array[i] = t_int / 100.0f;
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));  // Espera entre cada muestra del sensor
        }
        //construir payload CSV con HISTORY_LEN valores
        size_t payload_size = HISTORY_LEN * VALUE_STR_LEN + (HISTORY_LEN - 1) + 1;
        payload = (char *)malloc(sizeof(char)*payload_size);
        if (!payload) {
            ESP_LOGE(TAG, "No hay memoria para payload");
            break;
        }
        char *p = payload;
        for (int8_t i = 0; i < HISTORY_LEN; i++) {
            int w = snprintf(p, VALUE_STR_LEN, "%.2f", temp_array[i]);
            p += w;
            if (i < HISTORY_LEN - 1)
                *p++ = TOKEN_STRING;  // Agrega separador o token ','
        }
        // Enviar POST con el payload
        esp_http_client_config_t cfg = {
            .url        = AP_UPDATE_URL,
            .method     = HTTP_METHOD_POST,
            .timeout_ms = 3000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client) {
            esp_http_client_set_header(client, "Content-Type", "text/plain");
            esp_http_client_set_post_field(client, payload, strlen(payload));
            if (esp_http_client_perform(client) == ESP_OK) {
                int status = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "POST TEMP OK, STATUS=%d, payload=%s", status, payload);
            } else {
                ESP_LOGE(TAG, "Error en POST temperature history");
            }
            esp_http_client_cleanup(client);
        } else {
            ESP_LOGE(TAG, "Error init HTTP client");
        }
        free(payload);
        //esperar 5 segundos antes de comenzar otro sample siempre y cuando el bit de envio este activo.
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    free(temp_array);
    vTaskDelete(NULL);
}