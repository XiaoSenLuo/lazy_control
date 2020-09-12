

#include "main.h"


static const char *TAG="## HTTPD ##";

#define Q_HTTP_REQ_SIZE       4

static QueueHandle_t q_http_req = NULL;

static httpd_handle_t http_server_handle = NULL;
static EventGroupHandle_t http_event_group = NULL;

static TaskHandle_t http_server_task_handle = NULL;
static TaskHandle_t http_req_handle_task_handle = NULL;

const EventBits_t HTTP_SERVER_START_BIT = BIT0;
const EventBits_t HTTP_SERVER_STOP_BIT = BIT1;
const EventBits_t HTTP_WIFI_SAVE_CONFIG_BIT = BIT2;
const EventBits_t HTTP_MQTT_CLIENT_SAVE_CONFIG_BIT = BIT3;

static const char* html_page[] = {
    "/index.html",
    "/chip_info.html",
    "/wifi_config.html",
    "/mqtt_config.html",
    "/aliyun_mqtt_sign.html",
    "/update.html",
};

// static const char* css_page[] = {
//     "/css/style.css",
// };

/**
 * 发送请求文件 
 * 
 **/
esp_err_t http_send_request_file(httpd_req_t* req, const char* file_path){
    esp_err_t xERR = ESP_FAIL;
    int err = 0, ret = 0;
    size_t fs = 0;
    char* resp_str = NULL;
    size_t resp_len = 0;
    if(file_path != NULL && file_path[0] != '\0'){
        get_file_size((const char*)file_path, &fs, &err);
        // ESP_LOGI(TAG, "request file size:%d", fs);
        if(0 == err){   // 文件存在
            xERR = httpd_resp_set_status(req, HTTPD_200);
            if(ESP_OK != xERR) return xERR;
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
                xERR = httpd_resp_send_chunk(req, NULL, 0);
                if(ESP_OK != xERR) return xERR;
                fclose(file);
            }
            free(resp_str);
        }else if(err == ENOENT){ //文件不存在
            xERR = httpd_resp_set_status(req, HTTPD_404);
            if(ESP_OK != xERR) return xERR;
            xERR = httpd_resp_send(req, NULL, 0);
            if(ESP_OK != xERR) return xERR;            
        }
    }else{
        xERR = httpd_resp_send(req, NULL, 0);
        if(ESP_OK != xERR) return xERR;       
    }
    
    // const char* resp_str = html[i];

    return ESP_OK;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    if(server != NULL){
        httpd_stop(server);
        server = NULL;
    }
}


/* An HTTP GET handler */
static esp_err_t http_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len = 0;
    int page = 0;
    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */

    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
            ESP_LOGI(TAG, "Request Path: %s", req->uri);
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
    
    char* request_url = NULL;
    if(strcmp("/", req->uri) == 0){
        request_url = malloc(strlen("/index.html") + strlen(HTTPD_WEB_ROOT) + 1); //index.html路径
        if(request_url == NULL) return ESP_ERR_NO_MEM;
        strcpy(request_url, HTTPD_WEB_ROOT);
        strcat(request_url, "/index.html");
        // if(read_wifi_config(&sta_wifi_cfg) == ESP_OK)
            // ESP_LOGI(TAG, "Read WiFi Config: SSID: %s, PASSWD: %s", sta_wifi_cfg.ssid, sta_wifi_cfg.password);
    }else if(strncmp("/html", req->uri, 5) == 0){
        /* Read URL query string length and allocate memory for length + 1,
        * extra byte for null termination */
        buf_len = httpd_req_get_url_query_len(req) + 1;
        if (buf_len > 1) {
            buf = malloc(buf_len);
            if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query => %s", buf);
                char param[16];
                /* Get value of expected key from query string */
                if (httpd_query_key_value(buf, "page", param, sizeof(param)) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => page=%s", param);
                    string2Int(param, &page);
                    request_url = malloc(strlen(html_page[page]) + strlen(HTTPD_WEB_ROOT) + 1);
                    strcpy(request_url, HTTPD_WEB_ROOT);
                    strcat(request_url, html_page[page]);
                    // switch(page){
                    //     case 1:
                    //     case 2:
                    //     case 3:
                    //     case 4:
                    //     case 5:
                    //     default:
                    //         request_url = malloc(strlen(html_page[page]) + strlen(HTTPD_WEB_ROOT) + 1);
                    //         strcpy(request_url, HTTPD_WEB_ROOT);
                    //         strcat(request_url, html_page[page]);
                    //     break;
                    // }
                }
            }
            free(buf);
        }
        // request_url = malloc(strlen(req->uri) + strlen(HTTPD_WEB_ROOT) + 1); // 文件路径
        // if(request_url == NULL) return ESP_ERR_NO_MEM;
        // strcpy(request_url, HTTPD_WEB_ROOT);
        // strcat(request_url, req->uri);
    }else if(strcmp("/css/style.css", req->uri) == 0){
        request_url = malloc(strlen(req->uri) + strlen(HTTPD_WEB_ROOT) + 1); // 文件路径
        if(request_url == NULL) return ESP_ERR_NO_MEM;
        strcpy(request_url, HTTPD_WEB_ROOT);
        strcat(request_url, req->uri);
    }else{
        request_url = malloc(2);
        request_url[0] = '\0';
        httpd_resp_set_status(req, HTTPD_404);
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

httpd_uri_t get_index = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};
httpd_uri_t get_html = {
    .uri       = "/html",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};
httpd_uri_t get_css = {
    .uri       = "/css/style.css",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};

/* An HTTP POST handler */
static esp_err_t http_post_handler(httpd_req_t *req)
{
    char* buf = NULL;
    size_t buf_len = req->content_len;
    char* query_buf = NULL;
    size_t query_len = 0;
    int save_page = 0;
    
    if(buf_len > 0){
        uint32_t save_status = 0;
        int ret = 0;
        esp_err_t err;
        uint8_t is_ota = 0;
        fota_config_t ota_cfg;

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
            
        }else if(strncmp("/save", req->uri, 5) == 0){
            query_len = httpd_req_get_url_query_len(req) + 1;
            if (query_len > 1) {
                query_buf = malloc(query_len);
                if (httpd_req_get_url_query_str(req, query_buf, query_len) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query => %s", query_buf);
                    char param[16];
                    /* Get value of expected key from query string */
                    if (httpd_query_key_value(query_buf, "page", param, sizeof(param)) == ESP_OK) {
                        ESP_LOGI(TAG, "Found URL query parameter => page=%s", param);
                        string2Int(param, &save_page);
                        sta_wifi_config_t sta_wifi_cfg;
                        c_mqtt_config_t c_mqtt_cfg;

                        size_t cfg_size = 0;
                        switch(save_page){
                            case 0:
                            break;
                            case 1:   // WIFI Config
                                 
                                get_wifi_config_from_url(buf, &sta_wifi_cfg);
                                // ESP_LOGI("## HTTPD Config WiFi ##", "%s, %s", sta_wifi_cfg.ssid, sta_wifi_cfg.password);
                                // err = save_wifi_config(&sta_wifi_cfg);
                                cfg_size = sizeof(wifi_config_t);
                                err = save_data_to_nvs(WIFI_CONFIG_NAMESPACE, WIFI_CONFIG_KEY, &sta_wifi_cfg, cfg_size);
                                if(ESP_OK == err){
                                    save_status |= (0x01 << save_page);
                                    // restart_chip();
                                }else{
                                    save_status &= (~(0x01 << save_page));
                                }

                            break;
                            case 2:  // MQTT Broker Config

                                get_mqtt_config_from_url(buf, &c_mqtt_cfg);
                                // err = save_mqtt_config(&c_mqtt_cfg);
                                cfg_size = sizeof(c_mqtt_config_t);
                                err = save_data_to_nvs(MQTT_CONFIG_NAMESPACE, MQTT_CONFIG_KEY, &c_mqtt_cfg, cfg_size);
                                if(ESP_OK == err){
                                    save_status |= (0x01 << save_page);
                                    ESP_LOGI("## HTTPD Config MQTT ##", "MQTT Config Save Success!");
                                }else{
                                    ESP_LOGE("## HTTPD Config MQTT ##", "MQTT Config Save Failure ERROR Code:%d", err);
                                    save_status &= (~(0x01 << save_page));
                                }
                            break;
                            case 3: // OTA
                                get_fota_config_from_url(buf, &ota_cfg);
                                ESP_LOGI(TAG, "OTA Server:%s | Port: %s | File:%s",ota_cfg.server_host, ota_cfg.server_port, ota_cfg.ota_file);
                                
                                is_ota = 1;
                            break;
                            case 4:  // Aliyun MQTT Sign Config

                            break;
                            default:
                            break;
                        }

                    }
                }
                free(query_buf);
            }
        }
        free(buf);
        if(is_ota == 0){
            if(save_status & (0x01 << save_page)){ // config save successed
                // httpd_resp_set_status(req, HTTPD_200);
                httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
                http_send_request_file(req, "/c/www/200.html");
            }else{
                httpd_resp_set_status(req, HTTPD_500);
                httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
                http_send_request_file(req, "/c/www/500.html");
            }
        }else{  //
            // httpd_resp_set_status(req, HTTPD_200);
            httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
            httpd_resp_send(req, NULL, 0);
        }
        if(save_status & (0x01 << 2)){
            xEventGroupSetBits(mqtt_event_group, MQTT_CLIENT_START_BIT);  
        }
        if(is_ota){
            // vTaskDelay(1000 / portMAX_DELAY);
            // xEventGroupSetBits(fota_event_group, FOTA_START_BIT);
            notice_fota_update_task(&ota_cfg);
        }
    }
    // End response
    // httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t post_save = {
    .uri       = "/save",
    .method    = HTTP_POST,
    .handler   = http_post_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL,
};

void http_req_handle_task(void* parm){
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
            ESP_LOGE("## HTTPD ##", "http handle task recieve failure");
        }
    }
}

httpd_handle_t start_webserver(httpd_handle_t* server)
{
    // httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(*server, &get_index);
        httpd_register_uri_handler(*server, &get_html);
        httpd_register_uri_handler(*server, &post_save);
        httpd_register_uri_handler(*server, &get_css);
        return *server;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return NULL;
}


void http_server_task(void* parm){
    
    EventBits_t uBit;

    while(true){
        uBit = xEventGroupWaitBits(http_event_group, HTTP_SERVER_START_BIT | HTTP_SERVER_STOP_BIT, true, false, portMAX_DELAY);
        if(uBit == HTTP_SERVER_START_BIT){
            if(http_server_handle == NULL){
                start_webserver(&http_server_handle);
            }
        }else if(HTTP_SERVER_STOP_BIT == uBit){
            stop_webserver(http_server_handle);  
        }
    }
}


void create_http_server_task(void * const pvParameters, UBaseType_t uxPriority){
    BaseType_t xReturn = pdFAIL;

    http_event_group = xEventGroupCreate();

    if(http_server_handle == NULL){
        xReturn = xTaskCreate(http_server_task, "http_server_task", 1024, pvParameters, uxPriority, &http_server_task_handle);
        if(xReturn != pdPASS){
            ESP_LOGE(TAG, "\"http_server_task\" create failure!");
        }
    }
}

void delete_http_server_task(void){
    if(http_server_task_handle != NULL){
        vTaskDelete(http_server_task_handle);
        http_server_task_handle = NULL;
    }else{
        ESP_LOGE(TAG, "http server task do not create");
    }
}

void notice_http_server_task(bool en){
    if(http_event_group != NULL){
        if(en){
            xEventGroupClearBits(http_event_group, HTTP_SERVER_STOP_BIT);
            xEventGroupSetBits(http_event_group, HTTP_SERVER_START_BIT);
        }else{
            xEventGroupClearBits(http_event_group, HTTP_SERVER_START_BIT);
            xEventGroupSetBits(http_event_group, HTTP_SERVER_STOP_BIT);
        }
    }else{
        ESP_LOGE(TAG, "http event group is null");
    }
}

void create_http_req_handle_task(void * const pvParameters, UBaseType_t uxPriority){
    BaseType_t xReturn = pdFAIL;
    if(http_req_handle_task_handle == NULL){
        q_http_req = xQueueCreate(2, sizeof(httpd_req_t));
        xReturn = xTaskCreate(http_req_handle_task, "http_req_handle_task", 1024, pvParameters, uxPriority, &http_req_handle_task_handle);
        if(xReturn != pdPASS){
            ESP_LOGE(TAG, "\"http_req_handle_task\" create failure!");
        }
    }
}

void delete_http_req_handle_task(void){
    if(http_req_handle_task_handle != NULL){
        vQueueDelete(q_http_req);
        q_http_req = NULL;
        vTaskDelete(http_req_handle_task_handle);
        http_req_handle_task_handle = NULL;
    }
}






