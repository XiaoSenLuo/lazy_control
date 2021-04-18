
#include "esp_http_client.h"
#include <sys/socket.h>
#include <netdb.h>
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "http_download.h"


static const char* TAG = "## http download file ##";

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

int http_download_file_by_url(const char *url, const char *des_path){

    const esp_http_client_config_t _http_config = {
        .url = url,
        .event_handler = _http_event_handler,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&_http_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        ESP_LOGE(TAG, "Failed to open HTTP connection: %d", err);
        return err;
    }
    esp_http_client_fetch_headers(client);
    
    FILE *_des_file = fopen(des_path, "w");
    
    if(_des_file == NULL) goto error_secsion;

    int rec_data_len = 0;
    char *buffer = (char*)malloc(4096);
    if(buffer == NULL){
        ESP_LOGE(TAG, "Couldn't allocate memory to upgrade data buffer");
        goto error_secsion;
    }else memset(buffer, 0, 4096);

    while(1){
        rec_data_len = esp_http_client_read(client, buffer, 4096);
        if(rec_data_len == 0){
            ESP_LOGI(TAG, "Connection closed,all data received");
            break;
        }
        if(rec_data_len > 0){
            fwrite(buffer, 1, rec_data_len, _des_file);
        }
    }
    fclose(_des_file);
    free(buffer);
    buffer = NULL;
    http_cleanup(client); 
    return 0;
error_secsion:
   http_cleanup(client);
   return ESP_FAIL;
}



int http_download_file(const char *host, const int port, const char *file_path, const char *des_path){
    char _url[128] = {'\0'};

    sprintf(_url, "http://%s:%d/%s", host, port, file_path);
    
    return http_download_file_by_url((const char*)_url, des_path);
}



