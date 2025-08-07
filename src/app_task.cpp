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


// 온습도 업데이트 주기 (10초)
#define SENSOR_UPDATE_PERIOD_MS 10000
// 온습도 업데이트 스레드
K_THREAD_STACK_DEFINE(sensor_stack, 2048);

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;


struct k_thread sensor_thread_data;

// 엔드포인트 ID (ZAP에서 설정한 값)
constexpr chip::EndpointId kEndpointId = 1;

// 가상 센서 데이터
static float virtual_temperature = 25.0f;  // 초기 온도: 25°C
static float virtual_humidity = 50.0f;     // 초기 습도: 50%

/// @brief  float 값을 "정수부.소수2자리" 문자열로 포매팅
/// @param  value     포매팅할 float 값
/// @param  buf       결과 문자열을 쓸 버퍼
/// @param  buf_size  buf의 크기 (최소 8 이상 권장)
static inline char* float_to_string(float value, char *buf, size_t buf_size)
{
    int int_part  = (int)value;                            // 정수부
    int frac_part = abs((int)(value * 100)) % 100;         // 소수점 둘째자리까지

    // buf_size를 넘지 않도록 조심해서 snprintf 사용
    snprintf(buf, buf_size, "%d.%02d", int_part, frac_part);
    return buf;
}

// 실제 센서 드라이버에서 온도(℃)를 읽어오는 함수 (현재는 가상 데이터)
float ReadTemperatureSensor()
{
    char temp_buf[8];

    // 가상 데이터: 20~30°C 사이에서 변동
    virtual_temperature += ((k_uptime_get_32() % 3) - 1) * 0.1f;
    if (virtual_temperature > 30.0f) virtual_temperature = 30.0f;
    if (virtual_temperature < 20.0f) virtual_temperature = 20.0f;

    LOG_DBG("Virtual Temperature: %s°C", float_to_string(virtual_temperature, temp_buf, sizeof(temp_buf)) );

    return virtual_temperature;
}

// 실제 센서 드라이버에서 상대습도(%)를 읽어오는 함수 (현재는 가상 데이터)
float ReadHumiditySensor()
{
    char temp_buf[8];

    // 가상 데이터: 40~60% 사이에서 변동
    virtual_humidity += ((k_uptime_get_32() % 3) - 1) * 0.5f;
    if (virtual_humidity > 60.0f) virtual_humidity = 60.0f;
    if (virtual_humidity < 40.0f) virtual_humidity = 40.0f;

    LOG_INF("Virtual Humidity: %s%%", float_to_string(virtual_humidity, temp_buf, sizeof(temp_buf)));
    
    return virtual_humidity;
}

// 온습도 값을 Matter 속성에 업데이트하는 함수
void UpdateTemperatureHumidity()
{
    using namespace chip::app::Clusters;
    using chip::Protocols::InteractionModel::Status;
    char temp_buf[8];
    
    // 1) 센서 읽기
    float temperatureC = ReadTemperatureSensor();
    float humidityRH = ReadHumiditySensor();

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
        LOG_DBG("Temperature updated: %d (%s°C)", tempValue, float_to_string(temperatureC, temp_buf, sizeof(temp_buf)));
    }

    // 4) Humidity 속성 업데이트 (ZAP 파일에 클러스터 추가 필요)
    // 주의: RelativeHumidityMeasurement 클러스터가 ZAP 파일에 추가되어야 함
    Status statusHum = RelativeHumidityMeasurement::Attributes::MeasuredValue::Set(kEndpointId, humValue);
    
    if (statusHum != Status::Success) {
        LOG_ERR("Failed to update humidity: 0x%02X", static_cast<uint8_t>(statusHum));
    } else {
        LOG_DBG("Humidity updated: %d (%s%%)", humValue, float_to_string(humidityRH, temp_buf, sizeof(temp_buf)));
    }
}


// 센서 업데이트 스레드 함수
void sensor_thread_func(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    LOG_INF("Sensor thread started");

    // Matter 스택이 초기화될 때까지 대기
    k_sleep(K_SECONDS(5));

    while (1) {
        UpdateTemperatureHumidity();
        k_sleep(K_MSEC(SENSOR_UPDATE_PERIOD_MS));
    }
}



CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());

	if (!Nrf::GetBoard().Init()) {
		LOG_ERR("User interface initialization failed.");
		return CHIP_ERROR_INCORRECT_STATE;
	}

  /* Initialize sensor */
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
