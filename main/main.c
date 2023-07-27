#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/adc.h"
#include "esp_spi_flash.h"
#include "freertos/timers.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"

#include "ds3231.h"

#define  EXAMPLE_ESP_WIFI_SSID "HELLO"
#define  EXAMPLE_ESP_WIFI_PASS "12345678"
#define MAX_RETRY 10
#define ESP_INTR_FLAG_DEFAULT 0
#define CONFIG_BUTTON_PIN 0

struct tm rtcinfo;
int realtime= 0; 
int demthoigian = 0 ; 
static int retry_cnt = 0;
uint32_t voltage;
int cnt = 0 ; 
uint32_t MQTT_CONNEECTED = 0;
TaskHandle_t ISR = NULL;

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#define sntp_setoperatingmode esp_sntp_setoperatingmode
#define sntp_setservername esp_sntp_setservername
#define sntp_init esp_sntp_init
#endif

#if CONFIG_SET_CLOCK
	#define NTP_SERVER CONFIG_NTP_SERVER
#endif
#if CONFIG_GET_CLOCK
	#define NTP_SERVER " "
#endif
#if CONFIG_DIFF_CLOCK
	#define NTP_SERVER CONFIG_NTP_SERVER
#endif

static const char *TAG = "DS3213";

RTC_DATA_ATTR static int boot_count = 0;


void time_sync_notification_cb(struct timeval *tv)
{
	ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
	ESP_LOGI(TAG, "Initializing SNTP");
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	//sntp_setservername(0, "pool.ntp.org");
	ESP_LOGI(TAG, "Your NTP Server is %s", NTP_SERVER);
	sntp_setservername(0, NTP_SERVER);
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_init();
}

static bool obtain_time(void)
{
	ESP_ERROR_CHECK( nvs_flash_init() );
	ESP_ERROR_CHECK( esp_netif_init() );
	ESP_ERROR_CHECK( esp_event_loop_create_default() );

	/* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
	 * Read "Establishing Wi-Fi or Ethernet Connection" section in
	 * examples/protocols/README.md for more information about this function.
	 */
	ESP_ERROR_CHECK(example_connect());

	initialize_sntp();

	// wait for time to be set
	int retry = 0;
	const int retry_count = 10;
	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
		ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}

	ESP_ERROR_CHECK( example_disconnect() );
	if (retry == retry_count) return false;
	return true;
}


void setClock(void *pvParameters)
{
	
	// update 'now' variable with current time
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	now = now + (CONFIG_TIMEZONE*60*60);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(pcTaskGetName(0), "The current date/time is: %s", strftime_buf);

	// Initialize RTC
	i2c_dev_t dev;
	if (ds3231_init_desc(&dev, I2C_NUM_0, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO) != ESP_OK) {
		ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
		while (1) { vTaskDelay(1); }
	}

	ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_sec=%d",timeinfo.tm_sec);
	ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_min=%d",timeinfo.tm_min);
	ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_hour=%d",timeinfo.tm_hour);
	ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_wday=%d",timeinfo.tm_wday);
	ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_mday=%d",timeinfo.tm_mday);
	ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_mon=%d",timeinfo.tm_mon);
	ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_year=%d",timeinfo.tm_year);

	struct tm time = {
		.tm_year = 2023,
		.tm_mon  = 7,  // 0-based
		.tm_mday = 28,
		.tm_hour = 0,
		.tm_min  = 49,
		.tm_sec  = 0
	};

	if (ds3231_set_time(&dev, &time) != ESP_OK) {
		ESP_LOGE(pcTaskGetName(0), "Could not set time.");
		while (1) { vTaskDelay(1); }
	}
	ESP_LOGI(pcTaskGetName(0), "Set initial date time done");

	// goto deep sleep
	const int deep_sleep_sec = 0;
	ESP_LOGI(pcTaskGetName(0), "Entering deep sleep for %d seconds", deep_sleep_sec);
	esp_deep_sleep(1000000LL * deep_sleep_sec);
}

void getClock(void *pvParameters)
{
	// Initialize RTC
	i2c_dev_t dev;
	if (ds3231_init_desc(&dev, I2C_NUM_0, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO) != ESP_OK) {
		ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
		while (1) { vTaskDelay(1); }
	}

	// Initialise the xLastWakeTime variable with the current time.
	TickType_t xLastWakeTime = xTaskGetTickCount();

	// Get RTC date and time
	while (1) {
		float temp;


		if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
			ESP_LOGE(pcTaskGetName(0), "Could not get temperature.");
			while (1) { vTaskDelay(1); }
		}

		if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
			ESP_LOGE(pcTaskGetName(0), "Could not get time.");
			while (1) { vTaskDelay(1); }
		}

		ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, %.2f deg Cel", 
			rtcinfo.tm_year, rtcinfo.tm_mon + 1,
			rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, temp);
	vTaskDelayUntil(&xLastWakeTime, 100);
	}
}

void diffClock(void *pvParameters)
{
	// obtain time over NTP
	ESP_LOGI(pcTaskGetName(0), "Connecting to WiFi and getting time over NTP.");
	if(!obtain_time()) {
		ESP_LOGE(pcTaskGetName(0), "Fail to getting time over NTP.");
		while (1) { vTaskDelay(1); }
	}

	// update 'now' variable with current time
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	now = now + (CONFIG_TIMEZONE*60*60);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%m-%d-%y %H:%M:%S", &timeinfo);
	ESP_LOGI(pcTaskGetName(0), "NTP date/time is: %s", strftime_buf);

	// Initialize RTC
	i2c_dev_t dev;
	if (ds3231_init_desc(&dev, I2C_NUM_0, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO) != ESP_OK) {
		ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
		while (1) { vTaskDelay(1); }
	}

	// Get RTC date and time
	struct tm rtcinfo;
	if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
		ESP_LOGE(pcTaskGetName(0), "Could not get time.");
		while (1) { vTaskDelay(1); }
	}
	rtcinfo.tm_year = rtcinfo.tm_year - 1900;
	rtcinfo.tm_isdst = -1;
	ESP_LOGD(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d", 
		rtcinfo.tm_year, rtcinfo.tm_mon + 1,
		rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);

	// update 'rtcnow' variable with current time
	time_t rtcnow = mktime(&rtcinfo);
	localtime_r(&rtcnow, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%m-%d-%y %H:%M:%S", &timeinfo);
	ESP_LOGI(pcTaskGetName(0), "RTC date/time is: %s", strftime_buf);

	// Get the time difference
	double x = difftime(rtcnow, now);
	ESP_LOGI(pcTaskGetName(0), "Time difference is: %f", x);
	
	while(1) {
		vTaskDelay(1000);
	}
}


void button_task(void *arg)
{
        while(1){  
        vTaskSuspend(NULL);
        cnt ++ ; // dem so lan an nut 
        printf ("BUTTON PRESS \r\n");
        }
}
// interrupt service routine, called when the button is pressed

void IRAM_ATTR button_isr_handler(void* arg) {
        xTaskResumeFromISR(ISR);
        //portYIELD_FROM_ISR(  );
        }
static void mqtt_app_start(void);
static esp_err_t wifi_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        ESP_LOGI(TAG, "Trying to connect with Wi-Fi\n");
        break;

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Wi-Fi connected\n");
        break;

    case IP_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip: startibg MQTT Client\n");
        mqtt_app_start();
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "disconnected: Retrying Wi-Fi\n");
        if (retry_cnt++ < MAX_RETRY)
        {
            esp_wifi_connect();
        }
        else
        ESP_LOGI(TAG, "Max Retry Failed: Wi-Fi Connection\n");
        break;

    default:
        break;
    }
    return ESP_OK;
}

void wifi_init(void)
{
    esp_event_loop_create_default();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_netif_init();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        MQTT_CONNEECTED=1;
        
        msg_id = esp_mqtt_client_subscribe(client, "/topic/test1", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/test2", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        MQTT_CONNEECTED=0;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

esp_mqtt_client_handle_t client = NULL;
static void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "STARTING MQTT");
    esp_mqtt_client_config_t mqttConfig = {
        .broker.address.uri = "mqtt://192.168.137.1:1883/"};
    
    client = esp_mqtt_client_init(&mqttConfig);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}
void Publisher_Task(void *params)
{


char str[80];


  while (true)
  {
    if(MQTT_CONNEECTED)
    {
        voltage = adc1_get_raw(ADC1_CHANNEL_6); 
        sprintf (str,"[{Time: %d:%d:%d - Day: %d/%d/%d}]",rtcinfo.tm_hour,rtcinfo.tm_min,rtcinfo.tm_sec,rtcinfo.tm_mday, rtcinfo.tm_mon,rtcinfo.tm_year);
		puts(str);
        esp_mqtt_client_publish(client, "/topic/test3",  str, 0, 1,0 );
        vTaskDelay(1000/portTICK_PERIOD_MS) ;
    
    }
  }
}

void app_main(void)
{
	++boot_count;
	ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
	ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
	ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);
	ESP_LOGI(TAG, "Boot count: %d", boot_count);

#if CONFIG_SET_CLOCK
	// Set clock & Get clock
	if (boot_count == 1) {
		xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
	} else {
		xTaskCreate(getClock, "getClock", 1024*4, NULL, 2, NULL);
	}
#endif

#if CONFIG_GET_CLOCK
	// Get clock
	xTaskCreate(getClock, "getClock", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_DIFF_CLOCK
	// Diff clock
	xTaskCreate(diffClock, "diffClock", 1024*4, NULL, 2, NULL);
#endif

    // Configure ADC1 capture width
    // 12 bit decimal value from 0 to 4095
    adc1_config_width(ADC_WIDTH_BIT_12); 
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
   wifi_init();
    xTaskCreate(Publisher_Task, "Publisher_Task", 1024 * 5, NULL, 5, NULL);
}