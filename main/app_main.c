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
static const EventBits_t ESP_RESTART_BIT = BIT31;


#define SPIFFS_BASE_PATH                           "/c"
#define SPIFFS_PARTITION_LABEL                     "storage"


e_chip_info_t e_chip_info;

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

static void get_cpu_task_run_info(void* parm){
    // char cpu_task_run_info[256];
    char* cpu_task_run_info;
    while(true){
        cpu_task_run_info = malloc(512);
        memset(cpu_task_run_info, 0, 512);
        vTaskList(cpu_task_run_info);
        printf("%s\r\n%d\r\n", cpu_task_run_info, strlen(cpu_task_run_info));
        memset(cpu_task_run_info, 0, 512);
        vTaskGetRunTimeStats(cpu_task_run_info);
        printf("%s\r\n%d\r\n", cpu_task_run_info, strlen(cpu_task_run_info));
        free(cpu_task_run_info);
        // uxTaskGetSystemState();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
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

    esp_event_group = xEventGroupCreate();

    nvs_initialise();

    xTaskCreate(record_run_time_task, "record_run_time_task", 1024, NULL, 3, NULL);

    heap_info();
    spiffs_initialise();

    mqtt_event_group = xEventGroupCreate();
    // fota_event_group = xEventGroupCreate();

    create_http_server_task(NULL, 4);

    xTaskCreate(create_fota_update_task, "create_fota_update_task", 1024, NULL, 6, NULL);
    // xTaskCreate(get_cpu_task_run_info, "get_cpu_task_run_info", 2048, NULL, 1, NULL);
    initialise_wifi(NULL);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

}