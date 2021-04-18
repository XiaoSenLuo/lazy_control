/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "main.h"

static const char *TAG="APP";


// #define WIFI_SSID "XL001_2.4G"
// #define WIFI_PASSWD "vHZ-sRk-s3p-czW"

static EventGroupHandle_t esp_event_group;
EventBits_t ESP_RESTART_BIT = BIT0;


#define SPIFFS_BASE_PATH                           "/c"
#define SPIFFS_PARTITION_LABEL                     "storage"


e_chip_info_t e_chip_info;

static TimerHandle_t led_status_timer_handle = NULL;
static int8_t led_blink_freq = -1;
static uint32_t timer_callback_count = 0;
static void timer_callback(void);

void set_led_blink_freq(int8_t freq){
    if(freq > 10) freq = 10;
    led_blink_freq = freq;
}

static void phripherals_initialise(void){
    gpio_config_t io_config;

    io_config.pin_bit_mask = ((1ULL << GPIO_NUM_4));
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


size_t get_file_size(const char * file, size_t* file_size, int* err){
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

void get_config_from_url(const char* config_data, const char* config_key, uint8_t* value, uint8_t value_len){
    const char* str = config_data;
    const char* chr = config_key;
    int str_len = strlen(str), chr_len = strlen(chr), i=0;
    char s[32];
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

int int2String(char* des, size_t dl, int num){
    snprintf(des, dl, "%d", num);
    return num;
}

esp_err_t save_data_to_nvs(const char* namespace, const char* key, void* data, size_t data_size){
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

esp_err_t read_data_from_nvs(const char* namespace, const char* key, void* data, size_t data_size){
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


void get_fota_config_from_url(char* buf, fota_config_t* fota_cfg){
    uint8_t* src_data;
    int len;

    memset(fota_cfg->ota_file, 0, sizeof(fota_cfg->ota_file));
    memset(fota_cfg->server_port, 0, sizeof(fota_cfg->server_port));
    memset(fota_cfg->server_host, 0, sizeof(fota_cfg->server_host));

    len = sizeof(fota_cfg->server_host);
    src_data = malloc(len);
    get_config_from_url(buf, "fota_server_host", (uint8_t*)fota_cfg->server_host, len);
    // urldecode((const char*)src_data, (len), fota_cfg->server_host, &len);
    free(src_data);

    len = sizeof(fota_cfg->server_port);
    src_data = malloc(len);
    get_config_from_url(buf, "fota_server_port", (uint8_t*)fota_cfg->server_port, len);
    free(src_data);

    len = sizeof(fota_cfg->ota_file);
    src_data = malloc(len*2);
    get_config_from_url(buf, "fota_file", src_data, len*2);
    urldecode((const char*)src_data, (len*2), fota_cfg->ota_file, &len);
    free(src_data);
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
    char* file_path = NULL;
    file_path = malloc(32);
    
    memset(file_path, 0, 32);
    strcpy(file_path, HTTPD_WEB_ROOT);
    strcat(file_path, "/chip_info.html");
    FILE* chip_info_file = NULL;
    chip_info_file = fopen(file_path, "w");
    if(chip_info_file != NULL){
        char* text = NULL;
        text = malloc(512);
        if(text != NULL){
            memset(text, 0, 512);
            // strcpy(text, "<!DOCTYPE HTML><html lang=\"en\"><head><title>ESP8266</title></head><body>");
            strcat(text, "<p style=\"color:#000;font-size:15px;letter-spacing:.5px;text-transform:none;\">");
            char* cJ_string = NULL;
            cJ_string = cJ_create_chip_info(&e_chip_info); 
            strcat(text, cJ_string);
            free(cJ_string);
            strcat(text, "</p>");
            // strcat(text, "</body></html>");
            fwrite(text, 1, strlen(text), chip_info_file);
        }
        free(text);
        fclose(chip_info_file);
    }
    free(file_path);
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

esp_err_t reset_nvs_all(void){
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

/**
 * 获取系统任务状态及堆栈使用情况
 * */
static void get_cpu_task_run_info(void* parm){
    // char cpu_task_run_info[256];
    char* cpu_task_run_info;
    while(true){
        cpu_task_run_info = malloc(512);
        memset(cpu_task_run_info, 0, 512);
        vTaskList(cpu_task_run_info);
        printf("-----------------------------------------------------------------------\r\n");
        printf("Task Name       Task Status   Task Priority    Reset Stack   Task Index\r\n");
        printf("%s\r\n%d\r\n", cpu_task_run_info, strlen(cpu_task_run_info));
        printf("-----------------------------------------------------------------------\r\n");
        memset(cpu_task_run_info, 0, 512);
        vTaskGetRunTimeStats(cpu_task_run_info);
        printf("-----------------------------------------------------------------------\r\n");
        printf("Task Name       Run Times     Used Present \r\n");
        printf("%s\r\n%d\r\n", cpu_task_run_info, strlen(cpu_task_run_info));
        printf("-----------------------------------------------------------------------\r\n");
        free(cpu_task_run_info);
        // uxTaskGetSystemState();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

/**
 * 通过 LED 显示系统状态
 * */
static void system_status_task(void* pvParameters){

}

static void timer_callback(void){
    timer_callback_count += 1;
    if(led_blink_freq < 0){
        gpio_set_level(GPIO_NUM_4, 0);  //常亮
    }else if(led_blink_freq == 0){
        gpio_set_level(GPIO_NUM_4, 1);
    }else{
        if(timer_callback_count % (10 /led_blink_freq) == 0){
            gpio_set_level(GPIO_NUM_4, 0);
        }else{
            gpio_set_level(GPIO_NUM_4, 1);
        }
    }
}

void restart_chip(void){
    if(esp_event_group != NULL){
        xEventGroupSetBits(esp_event_group, ESP_RESTART_BIT);
    }
}

static void restart_chip_task(void* pvParameters){
    EventBits_t xBit;
    while(true){
        xBit = xEventGroupWaitBits(esp_event_group, ESP_RESTART_BIT, true, false, portMAX_DELAY);
        if(xBit == ESP_RESTART_BIT){
            vTaskDelay(5000 / portTICK_RATE_MS);
            esp_restart();
        }

    }
}

void app_main()
{
    
    phripherals_initialise();    //初始化外设
    
    led_status_timer_handle = xTimerCreate("s_Timer", pdMS_TO_TICKS(100), pdTRUE, (void*)0x01, (TimerCallbackFunction_t)timer_callback);
    if(led_status_timer_handle != NULL){
        xTimerStart(led_status_timer_handle, 0);
    }else{
        ESP_LOGE(TAG, "software timer create failure! system stop!");
        while(1);
    }

    create_led_control_task(NULL, 7);   // 创建 LED 控制任务

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

    esp_event_group = xEventGroupCreate();

    nvs_initialise();
    
    xTaskCreate(restart_chip_task, "restart_chip_task", 1024+256, NULL, 8, NULL);         // 延时重启任务
    xTaskCreate(record_run_time_task, "record_run_time_task", 1024, NULL, 3, NULL);  // 实现快速上电三次重置系统
    
    heap_info();
    spiffs_initialise();

    mqtt_event_group = xEventGroupCreate();
    // fota_event_group = xEventGroupCreate();

    create_http_server_task(NULL, 4);  // 创建 http server 处理任务

    xTaskCreate(create_fota_update_task, "create_fota_update_task", 1024, NULL, 6, NULL);   // 创建 OTA 更新任务
    // initialise_wifi(NULL);     // 初始化 WIFI
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    create_wifi_scan_task(NULL, 5);    // 扫描wifi并连接

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // xTaskCreate(get_cpu_task_run_info, "get_cpu_task_run_info", 2048, NULL, 1, NULL);

    // set_led_brightness(13, 50);
}
