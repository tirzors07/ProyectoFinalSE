#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include "string.h"
#include "stdlib.h"
#define HISTORY_COUNT  11
#define VALUE_MAX_LEN  8   // p.ej. "00.00\0"
#define MAX_DATA_LEN   (HISTORY_COUNT*VALUE_MAX_LEN + (HISTORY_COUNT-1))
static const char *TAG = "web_server";
static char latest_data_node1[32] = "Esperando...";
static char latest_data_node2[32] = "Esperando...";

static char *history_tmp[HISTORY_COUNT];
static char *history_hum[HISTORY_COUNT];

uint8_t fgMonitoring = 0;

void start_webserver(void);
void init_spiffs(void);
void wifi_init_softap(void);
esp_err_t html_get_handler(httpd_req_t *req);
esp_err_t css_get_handler(httpd_req_t *req);
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    // Inicializar SPIFFS
    init_spiffs();
    // Inicializar WiFi en modo Access Point
    wifi_init_softap();
    // Iniciar el servidor web
    start_webserver();
}
esp_err_t css_get_handler(httpd_req_t *req)
{
    FILE *file = fopen("/spiffs/index.css", "r");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Error al abrir el archivo para lectura");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/css"); // Establecer el tipo de contenido
    char response[256];
    while (fgets(response, sizeof(response), file))
    {
        httpd_resp_sendstr_chunk(req, response);
    }
    httpd_resp_sendstr_chunk(req, NULL); // Finalizar la respuesta
    fclose(file);
    return ESP_OK;
}
esp_err_t monitor_handler(httpd_req_t *req)
{
    // Solo aceptamos GET
    if (req->method != HTTP_GET) {
        return httpd_resp_send_err(req,
            HTTPD_405_METHOD_NOT_ALLOWED,
            "Método no permitido");
    }
    //fuerza Connection: close para que IDF ponga Content-Length y cierre
    httpd_resp_set_hdr(req, "Connection", "close");
    // tipo
    httpd_resp_set_type(req, "text/plain");
    //envia de golpe el cuerpo ("ON" o "OFF") y cierra
    const char *resp = fgMonitoring ? "ON" : "OFF";
    return httpd_resp_sendstr(req, resp);
}

esp_err_t post_root_handler(httpd_req_t *req) {
    fgMonitoring = !fgMonitoring;
    ESP_LOGI(TAG, "Monitoreo %s", fgMonitoring ? "ON" : "OFF");
    //redirect para que el navegador haga GET / y no reenvie el POST
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t data_post_handler(httpd_req_t *req)
{
    int8_t node = (int8_t)req->user_ctx;
    int len = req->content_len;
    //lee todo el payload
    char *buffer = malloc(len + 1);
    if (!buffer) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int r = httpd_req_recv(req, buffer, len);
    if (r <= 0) {
        free(buffer);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buffer[r] = '\0';
    ESP_LOGI(TAG, "Received /update%d → %s", node, buffer);
    //tokeniza sobre ','
    char *tokens[HISTORY_COUNT];
    int count = 0;
    char *tok = strtok(buffer, ",");
    while (tok && count < HISTORY_COUNT) {
        tokens[count++] = tok;
        tok = strtok(NULL, ",");
    }
    if (count != HISTORY_COUNT) {
        ESP_LOGW(TAG, "Se Esperaban %d valores, se recibio %d", HISTORY_COUNT, count);
        free(buffer);
        httpd_resp_sendstr(req, "Ok");
        return ESP_OK;
    }
    for (int8_t i = 0; i < HISTORY_COUNT; i++) {
        // libera anterior
        if (node == 1 && history_tmp[i]) {
            free(history_tmp[i]);
        }
        if (node == 2 && history_hum[i]) {
            free(history_hum[i]);
        }
        // duplica el string token
        char *dup = strdup(tokens[i]);
        if (node == 1) {
            history_tmp[i] = dup;
        } else {
            history_hum[i] = dup;
        }
    }
    // 4) guarda el ultimo valor (actual) en latest_data_node1 o 2 para compatibilidad
    if (node == 1) {
        strncpy(latest_data_node1, tokens[HISTORY_COUNT-1], 32);
    } else {
        strncpy(latest_data_node2, tokens[HISTORY_COUNT-1], 32);
    }
    free(buffer);
    httpd_resp_sendstr(req, "Ok");
    return ESP_OK;
}
esp_err_t html_get_handler(httpd_req_t *req)
{
    FILE *file = fopen("/spiffs/index.html", "r");
    if (!file) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    fseek(file, 0, SEEK_END);
    size_t fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *html_buffer = malloc(fsize + 1);
    fread(html_buffer, 1, fsize, file);
    fclose(file);
    html_buffer[fsize] = '\0';
    const char *state = fgMonitoring ? "ON" : "OFF";
    int placeholders = 3 + (HISTORY_COUNT - 1) * 2;
    size_t resp_size = fsize + placeholders * VALUE_MAX_LEN + 64;
    char *response_buffer = malloc(resp_size);
    if (!response_buffer) {
        free(html_buffer);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    const char *tmp_vals[HISTORY_COUNT-1];
    const char *hum_vals[HISTORY_COUNT-1];
    for (int i = 0; i < HISTORY_COUNT-1; i++) {
        tmp_vals[i] = history_tmp[i] ? history_tmp[i] : "0.00";
        hum_vals[i] = history_hum[i] ? history_hum[i] : "0.00";
    }
    int needed = snprintf(response_buffer, resp_size,html_buffer,state,latest_data_node1,latest_data_node2,
                            tmp_vals[0], hum_vals[0],
                            tmp_vals[1], hum_vals[1],
                            tmp_vals[2], hum_vals[2],
                            tmp_vals[3], hum_vals[3],
                            tmp_vals[4], hum_vals[4],
                            tmp_vals[5], hum_vals[5],
                            tmp_vals[6], hum_vals[6],
                            tmp_vals[7], hum_vals[7],
                            tmp_vals[8], hum_vals[8],
                            tmp_vals[9], hum_vals[9]
    );
    free(html_buffer);
    if (needed < 0 || (size_t)needed >= resp_size) {
        ESP_LOGE(TAG, "snprintf overflow: %d vs %d", needed, (int)resp_size);
        free(response_buffer);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response_buffer, needed);
    free(response_buffer);
    return ESP_OK;
}

void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t html_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = html_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &html_uri);
        httpd_uri_t css_uri = {
            .uri = "/index.css",
            .method = HTTP_GET,
            .handler = css_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &css_uri);
        // nodo 1
        httpd_uri_t post1 = {// para el nodo 1
            .uri = "/update1",
            .method = HTTP_POST,
            .handler = data_post_handler,
            .user_ctx = (void *)1};
        // nodo 2
        httpd_register_uri_handler(server, &post1);// 
        httpd_uri_t post2 = {//para el nodo 2
            .uri = "/update2",
            .method = HTTP_POST,
            .handler = data_post_handler,
            .user_ctx = (void *)2};
        httpd_register_uri_handler(server, &post2);
        httpd_uri_t monitor_uri = {//uri donde hace get el sta para saber si enviar o no sensado de valores
            .uri = "/monitor",
            .method = HTTP_GET,
            .handler = monitor_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server, &monitor_uri);
        httpd_uri_t post_root = {
            .uri = "/",
            .method = HTTP_POST,
            .handler = post_root_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &post_root);
        ESP_LOGI(TAG, "Web server iniciado");
    }
}
void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al inicializar SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "No se pudo obtener la información de la partición SPIFFS (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS: Tamano total: %d, Usado: %d", total, used);
    }
}
void wifi_init_softap(void)
{
    // Inicializar WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    const char mi_ssid[] = "Mr_Robot";

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(mi_ssid),
            .channel = 1,                      // Canal WiFi
            .password = "admini123",           // Contraseña del AP
            .max_connection = 4,               // Numero maximo de conexiones permitidas
            .authmode = WIFI_AUTH_WPA_WPA2_PSK // Modo de autenticacion
        },
    };

    memcpy(wifi_config.ap.ssid, mi_ssid, strlen(mi_ssid)); // Nombre de la red WiFi

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP)); // Configurar como AP
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "ESP32 AP iniciado. SSID:%s password:%s channel:%d",
             wifi_config.ap.ssid, wifi_config.ap.password, wifi_config.ap.channel);
}