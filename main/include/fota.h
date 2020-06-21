
#ifndef _FOTA_H_
#define _FOTA_H_



#ifdef __cplusplus
extern "C"{
#endif

typedef struct {
    char server_host[16];
    char server_port[8];
    char ota_file[64];
}fota_config_t;


/**
 * 创建任务时需要将fota_config参数传给任务
 * 
 * xTaskCreate(&native_ota_task, "native_ota_task", 8192, (fota_config_t*), 5, NULL);
 * 
 **/
// extern fota_config_t fota_cfg;

// void native_ota_task(void *pvParameter);

// void https_ota_task(void *pvParameter);

void notice_fota_update_task(fota_config_t* cfg);

void create_fota_update_task(void* pvParm);

#ifdef __cplusplus
}
#endif

#endif