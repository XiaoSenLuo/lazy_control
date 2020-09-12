

#include "main.h"


/**
 * 连接到MQTT broker, LED闪烁频率 1Hz
 * 断开连接, LED闪烁频率 3Hz
 * 
 * */

#define Q_MQTT_CLIENT_SIZE    4
#define MQTT_CONNECT_RETRY    5


#define MQTT_TOPIC_GPIO            "gpio/"
#define MQTT_TOPIC_TIMER           "timer/"
#define MQTT_TOPIC_STATUS          "/status"

static const char *TAG="## MQTT ##";


esp_mqtt_client_handle_t mqtt_client = NULL;
EventGroupHandle_t mqtt_event_group = NULL;
const EventBits_t MQTT_CLIENT_START_BIT = BIT8;
const EventBits_t MQTT_CLIENT_STOP_BIT = BIT10;
const EventBits_t MQTT_CLIENT_CONNECTED_BIT = BIT11;
TaskHandle_t mqtt_connect_to_broker_task_handle = NULL;
TaskHandle_t mqtt_event_recv_task_handle = NULL;

static QueueHandle_t q_mqtt_client_event = NULL;
static uint8_t mqtt_connecct_retry = MQTT_CONNECT_RETRY;

void get_mqtt_config_from_url(char* buf, c_mqtt_config_t* mqtt_cfg){
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


void get_mqtt_topic_from_url(char* buf, c_mqtt_topic_t* c_mqtt_topic){
    uint8_t* src_data = NULL;
    int len;

    memset(c_mqtt_topic->topic, 0, sizeof(c_mqtt_topic->topic));

    len = sizeof(c_mqtt_topic->topic);
    src_data = malloc(len*2);
    get_config_from_url(buf, "mqtt_topic", src_data, len*2);
    urldecode((const char*)src_data, (len*2), c_mqtt_topic->topic, &len);
    free(src_data);

    len = 4;
    src_data = malloc(len);
    get_config_from_url(buf, "topic_type", src_data, len);
    string2Int((char*)src_data, (int*)&(c_mqtt_topic->type));
    memset(src_data, 0, len);
    get_config_from_url(buf, "topic_qos", src_data, len);
    string2Int((char*)src_data, (int*)&(c_mqtt_topic->qos));
    free(src_data);
}

void get_aliyun_dev_meta_info(char* buf, iotx_dev_meta_info_t* iotx_dev_meta_info, iotx_mqtt_region_types_t* region){
    // uint8_t* src_data = NULL;
    // int len = 0;
}


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
char* cJ_create_chip_info(e_chip_info_t* e_chip_info){
    
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

void mqtt_event_recv_task(void* parm){
    esp_mqtt_event_handle_t event;
    BaseType_t xReturn = pdFAIL;
    size_t path_prefix_len = 0;
    char topic_path[64];
    int brightness = 0;


    path_prefix_len = strlen(MQTT_TOPIC_BASE_PATH) + (18);

    while(true){
        if(q_mqtt_client_event != NULL){
            xReturn = xQueueReceive(q_mqtt_client_event, &event, portMAX_DELAY); //接收 MQTT Event 消息
            if(pdPASS == xReturn){
                switch(event->event_id){
                case MQTT_EVENT_DATA:
                event->data[event->data_len] = '\0';
                event->topic[event->topic_len] = '\0';
                printf("Task Receive TOPIC(len=%d):%s  | DATA(len=%d):%s | msg_id=%d\r\n", 
                                    event->topic_len, event->topic, event->data_len, event->data, event->msg_id);
                
                // 数据转换成JSON对象
                // {"mqtt_dashboard":{"brightness":50,"color":-10816178}}
                cJSON *json_root = NULL, *json_object = NULL, *json_brightness = NULL;
                if(event->data_len > 0) json_root = cJSON_Parse(event->data);
                else ESP_LOGW(TAG, "NO Data Recevie");
                if(json_root != NULL){
                    if(json_root->type == cJSON_Number){
                        brightness = (int)json_root->valuedouble;
                    }else{
                        json_object = cJSON_GetObjectItem(json_root, "mqtt_dashboard");
                        if(json_object != NULL){
                            json_brightness = cJSON_GetObjectItem(json_object, "brightness");
                            if(json_brightness->type == cJSON_Number){
                                brightness = (int)json_brightness->valuedouble;
                                // printf("brightness:%d\r\n", brightness);
                                cJSON_Delete(json_brightness);
                            }
                            cJSON_Delete(json_object);
                        }
                    }
                    cJSON_Delete(json_root);
                }else{
                    ESP_LOGE(TAG, "data format not a JSON string!");
                    cJSON_Delete(json_root);
                    break;
                }
                memset(topic_path, 0, sizeof(topic_path));
                strncpy(topic_path, &(event->topic[path_prefix_len]), (strlen(event->topic) - path_prefix_len));   
                // ESP_LOGI(TAG, "Topic:%s", topic_path);
                if(strncmp(MQTT_TOPIC_GPIO, (const char*)topic_path, strlen(MQTT_TOPIC_GPIO)) == 0){
                    int i_gpio_pin;
                    string2Int(&(topic_path[strlen(MQTT_TOPIC_GPIO)]), &i_gpio_pin);
                    ESP_LOGI(TAG, "GPIO:%d", i_gpio_pin);
                    switch(i_gpio_pin){
                        case GPIO_NUM_14:
                            set_led_brightness(GPIO_NUM_14, (uint8_t)brightness);
                        break;
                        case GPIO_NUM_16:
                            set_led_brightness(GPIO_NUM_16, (uint8_t)brightness);
                        break;
                        case GPIO_NUM_13:
                            set_led_brightness(GPIO_NUM_13, (uint8_t)brightness);
                        break;
                        case GPIO_NUM_12:
                            set_led_brightness(GPIO_NUM_12, (uint8_t)brightness);
                        break;
                    }
                    memset(topic_path, 0, sizeof(topic_path));
                    strcpy(topic_path, event->topic);
                    strcat(topic_path, MQTT_TOPIC_STATUS);
                    int msg_id;
                    msg_id = esp_mqtt_client_publish(event->client, (const char*)topic_path, event->data, 0, 1, 0); //
                    ESP_LOGI("## MQTT ##", "topic:%s  return successful, msg_id=%d", topic_path, msg_id);
                }
                break;
                default:

                break;
                }         
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
            set_led_blink_freq(1);
            ESP_LOGI("## MQTT ##", "MQTT_EVENT_CONNECTED");
            if(q_mqtt_client_event == NULL) q_mqtt_client_event = xQueueCreate(Q_MQTT_CLIENT_SIZE, sizeof(esp_mqtt_event_handle_t));// 创建 mqtt client 句柄消息队列
            
            // if(mqtt_event_recv_task_handle == NULL){
            //     BaseType_t xReturn = pdFAIL;
            //     xReturn = xTaskCreate(mqtt_event_recv_task, "mqtt_event_recv_task", 1024, NULL, 7, &mqtt_event_recv_task_handle);  //创建 MQTT Event 处理任务
            //     if(xReturn != pdPASS){
            //         ESP_LOGI("## MQTT ##", "\"mqtt_event_recv_task\" Create Failure!");
            //     }
            // }
            create_mqtt_event_recv_task(NULL, 7);
            char* base_topic_path = NULL;
            size_t tmp_len = strlen(MQTT_TOPIC_BASE_PATH) + sizeof(e_chip_info.base_mac) + 2;
            base_topic_path = malloc(tmp_len);
            memset(base_topic_path, 0, tmp_len);
            strcpy(base_topic_path, MQTT_TOPIC_BASE_PATH);
            strcat(base_topic_path, e_chip_info.base_mac);

            size_t topic_path_len = tmp_len + strlen(MQTT_TOPIC_DEVICE_INFO) + 2;
            char* topic_path = malloc(topic_path_len);
            if(topic_path != NULL) memset(topic_path, 0, topic_path_len);
            if(topic_path != NULL){
                strcpy(topic_path, base_topic_path);
                strcat(topic_path, MQTT_TOPIC_DEVICE_INFO);                     // 创建 topic path
                char* cJ_info = cJ_create_chip_info(&e_chip_info);              // 创建chip info 数据
                msg_id = esp_mqtt_client_publish(client, (const char*)topic_path, (const char*)cJ_info, 0, 1, 0);   //发布 chip info 信息
                ESP_LOGI("## MQTT ##", "topic:%s  sent subscribe successful, msg_id=%d", topic_path, msg_id);
                free(cJ_info);
            }
            free(topic_path);
            
            topic_path_len = tmp_len + strlen(MQTT_TOPIC_GPIO) + 2 + 2;
            topic_path = malloc(topic_path_len);
            if(topic_path != NULL) memset(topic_path, 0, topic_path_len);
            strcpy(topic_path, base_topic_path);  // /topic/esp8266/esp-12f/mac/..
            strcat(topic_path, "/");              // /topic/esp8266/esp-12f/mac/../
            strcat(topic_path, MQTT_TOPIC_GPIO);  // /topic/esp8266/esp-12f/mac/../gpio/
            tmp_len = strlen(topic_path);
            
            uint8_t gpio_index = 0;
            if(topic_path != NULL){
                for(gpio_index = 0; gpio_index < LEDC_TEST_CH_NUM; gpio_index++){
                    memset(&topic_path[tmp_len], 0, topic_path_len - tmp_len);
                    if(topic_path != NULL){  // 创建 topic path: /topic/esp8266/esp-12f/mac/../gpio/16                   
                        char gpio_pin[4] = {0};
                        int2String(gpio_pin, sizeof(gpio_pin), ledc_gpio_num[gpio_index]);
                        strcat(topic_path, gpio_pin);
                        msg_id = esp_mqtt_client_subscribe(client, (const char*)topic_path, 1);
                        ESP_LOGI("## MQTT ##", "topic:%s  sent subscribe successful, msg_id=%d", topic_path, msg_id);
                    }               
                }
            }
            free(topic_path);           
            free(base_topic_path);
            break;
        case MQTT_EVENT_DISCONNECTED:
            set_led_blink_freq(3);
            mqtt_connecct_retry -= 1; //重连失败一次, 重连次数减一
            if(!mqtt_connecct_retry){ //达到最大重连次数, 重启MQTT客户端
                mqtt_connecct_retry = MQTT_CONNECT_RETRY;         //重置重连次数
                // esp_mqtt_client_stop(mqtt_client);                //停止MQTT Client
                // mqtt_client = NULL;      
                ESP_LOGI("## MQTT ##", "MQTT Client Restarting.......");                         
                xEventGroupSetBits(mqtt_event_group, MQTT_CLIENT_START_BIT);
            }
            // if(mqtt_event_recv_task_handle != NULL){
            //     vTaskDelete(mqtt_event_recv_task_handle);
            //     mqtt_event_recv_task_handle = NULL;
            // }
            delete_mqtt_event_recv_task();
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
                while(ret){
                    ret -= 1;
                    xReturn = xQueueSend(q_mqtt_client_event, &event, 0);
                    if(xReturn != pdPASS){
                        ESP_LOGE("## MQTT ##", "Queue Send Failure!");
                    }else{
                        break;
                    }
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE("## MQTT ##", "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

void mqtt_connect_to_broker_task(void* parm){
    // esp_mqtt_client_handle_t client = NULL;
    esp_err_t err;
    c_mqtt_config_t c_mqtt_cfg;
    uint8_t restart_times = MQTT_CONNECT_RETRY;
    while(true){
        if(mqtt_client != NULL){
            esp_mqtt_client_stop(mqtt_client);
            mqtt_client = NULL;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
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
            restart_times -= 1;
            if(restart_times == 0){
                // vTaskSuspend(NULL);
                restart_times = MQTT_CONNECT_RETRY;
                esp_restart();  // 重启
            }
            esp_mqtt_client_start(mqtt_client);
        }else{
            ESP_LOGE("## MQTT ##", "Read MQTT Config Failure ERROR Code: %d", err);
        }
        xEventGroupWaitBits(mqtt_event_group, MQTT_CLIENT_START_BIT, true, true, portMAX_DELAY);
    }
}

void create_mqtt_connect_to_broker_task(void * const pvParameters, UBaseType_t uxPriority){
    BaseType_t xRetrun = pdFAIL;

    if(NULL == mqtt_connect_to_broker_task_handle){
        xRetrun = xTaskCreate(mqtt_connect_to_broker_task, "mqtt_connect_to_broker_task", 1536, pvParameters, uxPriority, &mqtt_connect_to_broker_task_handle);
        if(xRetrun != pdPASS){
            ESP_LOGI(TAG, "\"mqtt_connect_to_broker_task\" create failure!");
        }
    }
}

void delete_mqtt_connect_to_broker_task(void){
    if(mqtt_connect_to_broker_task_handle != NULL){
        vTaskDelete(mqtt_connect_to_broker_task_handle);
        mqtt_connect_to_broker_task_handle = NULL;
    }
}

void create_mqtt_event_recv_task(void * const pvParameters, UBaseType_t uxPriority){
    if(mqtt_event_recv_task_handle == NULL){
        BaseType_t xReturn = pdFAIL;
        xReturn = xTaskCreate(mqtt_event_recv_task, "mqtt_event_recv_task", 1024, pvParameters, uxPriority, &mqtt_event_recv_task_handle);  //创建 MQTT Event 处理任务
        if(xReturn != pdPASS){
            ESP_LOGI(TAG, "\"mqtt_event_recv_task\" create failure!");
        }
    }
}

void delete_mqtt_event_recv_task(void){
    if(mqtt_event_recv_task_handle != NULL){
        vTaskDelete(mqtt_event_recv_task_handle);
        mqtt_event_recv_task_handle = NULL;
    }
}


