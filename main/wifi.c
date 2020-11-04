

#include "main.h"


/**
 * 连接到 WIFI, LED 闪烁频率 5Hz
 * 启动WIFI AP, LED 闪烁频率 10Hz
 * 
 * */

static const char *TAG="## WIFI ##";

#define WIFI_CONNECT_RETRY                           10
#define WIFI_SSID_KEY                          "wifi_ssid"
#define WIFI_PASSWORD_KEY                      "wifi_passwd"


static int8_t wifi_connect_retry = WIFI_CONNECT_RETRY;

EventGroupHandle_t wifi_event_group = NULL;
const EventBits_t WIFI_DISCONNECT_BIT = BIT0;
static const EventBits_t WIFI_SCAN_DONE_BIT = BIT1;
static const EventBits_t WIFI_SCAN_START_BIT = BIT2;
TaskHandle_t wifi_reconnect_task_handle = NULL;

static TaskHandle_t wifi_scan_task_handle = NULL;

static bool ap_start = false;
static bool is_connected = false;

static wifi_ap_record_t* ap_record_list = NULL;

wifi_config_t sta_wifi_cfg;

esp_err_t save_wifi_config_to_nvs(wifi_config_t* wifi_config){

    wifi_config = wifi_config;
    nvs_handle n_nvs_handle;
    esp_err_t err;
    
    err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    err = nvs_set_str(n_nvs_handle, WIFI_SSID_KEY, (char*)wifi_config->sta.ssid);
    if(err != ESP_OK) return err;

    err = nvs_set_str(n_nvs_handle, WIFI_PASSWORD_KEY, (char*)wifi_config->sta.password);
    if(err != ESP_OK) return err;

    err = nvs_commit(n_nvs_handle);
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
}


esp_err_t read_wifi_config_from_nvs(wifi_config_t* wifi_config){

    wifi_config = wifi_config;

    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t len;

    err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    err = nvs_get_str(n_nvs_handle, WIFI_SSID_KEY, NULL, &len);
    if(err != ESP_OK) return err;
    nvs_get_str(n_nvs_handle, WIFI_SSID_KEY, (char*)wifi_config->sta.ssid, &len);

    err = nvs_get_str(n_nvs_handle, WIFI_PASSWORD_KEY, NULL, &len);
    if(err != ESP_OK) return err;
    nvs_get_str(n_nvs_handle, WIFI_PASSWORD_KEY, (char*)wifi_config->sta.password, &len);

    nvs_close(n_nvs_handle);
    return ESP_OK;
}


static void setup_sta(char* ssid, char* password){
    wifi_config_t sta_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
        },
    };
    if(ssid != NULL) memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
    if(password != NULL) memcpy(sta_config.sta.password, password, strlen(password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
}
static void setup_ap(void){
    char ap_ssid[32];
    strcpy(ap_ssid, "ESPConfig_");
    strcat(ap_ssid, e_chip_info.base_mac);
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "",
            .ssid_len = strlen(ap_ssid),
            .password = "",
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 3,
        },
    };
    strcpy((char*)ap_config.ap.ssid, ap_ssid);
    if(strlen((const char*)ap_config.ap.password) == 0) ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
}

static void setup_ap_sta(char* sta_ssid, char* sta_password){
    char ap_ssid[32];
    strcpy(ap_ssid, "ESPConfig_");
    strcat(ap_ssid, e_chip_info.base_mac);
    wifi_config_t ap_sta_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL
        },
        .ap = {
            .ssid = "",
            .ssid_len = strlen(ap_ssid),
            .password = "",
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 3
        }
    };
    if(sta_ssid != NULL) memcpy(ap_sta_config.sta.ssid, sta_ssid, strlen(sta_ssid));
    if(sta_password != NULL) memcpy(ap_sta_config.sta.password, sta_password, strlen(sta_password));
    strcpy((char*)ap_sta_config.ap.ssid, ap_ssid);
    if(strlen((const char*)ap_sta_config.ap.password) == 0) ap_sta_config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_sta_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &ap_sta_config));
}


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
        is_connected = true;
        ap_start  = false;
        // delete_wifi_reconnect_task();
        xEventGroupClearBits(wifi_event_group, WIFI_SCAN_START_BIT);
        xEventGroupClearBits(wifi_event_group, WIFI_DISCONNECT_BIT);
        set_led_blink_freq(5);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        ESP_LOGI(TAG, "Got IP: '%s'",
                ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
                
        ESP_ERROR_CHECK(esp_wifi_scan_stop());
        delete_wifi_scan_task();       //

        create_mqtt_connect_to_broker_task(NULL, 6);
        // xEventGroupSetBits(wifi_event_group, WIFI_GOT_IP_BIT);
        // xEventGroupSetBits(http_event_group, HTTP_SERVER_START_BIT);
        notice_http_server_task(true);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        is_connected = false;
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        ESP_LOGE(TAG, "disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        }
        if((--wifi_connect_retry) > 0){
            ESP_ERROR_CHECK(esp_wifi_connect());
        }else{                                //达到最大重连次数
            delete_mqtt_connect_to_broker_task();
            create_wifi_scan_task(NULL, 5);
            wifi_connect_retry = WIFI_CONNECT_RETRY;
            xEventGroupSetBits(wifi_event_group, WIFI_SCAN_START_BIT);
            // ESP_ERROR_CHECK(esp_wifi_stop());
            // create_wifi_reconnect_task(NULL, 5);
            // if((info->disconnected.reason != WIFI_REASON_NO_AP_FOUND) && (info->disconnected.reason != WIFI_REASON_AUTH_FAIL)){
            //     ESP_LOGI(TAG, "restart Wifi...............");
            //     wifi_connect_retry = WIFI_CONNECT_RETRY;
            //     xEventGroupSetBits(wifi_event_group, WIFI_SCAN_START_BIT);
            //     // xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECT_BIT);        
            // }else{  //未找到指定SSID AP 或者验证错误
            //     wifi_connect_retry = 0;
            //     if(!ap_start){
            //         esp_wifi_stop();
            //         setup_ap_sta(NULL, NULL);
            //         ESP_ERROR_CHECK(esp_wifi_start());
            //         ESP_LOGI(TAG, "NOT FOUND STA OR AUTH ERROR, START CONFIG AP");
            //     }
            //     // create_wifi_scan_task(NULL, 5);
            //     xEventGroupSetBits(wifi_event_group, WIFI_SCAN_START_BIT);
            //     // xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECT_BIT);              
            // }
        }
        break;
    case SYSTEM_EVENT_AP_START:
        ap_start = true;
        // xEventGroupSetBits(http_event_group, HTTP_SERVER_START_BIT);
        notice_http_server_task(true);
        set_led_blink_freq(10);
        break;
    case SYSTEM_EVENT_AP_STOP:
        ap_start = false;
    break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        is_connected = true;
    break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        is_connected = false;
    break;
    case SYSTEM_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "scan is done");
        xEventGroupSetBits(wifi_event_group, WIFI_SCAN_DONE_BIT);
    break;
    default:
        break;
    }
    return ESP_OK;
}

void get_wifi_config_from_url(char* buf, wifi_config_t* sta_wifi_cfg){
    // uint8_t ssid[64], passwd[128];
    uint8_t* src_data = NULL;
    int len;
    // if(ssid_len == NULL){
    //     len = 32;
    // }else{
    //     len = *ssid_len;
    // }
    memset(sta_wifi_cfg->sta.ssid, 0, sizeof(sta_wifi_cfg->sta.ssid));
    memset(sta_wifi_cfg->sta.password, 0, sizeof(sta_wifi_cfg->sta.password));

    len = sizeof(sta_wifi_cfg->sta.ssid);
    src_data = malloc(len*2);
    get_config_from_url(buf, "wifi_ssid", src_data, len*2);
    urldecode((const char*)src_data, (len*2), (char*)(sta_wifi_cfg->sta.ssid), &len);
    // ESP_LOGI(TAG, "SSID: %s", sta_wifi_cfg.sta.ssid);
    free(src_data);

    // if(passwd_len == NULL){
    //     len = 64;
    // }else{
    //     len = *passwd_len;
    // }
    len = sizeof(sta_wifi_cfg->sta.password);
    src_data = malloc(len*2);
    get_config_from_url(buf, "wifi_passwd", src_data, len*2);
    urldecode((const char*)src_data, (len*2), (char*)(sta_wifi_cfg->sta.password), &len);
    // ESP_LOGI(TAG, "PASSWD: %s", sta_wifi_cfg.sta.password);
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

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, arg));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    
    memset(sta_wifi_cfg.sta.ssid, 0, sizeof(sta_wifi_cfg.sta.ssid));
    memset(sta_wifi_cfg.sta.password, 0, sizeof(sta_wifi_cfg.sta.password));

    wifi_event_group = xEventGroupCreate();

    // err = read_wifi_config(&sta_wifi_cfg);
    size_t cfg_size = sizeof(wifi_config_t);
    err = read_data_from_nvs(WIFI_CONFIG_NAMESPACE, WIFI_CONFIG_KEY, &sta_wifi_cfg, cfg_size);
    if(err == ESP_OK){
        ESP_LOGI(TAG, "read wifi config:\r\n    ssid:%s, password:%s", sta_wifi_cfg.sta.ssid, sta_wifi_cfg.sta.password);
        setup_sta((char*)sta_wifi_cfg.sta.ssid, (char*)sta_wifi_cfg.sta.password);
        // wifi_config_t sta_config = {
        //     .sta = {
        //         .ssid = "",
        //         .password = "",
        //         .scan_method = WIFI_ALL_CHANNEL_SCAN,
        //         .sort_method = WIFI_CONNECT_AP_BY_SIGNAL
        //     },
        // };
        // ESP_LOGI(TAG, "Read WIFI Config: SSID(len:%d): %s, PASSWD(len:%d): %s", (int)strlen((char*)sta_wifi_cfg.sta.ssid), sta_wifi_cfg.sta.ssid, (int)strlen((char*)sta_wifi_cfg.sta.password), sta_wifi_cfg.sta.password);
        // memcpy(sta_config.sta.ssid, sta_wifi_cfg.sta.ssid, sizeof(sta_wifi_cfg.sta.ssid));
        // memcpy(sta_config.sta.password, sta_wifi_cfg.sta.password, sizeof(sta_wifi_cfg.sta.password));
        // // ESP_LOGI(TAG, "WIFI SSID(len:%d): %s, PASSWD(len:%d): %s", (int)strlen((char*)sta_config.sta.ssid), sta_config.sta.ssid, (int)strlen((char*)sta_config.sta.password), sta_config.sta.password);
        // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        // ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
        // ESP_ERROR_CHECK(esp_wifi_set_auto_connect(true));       
    }else{
        // ESP_LOGI(TAG, "Read WIFI Config: SSID(len:%d): %s, PASSWD(len:%d): %s", (int)strlen((char*)sta_wifi_cfg.sta.ssid), sta_wifi_cfg.sta.ssid, (int)strlen((char*)sta_wifi_cfg.sta.password), sta_wifi_cfg.sta.password);
        setup_ap();
        // wifi_config_t ap_config = {
        //     .ap = {
        //         .ssid = "ESPConfig",
        //         .ssid_len = strlen("ESPConfig"),
        //         .password = "",
        //         .authmode = WIFI_AUTH_WPA2_PSK,
        //         .max_connection = 2,
        //     },
        // };
        // if(strlen((const char*)ap_config.ap.password) == 0) ap_config.ap.authmode = WIFI_AUTH_OPEN;
        // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        // ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MODEM));  //省电模式
}


void create_wifi_reconnect_task(void * const pvParameters, UBaseType_t uxPriority){
    BaseType_t xRetrun = pdFAIL;
    if(wifi_reconnect_task_handle == NULL){
        xRetrun = xTaskCreate(wifi_reconnect_task, "wifi_reconnect_task", 1024, pvParameters, uxPriority, &wifi_reconnect_task_handle);
        if(xRetrun != pdPASS){
            ESP_LOGE(TAG, "\"wifi_reconnect_task\" create flailure");
        }
    }
}

void delete_wifi_reconnect_task(void){
    if(wifi_reconnect_task_handle != NULL){
        vTaskDelete(wifi_reconnect_task_handle);
        wifi_reconnect_task_handle = NULL;
    }
}


void wifi_scan_task(void* pvParamters){

    uint16_t ap_numbers = 0, ap_index = 0;
    EventBits_t xBit;
    esp_err_t err;
    uint8_t scan_time = 6;

    const wifi_scan_config_t wifi_scan_cfg = {
        .ssid = NULL,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 500,
        .scan_time.passive = 500
    };


    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    setup_sta(NULL, NULL);

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_scan_start(&wifi_scan_cfg, false));

    while(true){
        xBit = xEventGroupWaitBits(wifi_event_group, WIFI_SCAN_DONE_BIT | WIFI_SCAN_START_BIT, true, false, portMAX_DELAY);
        scan_time -= 1;
        if(xBit == WIFI_SCAN_DONE_BIT){
            esp_wifi_scan_get_ap_num(&ap_numbers);
            ap_record_list = malloc(sizeof(wifi_ap_record_t) * ap_numbers);
            memset(ap_record_list, 0, sizeof(wifi_ap_record_t) * ap_numbers);
            esp_wifi_scan_get_ap_records(&ap_numbers, ap_record_list);
            if(ap_numbers != 0){
#if(NVS_DATA == 1)
                size_t cfg_size = sizeof(wifi_config_t);
                // wifi_config_t sta_wifi_cfg;
                err = read_data_from_nvs(WIFI_CONFIG_NAMESPACE, WIFI_CONFIG_KEY, &sta_wifi_cfg, cfg_size);
#else
                memset(sta_wifi_cfg.sta.ssid, 0, sizeof(sta_wifi_cfg.sta.ssid));
                memset(sta_wifi_cfg.sta.password, 0, sizeof(sta_wifi_cfg.sta.password));
                err = read_wifi_config_from_nvs(&sta_wifi_cfg);
#endif
                if(err == ESP_OK){
                    ESP_LOGI(TAG, "read wifi config:\r\n    ssid:%s, password:%s", sta_wifi_cfg.sta.ssid, sta_wifi_cfg.sta.password);
                    for(ap_index = 0; ap_index < ap_numbers; ap_index++){    
                        ESP_LOGI(TAG, "find ap's ssid(%d/%d):%s", ap_index + 1, ap_numbers, ap_record_list[ap_index].ssid);
                        if(strcmp((char*)ap_record_list[ap_index].ssid, (char*)sta_wifi_cfg.sta.ssid) == 0){  // 扫描到目标wifi
                            ESP_ERROR_CHECK(esp_wifi_stop());
                            vTaskDelay(1000 / portTICK_PERIOD_MS);         
                            setup_sta((char*)sta_wifi_cfg.sta.ssid, (char*)sta_wifi_cfg.sta.password);                         
                            ESP_ERROR_CHECK(esp_wifi_start());
                            break;
                        }
                    }                 
                }else{
                    if(!ap_start){
                        ESP_ERROR_CHECK(esp_wifi_stop());
                        vTaskDelay(1000 / portTICK_PERIOD_MS);
                        setup_ap();
                        ESP_ERROR_CHECK(esp_wifi_start());
                    }                   
                }
            }
            ap_numbers = 0;
            free(ap_record_list);
            if(scan_time == 0){
                ESP_ERROR_CHECK(esp_wifi_stop());
                setup_ap_sta(NULL, NULL);
                ESP_ERROR_CHECK(esp_wifi_start());
            }
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            if(!is_connected) ESP_ERROR_CHECK(esp_wifi_scan_start(&wifi_scan_cfg, false));
        }else if(xBit == WIFI_SCAN_START_BIT){
            if(!is_connected){
                wifi_mode_t wifi_mode;
                ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode));
                if(wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_STA){
                    err = esp_wifi_scan_start(&wifi_scan_cfg, false);
                    if(err == ESP_ERR_WIFI_NOT_STARTED){
                        ESP_ERROR_CHECK(esp_wifi_start());
                        ESP_ERROR_CHECK(esp_wifi_scan_start(&wifi_scan_cfg, false));
                    }
                }else{
                    ESP_ERROR_CHECK(esp_wifi_stop());
                    setup_sta(NULL, NULL);
                    ESP_ERROR_CHECK(esp_wifi_start());
                    ESP_ERROR_CHECK(esp_wifi_scan_start(&wifi_scan_cfg, false));
                }
            }
        }
    }
}

void create_wifi_scan_task(void * const pvParameters, UBaseType_t uxPriority){
    BaseType_t xReturn = pdFAIL;

    if(wifi_event_group == NULL) wifi_event_group = xEventGroupCreate();

    if(wifi_scan_task_handle == NULL){
        xReturn = xTaskCreate(wifi_scan_task, "wifi_scan_task", 2048, pvParameters, uxPriority, &wifi_scan_task_handle);
        if(xReturn != pdPASS){
            ESP_LOGE(TAG, "\"wifi_scan_task\" create flailure");
        }
    }
}

void delete_wifi_scan_task(void){
    if(wifi_scan_task_handle != NULL){
        vTaskDelete(wifi_scan_task_handle);
        wifi_scan_task_handle = NULL;
    }
}

void wifi_start_scan(void){
    if(wifi_event_group != NULL)
        xEventGroupSetBits(wifi_event_group, WIFI_SCAN_START_BIT);
}

