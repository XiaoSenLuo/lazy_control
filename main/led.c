



#include "main.h"

#include "driver/ledc.h"

static const char *TAG="## LED CONTROL ##";


#define LEDC_TEST_DUTY         (4096)
#define LEDC_TEST_FADE_TIME    (300)

#define cal_duty(duty)         ((uint32_t)((duty) * 8196 / 100))


#define LEDC_CONFIG_NAMESPACE               "ledc_config"


#if(MODE_ESP12 == 1) && (MODE_ESP01 == 0)
static const char* nvs_ledc_config_key[LEDC_TEST_CH_NUM] = {
    "channel_0",
    "channel_1",
    "channel_2",
    "channel_3",
};
int ledc_gpio_num[LEDC_TEST_CH_NUM] = {12, 13, 14, 2};
#elif(MODE_ESP12 == 0) && (MODE_ESP01 == 1)
static const char* nvs_ledc_config_key[LEDC_TEST_CH_NUM] = {
    "channel_0",
    "channel_1"
};
int ledc_gpio_num[LEDC_TEST_CH_NUM] = {0, 2};

#endif

static TaskHandle_t led_control_task_handle = NULL;
static QueueHandle_t q_led_brightness = NULL;

static ledc_channel_config_t ledc_channel[LEDC_TEST_CH_NUM];

static int ledc_fade_time = 1000;
static int ledc_freq_hz = 512;

void ledc_initialise(void){
    int ch = 0;
    /*
     * Prepare and set configuration of timers
     * that will be used by LED Controller
     */

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT,  // resolution of PWM duty
        .freq_hz = ledc_freq_hz,              // frequency of PWM signal
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // timer mode
        .timer_num = LEDC_TIMER_0            // timer index
    };
    // Set configuration of timer0 for high speed channels
    ledc_timer_config(&ledc_timer);

    // Prepare and set configuration of timer1 for low speed channels
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.timer_num = LEDC_TIMER_1;
    ledc_timer_config(&ledc_timer);   

    /*
     * Prepare individual configuration
     * for each channel of LED Controller
     * by selecting:
     * - controller's channel number
     * - output duty cycle, set initially to 0
     * - GPIO number where LED is connected to
     * - speed mode, either high or low
     * - timer servicing selected channel
     *   Note: if different channels use one timer,
     *         then frequency and bit_num of these channels
     *         will be the same
     */

    // Set LED Controller with previously prepared configuration
    // uint8_t gpio_index = 0;

    for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
        ledc_channel[ch].gpio_num = ledc_gpio_num[ch];
        ledc_channel[ch].channel = LEDC_CHANNEL_0 + ch;
        ledc_channel[ch].duty = 0;
        ledc_channel[ch].speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_channel_config(&ledc_channel[ch]);
    }

    // Initialize fade service.
    ledc_fade_func_install(0);
}

// static void save_led_config(){

// }

static void led_control_task(void* pvParameters){
    ledc_channel_config_t channel_config;
    BaseType_t xRetrun = pdFAIL;
    uint8_t i = 0;
    esp_err_t err;

    ledc_initialise();

    for(i = 0; i < LEDC_TEST_CH_NUM; i++){
        memset(&channel_config, 0, sizeof(ledc_channel_config_t));
        err = read_data_from_nvs(LEDC_CONFIG_NAMESPACE, nvs_ledc_config_key[i], &channel_config, sizeof(ledc_channel_config_t));
        if(err == ESP_OK){
            ESP_LOGI(TAG, "read the config of ledc channel %d successful", channel_config.channel);
            ledc_set_fade_with_time(channel_config.speed_mode, channel_config.channel, channel_config.duty, ledc_fade_time);
            ledc_fade_start(channel_config.speed_mode, channel_config.channel, LEDC_FADE_NO_WAIT);
        }
    }

    while (true){
        xRetrun = xQueueReceive(q_led_brightness, &channel_config, portMAX_DELAY);
        if(xRetrun == pdPASS){
            ledc_set_fade_with_time(channel_config.speed_mode, channel_config.channel, channel_config.duty, ledc_fade_time);
            ledc_fade_start(channel_config.speed_mode, channel_config.channel, LEDC_FADE_NO_WAIT);
        }else{
            ESP_LOGE(TAG, "led bbrightness receive failure!");
        }
    }    
}

void create_led_control_task(void * const pvParameters, UBaseType_t uxPriority){
    BaseType_t xRetrun = pdFAIL;

    if(led_control_task_handle == NULL){
        q_led_brightness = xQueueCreate(8, sizeof(ledc_channel_config_t));
        xRetrun = xTaskCreate(led_control_task, "led_control_task", 1024+128+512, pvParameters, uxPriority, &led_control_task_handle);
        if(xRetrun != pdPASS){
            ESP_LOGE(TAG, "led_control_task create failure!");
        }
    }
}

void delete_led_control_task(void){
    if(led_control_task_handle != NULL){
        vQueueDelete(q_led_brightness);
        q_led_brightness = NULL;
        vTaskDelete(led_control_task_handle);
        led_control_task_handle = NULL;
    }
}

void set_led_brightness(gpio_num_t gpio, uint8_t br){
    uint8_t i  = 0;
    BaseType_t xRetrun = pdFAIL;
    uint8_t ret = 3;
    esp_err_t err;

    if(led_control_task_handle != NULL){
        // while((eTaskGetState(led_control_task_handle) > 2) && ((--ret) != 0)){
        //     vTaskDelay(1000 / portTICK_PERIOD_MS);
        //     ESP_LOGW(TAG, "\"led control task\" has been suspended or deleted!");
        // }
        // if((ret == 0) && (eTaskGetState(led_control_task_handle) > 2)){
        //     return;
        // }else{
        //     ret = 3;
        // }
        if(!GPIO_IS_VALID_GPIO(gpio)) return; // 非法 GPIO
        for(i = 0; i < LEDC_TEST_CH_NUM; i++){
            if(gpio == ledc_channel[i].gpio_num){
                ledc_channel[i].duty = cal_duty(br);
                ESP_LOGI(TAG, "led brightness is %d", br);
                err = save_data_to_nvs(LEDC_CONFIG_NAMESPACE, nvs_ledc_config_key[i], &ledc_channel[i], sizeof(ledc_channel_config_t)); //保存通道设置, 断电不丢失
                if(err == ESP_OK) ESP_LOGI(TAG, "save the config of ledc channel %d successful", ledc_channel[i].channel);
                while(ret){
                    ret -= 1;
                    xRetrun = xQueueSend(q_led_brightness, &ledc_channel[i], 0);
                    if(xRetrun != pdPASS) ESP_LOGE(TAG, "queue of ledc channel config send failure!");
                    else break;
                }
                break;
            }
        }
    }else{
        ESP_LOGW(TAG, "\"led control task\" do not create!");
    }
}




