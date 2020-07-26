

#include "main.h"


static const char *TAG="## WIFI ##";

#define WIFI_CONNECT_RETRY                           5
static int8_t wifi_connect_retry = WIFI_CONNECT_RETRY;

EventGroupHandle_t wifi_event_group = NULL;
const EventBits_t WIFI_DISCONNECT_BIT = BIT0;
// static const EventBits_t WIFI_GOT_IP_BIT = BIT1;
TaskHandle_t wifi_reconnect_task_handle = NULL;



static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    // httpd_handle_t *server = (httpd_handle_t *) ctx;
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        // ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
    // ESP_LOGI(TAG, "SYSTEM_EVENT_STA_CONNECTED");
        delete_wifi_reconnect_task();
        xEventGroupClearBits(wifi_event_group, WIFI_DISCONNECT_BIT);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        ESP_LOGI(TAG, "Got IP: '%s'",
                ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));

        create_mqtt_connect_to_broker_task(NULL, 6);
        // xEventGroupSetBits(wifi_event_group, WIFI_GOT_IP_BIT);
        // xEventGroupSetBits(http_event_group, HTTP_SERVER_START_BIT);
        notice_http_server_task(true);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        ESP_LOGE(TAG, "disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
        }
        if((--wifi_connect_retry) > 0){
            ESP_ERROR_CHECK(esp_wifi_connect());
        }else{                                //达到最大重连次数, 重启wifi
            delete_mqtt_connect_to_broker_task();
            ESP_ERROR_CHECK(esp_wifi_stop());
            create_wifi_reconnect_task(NULL, 5);
            if((info->disconnected.reason != WIFI_REASON_NO_AP_FOUND) && (info->disconnected.reason != WIFI_REASON_AUTH_FAIL)){
                ESP_LOGI(TAG, "restart Wifi...............");
                wifi_connect_retry = WIFI_CONNECT_RETRY;
                xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECT_BIT);
                // create_wifi_reconnect_task(NULL, 5);          
            }else{  //未找到指定SSID AP 或者验证错误
                wifi_connect_retry = 0;
                wifi_config_t ap_config = {
                    .ap = {
                        .ssid = "ESPConfig",
                        .ssid_len = strlen("ESPConfig"),
                        .password = "",
                        .authmode = WIFI_AUTH_OPEN,
                        .max_connection = 2,
                    },
                };
                if(strlen((const char*)ap_config.ap.password) == 0) ap_config.ap.authmode = WIFI_AUTH_OPEN;
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));  
                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
                ESP_LOGI(TAG, "NOT FOUND STA OR AUTH ERROR, START CONFIG AP");
                xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECT_BIT);              
            }
        }
        break;
    case SYSTEM_EVENT_AP_START:
        // xEventGroupSetBits(http_event_group, HTTP_SERVER_START_BIT);
        notice_http_server_task(true);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void get_wifi_config_from_url(char* buf, sta_wifi_config_t* sta_wifi_cfg){
    // uint8_t ssid[64], passwd[128];
    uint8_t* src_data = NULL;
    int len;
    // if(ssid_len == NULL){
    //     len = 32;
    // }else{
    //     len = *ssid_len;
    // }
    memset(sta_wifi_cfg->ssid, 0, sizeof(sta_wifi_cfg->ssid));
    memset(sta_wifi_cfg->password, 0, sizeof(sta_wifi_cfg->password));

    len = sizeof(sta_wifi_cfg->ssid);
    src_data = malloc(len*2);
    get_config_from_url(buf, "wifi_ssid", src_data, len*2);
    urldecode((const char*)src_data, (len*2), (char*)(sta_wifi_cfg->ssid), &len);
    // ESP_LOGI(TAG, "SSID: %s", sta_wifi_cfg->ssid);
    free(src_data);

    // if(passwd_len == NULL){
    //     len = 64;
    // }else{
    //     len = *passwd_len;
    // }
    len = sizeof(sta_wifi_cfg->password);
    src_data = malloc(len*2);
    get_config_from_url(buf, "wifi_passwd", src_data, len*2);
    urldecode((const char*)src_data, (len*2), (char*)(sta_wifi_cfg->password), &len);
    // ESP_LOGI(TAG, "PASSWD: %s", sta_wifi_cfg->password);
    free(src_data);
}

void wifi_reconnect_task(void* parm){
     
    while (true){
         /* code */
        xEventGroupWaitBits(wifi_event_group, WIFI_DISCONNECT_BIT, true, true, portMAX_DELAY);
        vTaskDelay(1000);
        ESP_LOGI(TAG, "ESP STARTING WiFi.....");
        ESP_ERROR_CHECK(esp_wifi_start());
        
    }
}

void initialise_wifi(void *arg)
{
    esp_err_t err;
    sta_wifi_config_t sta_wifi_cfg;

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, arg));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    
    memset(sta_wifi_cfg.ssid, 0, sizeof(sta_wifi_cfg.ssid));
    memset(sta_wifi_cfg.password, 0, sizeof(sta_wifi_cfg.password));

    wifi_event_group = xEventGroupCreate();

    // err = read_wifi_config(&sta_wifi_cfg);
    size_t cfg_size = sizeof(wifi_config_t);
    err = read_data_from_nvs(WIFI_CONFIG_NAMESPACE, WIFI_CONFIG_KEY, &sta_wifi_cfg, cfg_size);
    if(err == ESP_OK){
        wifi_config_t sta_config = {
            .sta = {
                .ssid = "",
                .password = "",
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
            },
        };
        ESP_LOGI(TAG, "Read WIFI Config: SSID(len:%d): %s, PASSWD(len:%d): %s", (int)strlen((char*)sta_wifi_cfg.ssid), sta_wifi_cfg.ssid, (int)strlen((char*)sta_wifi_cfg.password), sta_wifi_cfg.password);
        memcpy(sta_config.sta.ssid, sta_wifi_cfg.ssid, sizeof(sta_wifi_cfg.ssid));
        memcpy(sta_config.sta.password, sta_wifi_cfg.password, sizeof(sta_wifi_cfg.password));
        // ESP_LOGI(TAG, "WIFI SSID(len:%d): %s, PASSWD(len:%d): %s", (int)strlen((char*)sta_config.sta.ssid), sta_config.sta.ssid, (int)strlen((char*)sta_config.sta.password), sta_config.sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
        // ESP_ERROR_CHECK(esp_wifi_set_auto_connect(true));       
    }else{
        // ESP_LOGI(TAG, "Read WIFI Config: SSID(len:%d): %s, PASSWD(len:%d): %s", (int)strlen((char*)sta_wifi_cfg.ssid), sta_wifi_cfg.ssid, (int)strlen((char*)sta_wifi_cfg.password), sta_wifi_cfg.password);
        wifi_config_t ap_config = {
            .ap = {
                .ssid = "ESPConfig",
                .ssid_len = strlen("ESPConfig"),
                .password = "",
                .authmode = WIFI_AUTH_WPA2_PSK,
                .max_connection = 2,
            },
        };
        if(strlen((const char*)ap_config.ap.password) == 0) ap_config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MODEM));  //省电模式
}


void create_wifi_reconnect_task(void * const pvParameters, UBaseType_t uxPriority){
    BaseType_t xRetrun = pdFAIL;
    if(wifi_reconnect_task_handle == NULL){
        xRetrun = xTaskCreate(wifi_reconnect_task, "wifi_reconnect_task", 1024, pvParameters, uxPriority, &wifi_reconnect_task_handle);
        if(xRetrun != pdPASS){
            ESP_LOGI(TAG, "\"wifi_reconnect_task\" create flailure");
        }
    }
}

void delete_wifi_reconnect_task(void){
    if(wifi_reconnect_task_handle != NULL){
        vTaskDelete(wifi_reconnect_task_handle);
        wifi_reconnect_task_handle = NULL;
    }
}

