#ifndef _MAIN_H_
#define _MAIN_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <stdio.h>
#include <stdbool.h>

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

#include "fota.h"

// #include "sign_api.h"  // Aliyun MQTT Sign


#define NVS_DATA    0            // 数据保存格式

#define GPIO_0x00                                     GPIO_NUM_14
#define GPIO_0x01                                     GPIO_NUM_16

void restart_chip(void);

void set_led_blink_freq(int8_t freq);

size_t get_file_size(const char * file, size_t* file_size, int* err);
void get_config_from_url(const char* config_data, const char* config_key, uint8_t* value, uint8_t value_len);
int string2Int(char* str, int* num);
int int2String(char* des, size_t dl, int num);
esp_err_t save_data_to_nvs(const char* namespace, const char* key, void* data, size_t data_size);
esp_err_t read_data_from_nvs(const char* namespace, const char* key, void* data, size_t data_size);
esp_err_t reset_nvs_all(void);

void get_fota_config_from_url(char* buf, fota_config_t* fota_cfg);




#define WIFI_CONFIG_NAMESPACE                        "wifi_config"
#define WIFI_CONFIG_KEY                              "wifi_config"


void wifi_reconnect_task(void* parm);
void create_wifi_reconnect_task(void * const pvParameters, UBaseType_t uxPriority);
void delete_wifi_reconnect_task(void);

void get_wifi_config_from_url(char* buf, wifi_config_t* sta_wifi_cfg);
esp_err_t save_wifi_config_to_nvs(wifi_config_t* wifi_config);
esp_err_t read_wifi_config_from_nvs(wifi_config_t* wifi_config);
void initialise_wifi(void *arg);

void wifi_start_scan(void);
void create_wifi_scan_task(void * const pvParameters, UBaseType_t uxPriority);
void delete_wifi_scan_task(void);

typedef struct {
    esp_chip_info_t esp_info;
    char base_mac[18];
    uint8_t flash_size;
    uint16_t voltage; 
}e_chip_info_t;

extern e_chip_info_t e_chip_info;

// #define MQTT_BROKER_URL "mqtt.flespi.io"
// #define MQTT_BROKER_USERNAME "vSvy3tP5RfCcZTqigojCUmPAAoRSTqAe9TC0cfTRklU738f9mpz4zcsYq9G9p4bm"
#define MQTT_TOPIC_BASE_PATH                   "/topic/esp8266/esp-12f/mac/"
#define MQTT_TOPIC_DEVICE_INFO                 "/chip_info"
// #define MQTT_TOPIC_CONTROL_0                   "/control0"
// #define MQTT_TOPIC_CONTROL_1                   "/control1"

#define MQTT_CONFIG_NAMESPACE                  "mqtt_config"
#define MQTT_CONFIG_KEY                        "mqtt_config"
#define MQTT_TOPIC_NAMESPACE                   "mqtt_topic"
#define MQTT_TOPIC_KEY                         "matt_topic"


typedef struct {
    char host[64];
    uint32_t port;
    char uri[64];
    char client_id[16];
    char username[128];
    char password[64];
}c_mqtt_config_t;

extern c_mqtt_config_t c_mqtt_cfg;

typedef struct {
    char topic[128];
    uint8_t type;   /* Subscribe: 0        Publish: 1  */
    uint8_t qos;    /* Qos0:0, Qos1:1, Qos2:2 */
}c_mqtt_topic_t;

extern EventGroupHandle_t mqtt_event_group;
extern const EventBits_t MQTT_CLIENT_START_BIT;
extern const EventBits_t MQTT_CLIENT_STOP_BIT;
extern const EventBits_t MQTT_CLIENT_CONNECTED_BIT;

void get_mqtt_config_from_url(char* buf, c_mqtt_config_t* mqtt_cfg);
void get_mqtt_topic_from_url(char* buf, c_mqtt_topic_t* c_mqtt_topic);
char* cJ_create_chip_info(e_chip_info_t* e_chip_info);

// void get_aliyun_dev_meta_info(char* buf, iotx_dev_meta_info_t* iotx_dev_meta_info, iotx_mqtt_region_types_t* region);

extern TaskHandle_t mqtt_event_recv_task_handle;
void mqtt_event_recv_task(void* parm);
void create_mqtt_event_recv_task(void * const pvParameters, UBaseType_t uxPriority);
void delete_mqtt_event_recv_task(void);

extern TaskHandle_t mqtt_connect_to_broker_task_handle;
void mqtt_connect_to_broker_task(void* parm);
void create_mqtt_connect_to_broker_task(void * const pvParameters, UBaseType_t uxPriority);
void delete_mqtt_connect_to_broker_task(void);


#define HTTPD_WEB_ROOT                             "/c/www"

esp_err_t http_send_request_file(httpd_req_t* req, const char* file_path);
void stop_webserver(httpd_handle_t server);
httpd_handle_t start_webserver(httpd_handle_t* server);

void create_http_req_handle_task(void * const pvParameters, UBaseType_t uxPriority);
void delete_http_req_handle_task(void);

void create_http_server_task(void * const pvParameters, UBaseType_t uxPriority);
void delete_http_server_task(void);
void notice_http_server_task(bool en);


// LEDC

#define LEDC_TEST_CH_NUM       (4)

extern int ledc_gpio_num[LEDC_TEST_CH_NUM];

void ledc_initialise(void);
void create_led_control_task(void * const pvParameters, UBaseType_t uxPriority);
void delete_led_control_task(void);


/**
 * @param gpio 管脚
 * @param br 亮度, 百分比 
*/
void set_led_brightness(gpio_num_t gpio, uint8_t br);


#ifdef __cplusplus
}
#endif

#endif