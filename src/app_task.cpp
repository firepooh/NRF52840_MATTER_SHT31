/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "app/matter_init.h"
#include "app/task_executor.h"
#include "board/board.h"
#include "lib/core/CHIPError.h"
#include "lib/support/CodeUtils.h"

#include <setup_payload/OnboardingCodesUtil.h>

#include <zephyr/logging/log.h>

#include <app-common/zap-generated/attributes/Accessors.h>
//#include <app/clusters/temperature-measurement-server/temperature-measurement-server.h>
//#include <app/clusters/relative-humidity-measurement-server/relative-humidity-measurement-server.h>

//#include "sht31_sensor.h"
//#include <drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#define CONFIG_USE_VIRTUAL_SENSOR_DATA 
//#define CONFIG_USE_REAL_SENSOR_DATA
#if defined(CONFIG_USE_VIRTUAL_SENSOR_DATA) && defined(CONFIG_USE_REAL_SENSOR_DATA)
  #error "Only one of CONFIG_USE_VIRTUAL_SENSOR_DATA or CONFIG_USE_REAL_SENSOR_DATA must be defined"
#endif

// 온습도 업데이트 주기 (10초)
#define SENSOR_UPDATE_PERIOD_MS 10000
// 온습도 업데이트 스레드
K_THREAD_STACK_DEFINE(sensor_stack, 2048);

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;


struct k_thread sensor_thread_data;

static const struct device *sht31_dev;


// 엔드포인트 ID (ZAP에서 설정한 값)
constexpr chip::EndpointId kEndpointId = 1;



#if defined(CONFIG_USE_VIRTUAL_SENSOR_DATA)
// 가상 온도 센서 데이터를 읽어오는 함수
float ReadTemperatureSensorVirtual( float *temperatureC )
{
    // 20~30°C 
    *temperatureC += 0.1f;
    if( *temperatureC > 30.0f) 
      *temperatureC = 20.0f;

    LOG_DBG("Virtual Temperature: %.2f°C", (double)(*temperatureC));

    return *temperatureC;
}

// 실제 센서 드라이버에서 상대습도(%)를 읽어오는 함수 (현재는 가상 데이터)
float ReadHumiditySensorVirtual( float *humidityRH )
{
    // 40~60%
    *humidityRH += 0.1f;
    if( *humidityRH > 60.0f) 
      *humidityRH = 40.0f;

    LOG_DBG("Virtual Humidity: %.2f%%", (double)(*humidityRH));

    return *humidityRH;
}

// 가상 센서 데이터를 읽어오는 함수
void GetVirtualSensorData(float *temperatureC, float *humidityRH)
{
    *temperatureC = ReadTemperatureSensorVirtual(temperatureC);
    *humidityRH   = ReadHumiditySensorVirtual(humidityRH);
}
#endif

#if defined(CONFIG_USE_REAL_SENSOR_DATA)
void GetRealSensorData(float *temperatureC, float *humidityRH)
{
    if (!sht31_dev) {
        LOG_ERR("SHT31 device not found");
        *temperatureC = 20.0f;
        *humidityRH = 40.0f;
        return;
    }

    // 센서 샘플 가져오기
    if (sensor_sample_fetch(sht31_dev) < 0) {
        LOG_ERR("Failed to fetch sample");
    } else {
        struct sensor_value temp, hum;
        sensor_channel_get(sht31_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
        sensor_channel_get(sht31_dev, SENSOR_CHAN_HUMIDITY, &hum);

        *temperatureC = temp.val1 + (temp.val2 / 1000000.0f); // °C
        *humidityRH   = hum.val1 + (hum.val2 / 1000000.0f); // %
        LOG_DBG("Real Temperature: %.2f°C, Humidity: %.2f%%", 
                (double)(*temperatureC), (double)(*humidityRH));
    }
}
#endif

void GetSensorData(float *temperatureC, float *humidityRH)
{
  #if defined(CONFIG_USE_VIRTUAL_SENSOR_DATA)
  GetVirtualSensorData(temperatureC, humidityRH);
  #endif
  #if defined(CONFIG_USE_REAL_SENSOR_DATA)
  GetRealSensorData(temperatureC, humidityRH);
  #endif
}

// 온습도 값을 Matter 속성에 업데이트하는 함수
void UpdateTemperatureHumidity(float temperatureC, float humidityRH)
{
    using namespace chip::app::Clusters;
    using chip::Protocols::InteractionModel::Status;

    // 2) Matter 단위로 변환 (0.01 단위)
    // Temperature: 0.01°C 단위
    int16_t tempValue = static_cast<int16_t>(temperatureC * 100);
    // Humidity: 0.01% 단위  
    uint16_t humValue = static_cast<uint16_t>(humidityRH * 100);

    // 3) Temperature 속성 업데이트
    Status statusTemp = TemperatureMeasurement::Attributes::MeasuredValue::Set(kEndpointId, tempValue);
    
    if (statusTemp != Status::Success) {
        LOG_ERR("Failed to update temperature: 0x%02X", static_cast<uint8_t>(statusTemp));
    } else {
        LOG_DBG("Temperature updated: %d (%.2f°C)", tempValue, (double)(temperatureC));
    }

    // 4) Humidity 속성 업데이트 (ZAP 파일에 클러스터 추가 필요)
    // 주의: RelativeHumidityMeasurement 클러스터가 ZAP 파일에 추가되어야 함
    Status statusHum = RelativeHumidityMeasurement::Attributes::MeasuredValue::Set(kEndpointId, humValue);
    
    if (statusHum != Status::Success) {
        LOG_ERR("Failed to update humidity: 0x%02X", static_cast<uint8_t>(statusHum));
    } else {
        LOG_DBG("Humidity updated: %d (%.2f%%)", humValue, (double)(humidityRH));
    }
}

// 센서 업데이트 스레드 함수
void sensor_thread_func(void *arg1, void *arg2, void *arg3)
{
    float temperatureC = 25.0f;
    float humidityRH   = 50.0f;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    LOG_INF("Sensor thread started");

    // Matter 스택이 초기화될 때까지 대기
    k_sleep(K_SECONDS(5));

    while (1) {
        GetSensorData( &temperatureC, &humidityRH);
        UpdateTemperatureHumidity(temperatureC, humidityRH);
        k_sleep(K_MSEC(SENSOR_UPDATE_PERIOD_MS));
    }
}

#if defined(CONFIG_USE_REAL_SENSOR_DATA)
static bool sensor_device_init( void )
{
    LOG_INF("Initializing SHT31 sensor...");
    
    /* Get device handle */
    sht31_dev = DEVICE_DT_GET_ONE(sensirion_sht3xd);
    if (!sht31_dev) {
        LOG_ERR("Failed to get SHT31 device");
        return false;
    }
    
    if (!device_is_ready(sht31_dev)) {
        LOG_ERR("SHT31 device is not ready");
        return false;
    }
    
    LOG_INF("SHT31 sensor initialized successfully");

    /* Perform initial reading */
    struct sensor_value temp, hum;
    if (sensor_sample_fetch(sht31_dev) < 0) {
        LOG_ERR("Failed to fetch initial sample from SHT31");
        return false;
    } else {
        sensor_channel_get(sht31_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
        sensor_channel_get(sht31_dev, SENSOR_CHAN_HUMIDITY, &hum);
        #if 0
        LOG_INF("Initial Temperature: %d.%06d°C, Humidity: %d.%06d%%",
                temp.val1, temp.val2, hum.val1, hum.val2);
        #endif                
    }

    return true;
}
#endif

CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());

	if (!Nrf::GetBoard().Init()) {
		LOG_ERR("User interface initialization failed.");
		return CHIP_ERROR_INCORRECT_STATE;
	}

  /* Initialize sensor */
  #if defined(CONFIG_USE_REAL_SENSOR_DATA)
  sensor_device_init();
  #endif

  k_thread_create(&sensor_thread_data,
                   sensor_stack,
                   K_THREAD_STACK_SIZEOF(sensor_stack),
                   sensor_thread_func,
                   NULL, NULL, NULL,
                   K_PRIO_COOP(5),
                   0, K_NO_WAIT);

	/* Register Matter event handler that controls the connectivity status LED based on the captured Matter network
	 * state. */
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));

	return Nrf::Matter::StartServer();
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
