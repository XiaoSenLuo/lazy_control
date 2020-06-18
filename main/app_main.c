/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <string.h>
#include "math.h"

#include "driver/gpio.h"
#include "driver/adc.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "nvs.h"
#include <sys/param.h>
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "mbedtls/md5.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include <esp_http_server.h>
#include "urldecode.h"

#include "mqtt_client.h"

#include "cJSON.h"

static const char *TAG="APP";

#define GPIO_4                                     GPIO_NUM_4
#define GPIO_5                                     GPIO_NUM_5

// #define WIFI_SSID "XL001_2.4G"
// #define WIFI_PASSWD "vHZ-sRk-s3p-czW"

static EventGroupHandle_t esp_event_group;
static const EventBits_t ESP_RESTART_BIT = BIT31;

#define WIFI_CONFIG_NAMESPACE                        "wifi_config"
#define WIFI_CONFIG_KEY                              "wifi_config"
#define WIFI_CONNECT_RETRY                           3
static uint8_t wifi_connect_retry = WIFI_CONNECT_RETRY;
static EventGroupHandle_t wifi_event_group;
static const EventBits_t WIFI_DISCONNECT_BIT = BIT0;
static TaskHandle_t wifi_reconnect_task_handle = NULL;

static httpd_handle_t http_server_handle = NULL;
static EventGroupHandle_t http_event_group;
static TaskHandle_t http_server_task_handle = NULL;
static const EventBits_t HTTP_SERVER_START_BIT = BIT4;
static const EventBits_t HTTP_SERVER_STOP_BIT  = BIT5;
static const EventBits_t HTTP_WIFI_SAVE_CONFIG_BIT = BIT1;
static const EventBits_t HTTP_MQTT_CLIENT_SAVE_CONFIG_BIT = BIT9;
static TaskHandle_t http_req_handle_task_handle = NULL;
#define Q_HTTP_REQ_SIZE       4
static QueueHandle_t q_http_req = NULL;

// #define MQTT_BROKER_URL "mqtt.flespi.io"
// #define MQTT_BROKER_USERNAME "vSvy3tP5RfCcZTqigojCUmPAAoRSTqAe9TC0cfTRklU738f9mpz4zcsYq9G9p4bm"
#define MQTT_TOPIC_BASE_PATH                   "/topic/esp8266/mac/"
#define MQTT_TOPIC_DEVICE_INFO                 "/chip_info"
#define MQTT_TOPIC_CONTROL_0                   "/control0"
#define MQTT_TOPIC_CONTROL_1                   "/control1"

#define MQTT_CONFIG_NAMESPACE                  "mqtt_config"
#define MQTT_CONFIG_KEY                        "mqtt_config"
#define MQTT_TOPIC_NAMESPACE                   "mqtt_topic"
#define MQTT_TOPIC_KEY                         "matt_topic"
#define MQTT_CONNECT_RETRY                     5


static uint8_t mqtt_connecct_retry = MQTT_CONNECT_RETRY;
esp_mqtt_client_handle_t mqtt_client = NULL;
static EventGroupHandle_t mqtt_event_group;
static const EventBits_t MQTT_CLIENT_START_BIT = BIT8;
static const EventBits_t MQTT_CLIENT_STOP_BIT = BIT10;
static const EventBits_t MQTT_CLIENT_CONNECTED_BIT = BIT11;
static TaskHandle_t mqtt_connect_to_broker_task_handle = NULL;
static TaskHandle_t mqtt_event_recv_task_handle = NULL;
#define Q_MQTT_CLIENT_SIZE    4
static QueueHandle_t q_mqtt_client_event = NULL;


#define SPIFFS_BASE_PATH                           "/c"
#define HTTPD_WEB_ROOT                             "/c/www"
#define SPIFFS_PARTITION_LABEL                     "storage"

static void phripherals_initialise(void){
    gpio_config_t io_config;

    io_config.pin_bit_mask = ((1ULL << GPIO_4) | (1ULL << GPIO_5));
    io_config.mode = GPIO_MODE_OUTPUT;
    io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_config.pull_up_en = GPIO_PULLUP_DISABLE;
    io_config.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_config);
    
    // // 1. init adc
    adc_config_t adc_config;

    // Depend on menuconfig->Component config->PHY->vdd33_const value
    // When measuring system voltage(ADC_READ_VDD_MODE), vdd33_const must be set to 255.
    adc_config.mode = ADC_READ_VDD_MODE;
    adc_config.clk_div = 8; // ADC sample collection clock = 80MHz/clk_div = 10MHz
    ESP_ERROR_CHECK(adc_init(&adc_config));   

}


static size_t get_file_size(const char * file, size_t* file_size, int* err){
    struct stat stat_info;
    size_t fs = 0;
    int xReturn = -1;
    int _err = 0;
    file_size = file_size;
    err = err;                      //避免 [-Werror=unused-but-set-parameter]
    xReturn = stat(file, &stat_info);
    if(0 == xReturn){
        fs = stat_info.st_size;
        _err = 0;
    }else{
        fs = 0;
        _err = *__errno();
    }
    if(file_size != NULL) *file_size = fs;
    if(err != NULL) *err = _err;
    return fs;
}

static void get_config_from_url(const char* config_data, const char* config_key, uint8_t* value, uint8_t value_len){
    const char* str = config_data;
    const char* chr = config_key;
    int str_len = strlen(str), chr_len = strlen(chr), i=0;
    char s[16];
    char ss[128];
    do{
        memset(s, 0, sizeof(s));
        memcpy(s, &str[i], chr_len);
        s[chr_len] = '\0';
//        printf("%d, %s\n", i, s);
        if(strcmp(s, chr) == 0){
            char ptr;
            int k = 0;
            do{
                ptr = str[i+chr_len+k+1];
                ++k;
                if((ptr == '&') || (ptr == '\0')){
                    memcpy(ss, &str[i+chr_len+1], k-1);
                    ss[k-1] = '\0';
                    // strcpy((char*)value, ss);
                    // printf("%s\n", ss);
                    uint8_t vl = ((value_len) > (k)) ? (k) : (value_len);
                    memcpy(value, ss, vl);
                    return;
                }
            }while((ptr != '&') && (ptr != '\0'));
        }
        ++i;
    }while (i + chr_len <= str_len);
    return;
}

int string2Int(char* str, int* num){
    size_t len = strlen(str);
    char sign = 0;
    int m = 0, n = 0, k = 0;
    char* src_data = str;

    if('-' == src_data[0]){
        sign = 1;
        len -= 1;
        src_data++;
    }
    for(m = 0; m < len; m++){
        k = *src_data++ - '0';
        n += (int)(k * pow(10, (len - 1 - m)));
    }
    if(sign) n *= -1;
    *num = n;
    return n;
}

static esp_err_t save_data_to_nvs(const char* namespace, const char* key, void* data, size_t data_size){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t len = data_size;
    
    err = nvs_open(namespace, NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    err = nvs_set_blob(n_nvs_handle, key, data, len);
    if(err != ESP_OK) return err;

    err = nvs_commit(n_nvs_handle);
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
}

static esp_err_t read_data_from_nvs(const char* namespace, const char* key, void* data, size_t data_size){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t len = data_size;

    err = nvs_open(namespace, NVS_READONLY, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;

    err = nvs_get_blob(n_nvs_handle, key, data, &len);
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;   
}

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
}sta_wifi_config_t;

sta_wifi_config_t sta_wifi_cfg;

/** 
static esp_err_t save_wifi_config(sta_wifi_config_t* sta_wifi_cfg){
    nvs_handle n_nvs_handle;
    esp_err_t err;

    err = nvs_open("wifi_config", NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    err = nvs_set_blob(n_nvs_handle, "wifi_ssid", sta_wifi_cfg->ssid, sizeof(sta_wifi_cfg->ssid)); //保存wifi ssid, 最大32byte
    if(err != ESP_OK) return err;

    err = nvs_set_blob(n_nvs_handle, "wifi_passwd", sta_wifi_cfg->password, sizeof(sta_wifi_cfg->password)); // 保存wifi密码, 最大64byte
    if(err != ESP_OK) return err;

    err = nvs_commit(n_nvs_handle);  //提交保存
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
} 

static esp_err_t save_wifi_config_to_nvs(const char* namespace, const char* key, sta_wifi_config_t* sta_wifi_cfg){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t len;
    
    err = nvs_open(namespace, NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    len = sizeof(sta_wifi_config_t);
    err = nvs_set_blob(n_nvs_handle, key, sta_wifi_cfg, len);
    if(err != ESP_OK) return err;

    err = nvs_commit(n_nvs_handle);
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
}

static esp_err_t read_wifi_config(sta_wifi_config_t* sta_wifi_cfg){
     nvs_handle n_nvs_handle;
     esp_err_t err;
     size_t ssid_len = sizeof(sta_wifi_cfg->ssid), passwd_len = sizeof(sta_wifi_cfg->password);

    err = nvs_open("wifi_config", NVS_READONLY, &n_nvs_handle);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;

    err = nvs_get_blob(n_nvs_handle, "wifi_ssid", sta_wifi_cfg->ssid, &ssid_len);
    if(err != ESP_OK) return err;

    err = nvs_get_blob(n_nvs_handle, "wifi_passwd", sta_wifi_cfg->password, &passwd_len);
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
}

static esp_err_t read_wifi_config_from_nvs(const char* namespace, const char* key, sta_wifi_config_t* sta_wifi_cfg){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t len;

    err = nvs_open(namespace, NVS_READONLY, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;

    len = sizeof(sta_wifi_config_t);
    err = nvs_get_blob(n_nvs_handle, key, sta_wifi_cfg, &len);
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
}
**/

static void get_wifi_config_from_url(char* buf, sta_wifi_config_t* sta_wifi_cfg){
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
    get_config_from_url(buf, "wifi_ssid", src_data, len);
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
    get_config_from_url(buf, "wifi_passwd", src_data, len);
    urldecode((const char*)src_data, (len*2), (char*)(sta_wifi_cfg->password), &len);
    // ESP_LOGI(TAG, "PASSWD: %s", sta_wifi_cfg->password);
    free(src_data);
}


typedef struct {
    char host[64];
    uint32_t port;
    char uri[64];
    char client_id[16];
    char username[128];
    char password[64];
}c_mqtt_config_t;

c_mqtt_config_t c_mqtt_cfg;

/**
static esp_err_t save_mqtt_config(c_mqtt_config_t* mqtt_cfg){
    nvs_handle n_nvs_handle;
    esp_err_t err;

    err = nvs_open("mqtt_config", NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;

    // if(mqtt_cfg->host != NULL && mqtt_cfg->host[0] != '\0'){
        err = nvs_set_blob(n_nvs_handle, "mqtt_host", mqtt_cfg->host, sizeof(mqtt_cfg->host));
        if(err != ESP_OK) return err;

        err = nvs_set_u32(n_nvs_handle, "mqtt_port", mqtt_cfg->port);
        if(err != ESP_OK) return err;
    // }

    // if(mqtt_cfg->uri != NULL && mqtt_cfg->uri[0] != '\0'){
        err = nvs_set_blob(n_nvs_handle, "mqtt_uri", mqtt_cfg->uri, sizeof(mqtt_cfg->uri));
        if(err != ESP_OK) return err;
    // }

    // if(mqtt_cfg->client_id != NULL && mqtt_cfg->client_id[0] != '\0'){
        err = nvs_set_blob(n_nvs_handle, "mqtt_client_id", mqtt_cfg->client_id, sizeof(mqtt_cfg->client_id));
        if(err != ESP_OK) return err;
    // }

    // if(mqtt_cfg->username != NULL && mqtt_cfg->username[0] != '\0'){
        err = nvs_set_blob(n_nvs_handle, "mqtt_username", mqtt_cfg->username, sizeof(mqtt_cfg->username));
        if(err != ESP_OK) return err;

        // if(mqtt_cfg->password != NULL && mqtt_cfg->password[0] != '\0'){
            err = nvs_set_blob(n_nvs_handle, "mqtt_passwd", mqtt_cfg->password, sizeof(mqtt_cfg->password));
            if(err != ESP_OK) return err;
        // }
    // }
    err = nvs_commit(n_nvs_handle);  //提交保存
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
}

static esp_err_t read_mqtt_config(c_mqtt_config_t* mqtt_cfg){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t len;

    err = nvs_open("mqtt_config", NVS_READONLY, &n_nvs_handle);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    // ESP_LOGI("Read MQTT Config", "READONLY: OpenSuccess!");
    len = sizeof(mqtt_cfg->host);
    err = nvs_get_blob(n_nvs_handle, "mqtt_host", mqtt_cfg->host, &len);
    if(err != ESP_OK){
        // mqtt_cfg->host = NULL;
        return err;
    }

    err = nvs_get_u32(n_nvs_handle, "mqtt_port", &(mqtt_cfg->port));
    if(err != ESP_OK){
        return err;
    }
    
    len = sizeof(mqtt_cfg->uri);
    err = nvs_get_blob(n_nvs_handle, "mqtt_uri", mqtt_cfg->uri, &len);
    if(err != ESP_OK){
        // mqtt_cfg->uri = NULL;
        return err;
    }
    
    len = sizeof(mqtt_cfg->client_id);
    err = nvs_get_blob(n_nvs_handle, "mqtt_client_id", mqtt_cfg->client_id, &len);
    if(err != ESP_OK){
        return err;
    }
    
    len = sizeof(mqtt_cfg->username);
    err = nvs_get_blob(n_nvs_handle, "mqtt_username", mqtt_cfg->username, &len);
    if(err != ESP_OK){
        return err;
    }
    
    len = sizeof(mqtt_cfg->password);
    err = nvs_get_blob(n_nvs_handle, "mqtt_passwd", mqtt_cfg->password, &len);
    if(err != ESP_OK){
        return err;
    }

    nvs_close(n_nvs_handle);
    return ESP_OK;
}

static esp_err_t save_mqtt_config_to_nvs(const char* namespace, const char* key, c_mqtt_config_t* mqtt_cfg){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t len;
    
    err = nvs_open(namespace, NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    len = sizeof(c_mqtt_config_t);
    err = nvs_set_blob(n_nvs_handle, key, mqtt_cfg, len);
    if(err != ESP_OK) return err;

    err = nvs_commit(n_nvs_handle);
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
}

static esp_err_t read_mqtt_config_from_nvs(const char* namespace, const char* key, c_mqtt_config_t* mqtt_cfg){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t len;

    err = nvs_open(namespace, NVS_READONLY, &n_nvs_handle);
    if(err != ESP_OK  && err != ESP_ERR_NVS_NOT_FOUND) return err;

    len = sizeof(c_mqtt_config_t);
    err = nvs_get_blob(n_nvs_handle, key, mqtt_cfg, &len);
    if(err != ESP_OK) return err;

    nvs_close(n_nvs_handle);
    return ESP_OK;
}
**/

static void get_mqtt_config_from_url(char* buf, c_mqtt_config_t* mqtt_cfg){
    uint8_t* src_data = NULL;
    int len = 0;

    len = sizeof(mqtt_cfg->host);
    src_data = malloc(len);
    get_config_from_url(buf, "mqtt_host", src_data, len);
    urldecode((const char*)src_data, len, mqtt_cfg->host, &len);
    free(src_data);

    src_data = malloc(32);
    get_config_from_url(buf, "mqtt_port", src_data, len);
    string2Int((char*)src_data, (int*)&mqtt_cfg->port);
    free(src_data);

    len = sizeof(mqtt_cfg->uri);
    src_data = malloc(len);
    get_config_from_url(buf, "mqtt_uri", src_data, len);
    urldecode((const char*)src_data, len, mqtt_cfg->uri, &len);
    free(src_data);
    
    len = sizeof(mqtt_cfg->client_id);
    src_data = malloc(len);
    get_config_from_url(buf, "mqtt_client_id", src_data, len);
    urldecode((const char*)src_data, len, mqtt_cfg->client_id, &len);
    if(strlen(mqtt_cfg->client_id) == 0) strcpy(mqtt_cfg->client_id, "esp8266-0x29");
    free(src_data);

    len = sizeof(mqtt_cfg->username);
    src_data = malloc(len);
    get_config_from_url(buf, "mqtt_username", src_data, len);
    urldecode((const char*)src_data, len, mqtt_cfg->username, &len);
    free(src_data);

    len = sizeof(mqtt_cfg->password);
    src_data = malloc(len);
    get_config_from_url(buf, "mqtt_passwd", src_data, len);
    urldecode((const char*)src_data, len, mqtt_cfg->password, &len);
    free(src_data);
}


typedef struct {
    char topic[128];
    uint8_t type;   /* Subscribe: 0        Publish: 1  */
    uint8_t qos;    /* Qos0:0, Qos1:1, Qos2:2 */
}c_mqtt_topic_t;

static void get_mqtt_topic_from_url(char* buf, c_mqtt_topic_t* c_mqtt_topic){
    uint8_t* src_data = NULL;
    int len;

    memset(c_mqtt_topic->topic, 0, sizeof(c_mqtt_topic->topic));

    len = sizeof(c_mqtt_topic->topic);
    src_data = malloc(len*2);
    get_config_from_url(buf, "mqtt_topic", src_data, len);
    urldecode((const char*)src_data, (len*2), c_mqtt_topic->topic, &len);
    free(src_data);

    len = 4;
    src_data = malloc(len*2);
    get_config_from_url(buf, "topic_type", src_data, len);
    string2Int((char*)src_data, (int*)&(c_mqtt_topic->type));
    memset(src_data, 0, len*2);
    get_config_from_url(buf, "topic_qos", src_data, len);
    string2Int((char*)src_data, (int*)&(c_mqtt_topic->qos));
    free(src_data);
}

/**
static esp_err_t save_mqtt_topic_to_nvs(const char* namespace, const char* key, c_mqtt_topic_t* c_mqtt_topic){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    
    err = nvs_open(namespace, NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    err = nvs_set_blob(n_nvs_handle, key, (c_mqtt_topic_t*)c_mqtt_topic, sizeof(c_mqtt_topic_t));
    if(err != ESP_OK) return err;
    
    err = nvs_commit(n_nvs_handle);
    if(err != ESP_OK) return err;
    
    nvs_close(n_nvs_handle);
    ESP_LOGI("## NVS ##", "Save Success!");
    return ESP_OK;
}

static esp_err_t read_mqtt_topic_from_nvs(const char* namespace, const char* key, c_mqtt_topic_t* c_mqtt_topic, size_t* req_size){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    size_t request_size;

    err = nvs_open(namespace, NVS_READONLY, &n_nvs_handle);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    
    err = nvs_get_blob(n_nvs_handle, key, NULL, &request_size);
    if(err != ESP_OK) return err;
    // ESP_LOGI("## NVS ##", "reuest_size:%d", request_size);
    if(request_size == 0){
        c_mqtt_topic = NULL;
        *req_size = 0;
    }else{
        if(c_mqtt_topic == NULL && req_size != NULL){ // 获取大小返回
            *req_size = request_size;
            return ESP_OK;
        }else if(c_mqtt_topic != NULL){
            if(req_size == NULL){ //req_size为NULL, 只读取sizeof(c_mqtt_topic_t)
                request_size = sizeof(c_mqtt_topic_t);
                err = nvs_get_blob(n_nvs_handle, key, c_mqtt_topic, &request_size);
                if(err != ESP_OK) return err;
            }else{
                err = nvs_get_blob(n_nvs_handle, key, c_mqtt_topic, req_size);
                if(err != ESP_OK) return err;
            }
        }
    }
    nvs_close(n_nvs_handle);
    return ESP_OK;
}
**/

typedef struct {
    esp_chip_info_t esp_info;
    char base_mac[18];
    uint8_t flash_size;
    uint16_t voltage; 
}e_chip_info_t;

e_chip_info_t e_chip_info;

/**
{
  "chip_info":
  {
    "model":"esp8266",
	"feature":["EMB_FLASH", "WIFI_BGN"],
	"core":2,
	"revision":2
  },
  "base_mac":"00-00-00-00-00-00",
  "flash_size": 32,
  "voltage":3300
} 
 **/
static char* cJ_create_chip_info(e_chip_info_t* e_chip_info){
    
    if(e_chip_info == NULL) return NULL;

    char* cJ_print = NULL;
    cJSON* cJ_root = NULL;
    cJSON* cJ_chip_info = NULL;
    cJSON* cJ_Item = NULL;
    
    cJ_root = cJSON_CreateObject();
    if(cJ_root == NULL) goto end;
    cJ_chip_info = cJSON_CreateObject();
    if(cJ_chip_info == NULL) goto end;

    cJSON_AddItemToObject(cJ_root, "chip_info", cJ_chip_info);

    cJ_Item = cJSON_CreateString(e_chip_info->base_mac);
    if(cJ_Item == NULL) goto end;
    cJSON_AddItemToObject(cJ_root, "base_mac", cJ_Item);

    const char *fm[] = {"FLASH_SIZE_4M_MAP_256_256", 
                    "FLASH_SIZE_2M",
                    "FLASH_SIZE_8M_MAP_512_512",
                    "FLASH_SIZE_16M_MAP_512_512",
                    "FLASH_SIZE_32M_MAP_512_512",
                    "FLASH_SIZE_16M_MAP_1024_1024",
                    "FLASH_SIZE_32M_MAP_1024_1024",
                    "FLASH_SIZE_32M_MAP_2048_2048",
                    "FLASH_SIZE_64M_MAP_1024_1024",
                    "FLASH_SIZE_128M_MAP_1024_1024",
                    "FALSH_SIZE_MAP_MAX"};

    cJ_Item = cJSON_CreateString(fm[e_chip_info->flash_size]);
    if(cJ_Item == NULL) goto end;
    cJSON_AddItemToObject(cJ_root, "flash_size", cJ_Item);
    
    cJ_Item = cJSON_CreateNumber(e_chip_info->voltage);
    if(cJ_Item == NULL) goto end;
    cJSON_AddItemToObject(cJ_root, "voltage", cJ_Item);
    
    const char *em[] = {"ESP8266", "ESP32"};

    cJ_Item = cJSON_CreateString(em[e_chip_info->esp_info.model]);
    if(cJ_Item == NULL) goto end;
    cJSON_AddItemToObject(cJ_chip_info, "model", cJ_Item);
    
    cJ_Item = cJSON_CreateNumber(e_chip_info->esp_info.cores);
    if(cJ_Item == NULL) goto end;
    cJSON_AddItemToObject(cJ_chip_info, "core", cJ_Item);

    cJ_Item = cJSON_CreateArray();
    if(cJ_Item == NULL) goto end;
    if(e_chip_info->esp_info.features & CHIP_FEATURE_EMB_FLASH){
        cJSON_AddItemToArray(cJ_Item, cJSON_CreateString("EMB_FLASH"));
    }
    if(e_chip_info->esp_info.features & CHIP_FEATURE_WIFI_BGN){
        cJSON_AddItemToArray(cJ_Item, cJSON_CreateString("WIFI_BGN"));
    }
    cJSON_AddItemToObject(cJ_chip_info, "features", cJ_Item);

    cJ_Item = cJSON_CreateNumber(e_chip_info->esp_info.revision);
    if(cJ_Item == NULL) goto end;
    cJSON_AddItemToObject(cJ_chip_info, "revision", cJ_Item);
    
    cJ_print = cJSON_Print(cJ_root);
    if(cJ_print == NULL){

    }
end:
    cJSON_Delete(cJ_root);
    cJSON_Delete(cJ_chip_info);
    cJSON_Delete(cJ_Item);
    return cJ_print;
}


static void mqtt_event_recv_task(void* parm){
    esp_mqtt_event_handle_t event;
    BaseType_t xReturn = pdFAIL;
    size_t path_prefix_len = 0;
    char topic_path[32];
    
    path_prefix_len = strlen(MQTT_TOPIC_BASE_PATH) + (18 - 1);

    while(true){
        xReturn = xQueueReceive(q_mqtt_client_event, &event, portMAX_DELAY); //接收 MQTT Event 消息
        if(pdPASS == xReturn){
            switch(event->event_id){
            case MQTT_EVENT_DATA:
            event->data[event->data_len] = '\0';
            event->topic[event->topic_len] = '\0';
            // printf("Task Receive TOPIC=%.*s  | DATA=%.*s | msg_id=%d\r\n", event->topic_len, event->topic, event->data_len, event->data, event->msg_id);
            printf("Task Receive TOPIC(len=%d)=%s  | DATA(len=%d)=%s | msg_id=%d\r\n", event->topic_len, event->topic, event->data_len, event->data, event->msg_id);
            memset(topic_path, 0, sizeof(topic_path));
            strncpy(topic_path, &(event->topic[path_prefix_len]), (strlen(event->topic) - path_prefix_len));
            ESP_LOGI(TAG, "Topic:%s", topic_path);
            if(strcmp(MQTT_TOPIC_CONTROL_0, (const char*)topic_path) == 0){
                if(strcmp("0", (const char*)event->data) == 0){
                    ESP_LOGI(TAG, "Close Control-0");
                    gpio_set_level(GPIO_4, 0);
                }else if(strcmp("1", (const char*)event->data) == 0){
                    ESP_LOGI(TAG, "Open Control-0");
                    gpio_set_level(GPIO_4, 1);
                }
            }
            if(strcmp(MQTT_TOPIC_CONTROL_1, (const char*)topic_path) == 0){
                if(strcmp("0", (const char*)event->data) == 0){
                    ESP_LOGI(TAG, "Close Control-1");
                    gpio_set_level(GPIO_5, 0);
                }else if(strcmp("1", (const char*)event->data) == 0){
                    ESP_LOGI(TAG, "Open Control-1");
                    gpio_set_level(GPIO_5, 1);
                }
            }
            break;
            default:

            break;
            }         
        }
    }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    // mqtt_client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("## MQTT ##", "MQTT_EVENT_CONNECTED");
            if(q_mqtt_client_event == NULL) q_mqtt_client_event = xQueueCreate(Q_MQTT_CLIENT_SIZE, sizeof(esp_mqtt_event_handle_t));// 创建 mqtt client 句柄消息队列
            
            if(mqtt_event_recv_task_handle == NULL){
                BaseType_t xReturn = pdFAIL;
                xReturn = xTaskCreate(mqtt_event_recv_task, "mqtt_event_recv_task", 1024, NULL, 7, &mqtt_event_recv_task_handle);  //创建 MQTT Event 处理任务
                if(xReturn != pdPASS){
                    ESP_LOGI("## MQTT ##", "\"mqtt_event_recv_task\" Create Failure!");
                }
            }
            size_t topic_path_len = strlen(MQTT_TOPIC_BASE_PATH) + strlen(MQTT_TOPIC_DEVICE_INFO) + sizeof(e_chip_info.base_mac) + 2;
            char* topic_path = malloc(topic_path_len);
            if(topic_path != NULL){
                strcpy(topic_path, MQTT_TOPIC_BASE_PATH);
                strcat(topic_path, e_chip_info.base_mac);
                strcat(topic_path, MQTT_TOPIC_DEVICE_INFO);                     // 创建 topic path
                char* cJ_info = cJ_create_chip_info(&e_chip_info);              // 创建chip info 数据
                // msg_id = esp_mqtt_client_subscribe(client, (const char*)topic_path, 0);   //发布 chip info 信息
                // ESP_LOGI("## MQTT ##", "sent subscribe successful, msg_id=%d", msg_id);
                msg_id = esp_mqtt_client_publish(client, (const char*)topic_path, (const char*)cJ_info, 0, 1, 0);   //发布 chip info 信息
                ESP_LOGI("## MQTT ##", "sent publish successful, msg_id=%d", msg_id);
                free(cJ_info);
            }
            free(topic_path);

            topic_path_len = strlen(MQTT_TOPIC_BASE_PATH) + strlen(MQTT_TOPIC_CONTROL_0) + sizeof(e_chip_info.base_mac) + 2;
            topic_path = malloc(topic_path_len);
            if(topic_path != NULL){
                strcpy(topic_path, MQTT_TOPIC_BASE_PATH);
                strcat(topic_path, e_chip_info.base_mac);
                strcat(topic_path, MQTT_TOPIC_CONTROL_0);                     // 创建 topic path
                msg_id = esp_mqtt_client_subscribe(client, (const char*)topic_path, 1);
                ESP_LOGI("## MQTT ##", "sent subscribe successful, msg_id=%d", msg_id);
            }
            free(topic_path);

            topic_path_len = strlen(MQTT_TOPIC_BASE_PATH) + strlen(MQTT_TOPIC_CONTROL_1) + sizeof(e_chip_info.base_mac) + 2;
            topic_path = malloc(topic_path_len);
            if(topic_path != NULL){
                strcpy(topic_path, MQTT_TOPIC_BASE_PATH);
                strcat(topic_path, e_chip_info.base_mac);
                strcat(topic_path, MQTT_TOPIC_CONTROL_1);                     // 创建 topic path
                msg_id = esp_mqtt_client_subscribe(client, (const char*)topic_path, 1);
                ESP_LOGI("## MQTT ##", "sent subscribe successful, msg_id=%d", msg_id);
            }
            free(topic_path);           
            
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connecct_retry -= 1; //重连失败一次, 重连次数减一
            if(!mqtt_connecct_retry){ //达到最大重连次数, 重启MQTT客户端
                mqtt_connecct_retry = MQTT_CONNECT_RETRY;         //重置重连次数
                esp_mqtt_client_stop(mqtt_client);                //停止MQTT Client
                mqtt_client = NULL;      
                ESP_LOGI("## MQTT ##", "MQTT Client Restarting.......");                         
                xEventGroupSetBits(mqtt_event_group, MQTT_CLIENT_START_BIT);
            }
            if(mqtt_event_recv_task_handle != NULL){
                vTaskDelete(mqtt_event_recv_task_handle);
                mqtt_event_recv_task_handle = NULL;
            }
            if(q_mqtt_client_event != NULL){
                vQueueDelete(q_mqtt_client_event);
                q_mqtt_client_event = NULL;
            }
            ESP_LOGI("## MQTT ##", "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            // ESP_LOGI("## MQTT ##", "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            // // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            // // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            // ESP_LOGI("## MQTT ##", "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            // ESP_LOGI("## MQTT ##", "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            // ESP_LOGI("## MQTT ##", "MQTT_EVENT_DATA");
            if(q_mqtt_client_event != NULL){
                uint8_t ret = 4;
                BaseType_t xReturn = pdFAIL;
                do{
                    ret -= 1;
                    xReturn = xQueueSend(q_mqtt_client_event, &event, 0);
                }while(xReturn != pdPASS && ret != 0);
                if(xReturn != pdPASS) ESP_LOGI("## MQTT ##", "Queue Send Failure!");
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI("## MQTT ##", "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

static void mqtt_connect_to_broker_task(void* parm){
    // esp_mqtt_client_handle_t client = NULL;
    esp_err_t err;

    while(true){
        if(mqtt_client != NULL){
            esp_mqtt_client_stop(mqtt_client);
            mqtt_client = NULL;
        }
        memset(c_mqtt_cfg.host, 0, sizeof(c_mqtt_cfg.host));
        memset(c_mqtt_cfg.uri, 0, sizeof(c_mqtt_cfg.uri));
        memset(c_mqtt_cfg.client_id, 0, sizeof(c_mqtt_cfg.client_id));
        memset(c_mqtt_cfg.username, 0, sizeof(c_mqtt_cfg.username));
        memset(c_mqtt_cfg.password, 0, sizeof(c_mqtt_cfg.password));
        // err = read_mqtt_config(&c_mqtt_cfg);
        size_t cfg_size = sizeof(c_mqtt_config_t);
        err = read_data_from_nvs(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_KEY, &c_mqtt_cfg, cfg_size);
        if(ESP_OK == err){
            // ESP_LOGI("## MQTT ##", "Host:%s Port:%d Username:%s", c_mqtt_cfg.host, c_mqtt_cfg.port, c_mqtt_cfg.username);
            esp_mqtt_client_config_t mqtt_cfg = {
                .host = c_mqtt_cfg.host,
                .event_handle = mqtt_event_handler,
                .port = c_mqtt_cfg.port,
                .client_id = c_mqtt_cfg.client_id,
                .username = c_mqtt_cfg.username,
                // .user_context = (void *)your_context
            };
            ESP_LOGI("## MQTT ##", "Host:%s Port:%d Username:%s", mqtt_cfg.host, mqtt_cfg.port, mqtt_cfg.username);
            // ESP_LOGI("## MQTT ##", "MQTT Client connecting to broker!");
            mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
            // mqtt_client = client;
            esp_mqtt_client_start(mqtt_client);
        }else{
            ESP_LOGI("## MQTT ##", "Read MQTT Config Failure ERROR Code: %d", err);
        }
        xEventGroupWaitBits(mqtt_event_group, MQTT_CLIENT_START_BIT, true, true, portMAX_DELAY);
    }
}

/**
 * 发送请求文件 
 * 
 **/
static esp_err_t http_send_request_file(httpd_req_t* req, const char* file_path){
    esp_err_t xERR = ESP_FAIL;
    int err = 0, ret = 0;
    size_t fs = 0;
    char* resp_str = NULL;
    size_t resp_len = 0;

    get_file_size((const char*)file_path, &fs, &err);
    // ESP_LOGI(TAG, "request file size:%d", fs);
    if(0 == err){
        // xERR = httpd_resp_set_status(req, HTTPD_200);
        // if(ESP_OK != xERR) return xERR;
        if(fs > 512){
            resp_len = 512;
        }else{
            resp_len = fs;
        }
        resp_str = malloc(resp_len);
        if(resp_str == NULL) return ESP_ERR_NO_MEM;
        if(resp_str != NULL){
            FILE* file = fopen((const char*)file_path, "r");
            if(file == NULL){xERR = (esp_err_t)*__errno(); return xERR;}
            ret = fs;
            size_t read_size;
            while(ret > 0){                     //读取文件并发送
                memset(resp_str, 0, resp_len);
                read_size = fread(resp_str, 1, resp_len, file);
                ret = ret - read_size * sizeof(char);
                if(read_size < resp_len){
                    if(feof(file)){  //文件末尾标识符
                        resp_str[read_size * sizeof(char)] = '\0';
                        xERR = httpd_resp_send_chunk(req, (const char*)resp_str, read_size * sizeof(char));
                        if(xERR != ESP_OK) return xERR;
                        break;
                    }
                }
                xERR = httpd_resp_send_chunk(req, (const char*)resp_str, resp_len);
                if(ESP_OK != xERR) return xERR;
                // printf("%d\r\n", read_size);
            }
            fclose(file);
        }
        free(resp_str);
    }else{
        // xERR = httpd_resp_set_status(req, HTTPD_404);
        // if(ESP_OK != xERR) return xERR;
    }
    
    // const char* resp_str = html[i];
    xERR = httpd_resp_send_chunk(req, NULL, 0);
    if(ESP_OK != xERR) return xERR;
    return ESP_OK;
}

/* An HTTP GET handler */
esp_err_t http_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */

    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            // ESP_LOGI(TAG, "Found header => Host: %s", buf);
            // ESP_LOGI(TAG, "Request Path: %s", req->uri);
        }
        free(buf);
    }
    buf_len = httpd_req_get_hdr_value_len(req, "Connection") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Connection", buf, buf_len) == ESP_OK) {
            // ESP_LOGI(TAG, "Found header => Connection: %s", buf);
        }
        free(buf);
    }
    buf_len = httpd_req_get_hdr_value_len(req, "Accept") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Accept", buf, buf_len) == ESP_OK) {
            // ESP_LOGI(TAG, "Found header => Accept: %s", buf);
            if(strncmp("text/css", (const char*)buf, strlen("text/css")) == 0){
                httpd_resp_set_type(req, "text/css");
            }else if(strncmp("text/html", (const char*)buf, strlen("text/html")) == 0){
                httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
            }
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
            }
        }
        free(buf);
    }

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Cache-Control", "public,no-cache,max-age=30,s-maxage=30");
    httpd_resp_set_hdr(req, "Connection", "Keep-Alive");
    httpd_resp_set_hdr(req, "Age", "30");
    /* Send response with custom headers and body set as the
     * string passed in user context*/
    
    char* request_url = NULL;
    if(strcmp("/", req->uri) == 0){
        request_url = malloc(strlen("/index.html") + strlen(HTTPD_WEB_ROOT) + 1); //index.html路径
        if(request_url == NULL) return ESP_ERR_NO_MEM;
        strcpy(request_url, HTTPD_WEB_ROOT);
        strcat(request_url, "/index.html");
        // if(read_wifi_config(&sta_wifi_cfg) == ESP_OK)
            // ESP_LOGI(TAG, "Read WiFi Config: SSID: %s, PASSWD: %s", sta_wifi_cfg.ssid, sta_wifi_cfg.password);
    }else{
        request_url = malloc(strlen(req->uri) + strlen(HTTPD_WEB_ROOT) + 1); // 文件路径
        if(request_url == NULL) return ESP_ERR_NO_MEM;
        strcpy(request_url, HTTPD_WEB_ROOT);
        strcat(request_url, req->uri);
    }
    
    http_send_request_file(req, request_url);   //发送请求文件

    free(request_url);
    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        // ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

httpd_uri_t html_index = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};
httpd_uri_t html_wifi_config = {
    .uri       = "/wifi_config.html",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};
httpd_uri_t html_mqtt_config = {
    .uri       = "/mqtt_config.html",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};
httpd_uri_t html_css = {
    .uri       = "/css/style.css",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};
httpd_uri_t html_mqtt_topic = {
    .uri       = "/mqtt_topic.html",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};


/* An HTTP POST handler */
esp_err_t http_post_handler(httpd_req_t *req)
{
    char* buf = NULL;
    size_t buf_len = req->content_len;
    uint8_t save_status = 0;
    int ret = 0;
    esp_err_t err;
    if(buf_len > 0){
        buf = malloc(buf_len);
        ret = httpd_req_recv(req, buf, buf_len);
        buf[buf_len] = '\0';
        if(ret <= 0){
            if(ret == HTTPD_SOCK_ERR_TIMEOUT){
            }
            free(buf);
            return ESP_FAIL;
        }
        // char* request_url = malloc(strlen(req->uri));
        // strcpy(request_url, req->uri);
        if(strcmp("/", req->uri) == 0){
            
        }else if(strcmp("/wifi_save", req->uri) == 0){
            get_wifi_config_from_url(buf, &sta_wifi_cfg);
            ESP_LOGI("## HTTPD Config WiFi ##", "%s, %s", sta_wifi_cfg.ssid, sta_wifi_cfg.password);
            // err = save_wifi_config(&sta_wifi_cfg);
            size_t cfg_size = sizeof(wifi_config_t);
            err = save_data_to_nvs(WIFI_CONFIG_NAMESPACE, WIFI_CONFIG_KEY, &sta_wifi_cfg, cfg_size);
            if(ESP_OK == err){
                save_status |= 0x01;
            }else{
                save_status &= (~0x01);
            }
        }else if(strcmp("/mqtt_save", req->uri) == 0){
            get_mqtt_config_from_url(buf, &c_mqtt_cfg);
            // err = save_mqtt_config(&c_mqtt_cfg);
            size_t cfg_size = sizeof(c_mqtt_config_t);
            err = save_data_to_nvs(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_KEY, &c_mqtt_cfg, cfg_size);
            if(ESP_OK == err){
                save_status |= 0x02;
                ESP_LOGI("## HTTPD Config MQTT ##", "MQTT Config Save Success!");
            }else{
                ESP_LOGI("## HTTPD Config MQTT ##", "MQTT Config Save Failure ERROR Code:%d", err);
                save_status &= (~0x02);
            }
        }else if(strcmp("/mqtt_topic_cfg", req->uri) == 0){
             
        }else{
            httpd_resp_send_404(req);
        }
        // free(request_url);

        if((save_status & 0x01) || (save_status & 0x02)){ // config save successed
            httpd_resp_set_status(req, HTTPD_200);
            httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
            http_send_request_file(req, "/c/www/200.html");
        }else{
            httpd_resp_set_status(req, HTTPD_500);
            httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
            http_send_request_file(req, "/c/www/500.html");
        }

        // httpd_resp_send(req, resp_buf, strlen(resp_buf));
        // httpd_resp_send_chunk(req, resp_buf, strlen(resp_buf));
        // ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        // ESP_LOGI(TAG, "%.*s", buf_len, buf);
        // ESP_LOGI(TAG, "====================================");
        free(buf);
        if(save_status & 0x02){
            xEventGroupSetBits(mqtt_event_group, MQTT_CLIENT_START_BIT);  
        }
    }
    // End response
    // httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t html_wifi_save = {
    .uri       = "/wifi_save",
    .method    = HTTP_POST,
    .handler   = http_post_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};
httpd_uri_t html_mqtt_save = {
    .uri       = "/mqtt_save",
    .method    = HTTP_POST,
    .handler   = http_post_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};
httpd_uri_t html_mqtt_topic_cfg = {
    .uri       = "/mqtt_topic_cfg",
    .method    = HTTP_POST,
    .handler   = http_post_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &html_index);
        httpd_register_uri_handler(server, &html_wifi_config);
        httpd_register_uri_handler(server, &html_wifi_save);
        httpd_register_uri_handler(server, &html_mqtt_config);
        httpd_register_uri_handler(server, &html_mqtt_save);
        httpd_register_uri_handler(server, &html_mqtt_topic);
        httpd_register_uri_handler(server, &html_css);

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static void http_req_handle_task(void* parm){
    httpd_req_t* req = NULL;

    BaseType_t xReturn = pdPASS;

    while(true){
        xReturn = xQueueReceive(q_http_req, &req, portMAX_DELAY);
        if(xReturn == pdPASS){
            ESP_LOGI("## HTTPD ##", "http handle task recieve req path:%s", req->uri);
            switch(req->method)
            {
                case HTTP_GET:
                    
                break;
                case HTTP_POST:

                break;
            }
        }else{
            ESP_LOGI("## HTTPD ##", "http handle task recieve failure");
        }
    }
}

void http_server_task(void* parm){
    
    EventBits_t uBit;

    while(true){
        uBit = xEventGroupWaitBits(http_event_group, HTTP_SERVER_START_BIT | HTTP_SERVER_STOP_BIT | WIFI_DISCONNECT_BIT, true, false, portMAX_DELAY);
        if(uBit == HTTP_SERVER_START_BIT){
            if(http_server_handle == NULL){
                http_server_handle = start_webserver();
                // BaseType_t xReturn = pdFAIL;
                // if(q_http_req == NULL){
                //     q_http_req = xQueueCreate(Q_HTTP_REQ_SIZE, sizeof(httpd_req_t));
                // }
                // if(http_req_handle_task_handle == NULL){
                //     xReturn = xTaskCreate(http_req_handle_task, "htpp_req_handle_task", 2048, NULL, 8, &http_req_handle_task_handle);
                //     if(xReturn != pdPASS){
                //         ESP_LOGI(TAG, "create http req receive task failure");
                //     }
                // }
            }
        }else if(HTTP_SERVER_STOP_BIT == uBit || WIFI_DISCONNECT_BIT == uBit){
            if(http_server_handle != NULL){
                stop_webserver(http_server_handle);  
                http_server_handle = NULL;
                // vTaskSuspend(NULL);   // 挂起任务
                // if(q_http_req != NULL){
                //     vQueueDelete(q_http_req);
                //     q_http_req = NULL;
                // }
            }
        }
    }
}


void wifi_reconnect_task(void* parm){
     
    while (true){
         /* code */
        xEventGroupWaitBits(wifi_event_group, WIFI_DISCONNECT_BIT, true, true, portMAX_DELAY);
        vTaskDelay(1000);
        ESP_LOGI(TAG, "ESP Reconnecting to AP.....");
        ESP_ERROR_CHECK(esp_wifi_start());
        
    }
     
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
        if(wifi_reconnect_task_handle != NULL){
            vTaskDelete((wifi_reconnect_task_handle));
            wifi_reconnect_task_handle = NULL;
        }
        xEventGroupClearBits(wifi_event_group, WIFI_DISCONNECT_BIT);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        ESP_LOGI(TAG, "Got IP: '%s'",
                ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));

        // if(http_server_task_handle != NULL) vTaskResume(http_server_task_handle);
        if(NULL == mqtt_connect_to_broker_task_handle){
            xTaskCreate(mqtt_connect_to_broker_task, "mqtt_connect_to_broker_task", 1024, NULL, 6, &mqtt_connect_to_broker_task_handle);
        }
        xEventGroupSetBits(http_event_group, HTTP_SERVER_START_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
        }
        if((--wifi_connect_retry) != 0){
            ESP_ERROR_CHECK(esp_wifi_connect());
        }else{                                //达到最大重连次数, 重启wifi
           if(mqtt_connect_to_broker_task_handle != NULL){
               vTaskDelete(mqtt_connect_to_broker_task_handle);
               mqtt_connect_to_broker_task_handle = NULL;
           }
            wifi_connect_retry = WIFI_CONNECT_RETRY;
            ESP_ERROR_CHECK(esp_wifi_stop());
            ESP_LOGI(TAG, "Restart Wifi...............");
            xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECT_BIT);
        }
        if(wifi_reconnect_task_handle == NULL){
            xTaskCreate(wifi_reconnect_task, "wifi_reconnect_task", 1024, NULL, 5, &wifi_reconnect_task_handle);
        }
        break;
    case SYSTEM_EVENT_AP_START:
        xEventGroupSetBits(http_event_group, HTTP_SERVER_START_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void *arg)
{
    esp_err_t err;

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, arg));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    
    memset(sta_wifi_cfg.ssid, 0, sizeof(sta_wifi_cfg.ssid));
    memset(sta_wifi_cfg.password, 0, sizeof(sta_wifi_cfg.password));
    
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
                .max_connection = 10,
            },
        };
        if(strlen((const char*)ap_config.ap.password) == 0) ap_config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MODEM));  //省电模式
}


static void spiffs_initialise(void){
    esp_vfs_spiffs_conf_t conf = {
      .base_path = SPIFFS_BASE_PATH,
      .partition_label = SPIFFS_PARTITION_LABEL,
      .max_files = 5,
      .format_if_mount_failed = false
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("## SPIFFS ##", "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE("## SPIFFS ##", "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE("## SPIFFS ##", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(SPIFFS_PARTITION_LABEL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE("## SPIFFS ##", "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI("## SPIFFS ##", "Partition size: total: %d, used: %d", total, used);
    }
    size_t fs;
    int err = 0;
    get_file_size("/c/www/index.html", &fs, &err);
    // ESP_LOGI("## SPIFFS ##", "test.txt file size:%d, err:%d", fs, err);
    if(err != 0){

    }else{

    char* html;
    size_t len = 0;
    int remain = fs;
    if(fs > 512){
        len = 512;
    }else{
        len = fs;
    }
    html = malloc(len);
    if(html != NULL){
        FILE * index = NULL;
        index = fopen("/c/www/index.html", "r");
        if(index == NULL) return;
        // ESP_LOGI("## SPIFFS ##", "file open");
        remain = fs;
        size_t read_size;
        while (remain > 0){
            memset(html, 0, len);
            read_size = fread(html, 1, len, index);
            remain = remain - read_size * sizeof(char);
            if(read_size < len){
                if(feof(index)){
                    html[read_size*sizeof(char)] = '\0';
                    // printf("%s", html);
                    break;
                }
            }
            html[read_size*sizeof(char)] = '\0';
            // printf("%s", html);
        }
        // ESP_LOGI("## SPIFFS ##", "file read");
        fclose(index);
        free(html);
    }else{
        free(html);
    }
    }
}
static void heap_info(void){
    uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    ESP_LOGI("## HEAP ##", "heap free(32):%dbyte", heap_free);
    heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI("## HEAP ##", "heap free(8):%dbyte", heap_free);
}


static void nvs_initialise(void){
     esp_err_t err = nvs_flash_init();
     if(err == ESP_ERR_NVS_NO_FREE_PAGES){
         ESP_ERROR_CHECK(nvs_flash_erase());
         err = nvs_flash_init();
     }
     ESP_ERROR_CHECK(err);
}


#define RUN_TIME_NAMESPACE            "run_time"
#define RUN_TIME_KEY                  "run_time"
#define POWER_ON_DOWN_TIME            3

static esp_err_t reset_nvs_all(void){
    nvs_handle n_nvs_handle;
    esp_err_t err;

    err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_all(n_nvs_handle);
    if(err != ESP_OK) return err;
    err = nvs_commit(n_nvs_handle);
    if(err != ESP_OK) return err;
    nvs_close(n_nvs_handle);

    err = nvs_open(MQTT_CONFIG_NAMESPACE, NVS_READWRITE, &n_nvs_handle);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_all(n_nvs_handle);
    if(err != ESP_OK) return err;
    err = nvs_commit(n_nvs_handle);
    if(err != ESP_OK) return err;
    nvs_close(n_nvs_handle);

    return ESP_OK;
}

static esp_err_t add_run_counte(int8_t r_counte){
    nvs_handle n_nvs_handle;
    esp_err_t err;

    err = nvs_open(RUN_TIME_NAMESPACE, NVS_READWRITE, &n_nvs_handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;

    err = nvs_set_i8(n_nvs_handle, RUN_TIME_KEY, r_counte);
    if(err != ESP_OK) return err;

    err = nvs_commit(n_nvs_handle);
    if(err != ESP_OK) return err;

    // Close
    nvs_close(n_nvs_handle);
    return ESP_OK;
}

static esp_err_t get_run_counte(int8_t* r_counte){
    nvs_handle n_nvs_handle;
    esp_err_t err;
    int8_t rc = 0;

    err = nvs_open(RUN_TIME_NAMESPACE, NVS_READWRITE, &n_nvs_handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;

    err = nvs_get_i8(n_nvs_handle, RUN_TIME_KEY, &rc);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND){
        *r_counte = -1;
        return err;
    }
    *r_counte = rc;
    // Close
    nvs_close(n_nvs_handle);
    return ESP_OK;
}

static void record_run_time_task(void* parm){
    uint32_t run_time;
    int8_t r_counte;
    while(true){
        if(get_run_counte(&r_counte) == ESP_OK){
            ESP_LOGI(TAG, "r_counte: %d", r_counte);
            if(r_counte == -1){
                r_counte = 0; 
                add_run_counte(r_counte);
            }else if(r_counte == POWER_ON_DOWN_TIME){    //快速上电断电 'POWER_ON_DOWN_TIME' 次, 重置设置, 并重启设备
                reset_nvs_all();
                ESP_LOGI(TAG, "Reset setting.........");
                r_counte = 0;
                add_run_counte(r_counte);
                ESP_LOGI(TAG, "Restart Device........");
                esp_restart();
            }else{           //小于3
                run_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "run_time: %d", run_time);
                if(run_time < 3000){
                    r_counte += 1;
                    add_run_counte(r_counte);
                }else{
                    r_counte = 0;
                    add_run_counte(r_counte);
                    ESP_LOGI(TAG, "Delete \"record_run_time_task\".........");
                    vTaskDelete(NULL);   //删除任务
                }
            }
            vTaskDelay(3000 / portTICK_PERIOD_MS);
        }
    }
}

void app_main()
{   
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    phripherals_initialise();    //初始化外设

    // 获取BASE MAC地址
    uint8_t efuse_base_mac[6];
    memset(efuse_base_mac, 0, 6);
    esp_efuse_mac_get_default(efuse_base_mac);
    sprintf(e_chip_info.base_mac, "%x-%x-%x-%x-%x-%x", efuse_base_mac[0], efuse_base_mac[1], efuse_base_mac[2], efuse_base_mac[3], efuse_base_mac[4], efuse_base_mac[5]);
    ESP_LOGI(TAG, "EFUSE Base MAC: %s", e_chip_info.base_mac);
    
    esp_chip_info(&(e_chip_info.esp_info));   //获取chip info

    e_chip_info.flash_size = system_get_flash_size_map();
    
    adc_read(&e_chip_info.voltage);

    ESP_ERROR_CHECK(adc_deinit());

    char* cJ_string = NULL;
    cJ_string = cJ_create_chip_info(&e_chip_info);
    printf("%s\r\n", cJ_string);
    free(cJ_string);

    static httpd_handle_t server = NULL;
    esp_event_group = xEventGroupCreate();
    // vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay 1s
    nvs_initialise();

    xTaskCreate(record_run_time_task, "record_run_time_task", 1024, NULL, 3, NULL);

    heap_info();
    spiffs_initialise();

    wifi_event_group = xEventGroupCreate();
    http_event_group = xEventGroupCreate();
    mqtt_event_group = xEventGroupCreate();

    xTaskCreate(http_server_task, "htpp_server_task", 1024, NULL, 4, &http_server_task_handle);

    initialise_wifi(&server);
    // vTaskDelay(1000);
    
}