/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include <zephyr/logging/log.h>

#include <platform/CHIPDeviceLayer.h>
#include <app/server/Server.h>

#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/attributes/Accessors.h>


LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);


#if 0
// 온습도 값을 Matter 속성에 업데이트하는 함수
void UpdateTemperatureHumidity()
{
    // 1) 센서 읽기
    float temperatureC = ReadTemperatureSensor();    // 예: 25.34
    float humidityRH   = ReadHumiditySensor();       // 예: 48.75

    // 2) ZCL 단위(0.01)로 스케일링
    int16_t  tempValue = static_cast<int16_t>(temperatureC * 100);  // 2534
    uint16_t humValue  = static_cast<uint16_t>(humidityRH   * 100);  // 4875

    // 3) Set() 호출: 반환 타입을 Status로 받아야 함
    auto statusTemp = chip::app::Clusters::TemperatureMeasurement
                          ::Attributes::MeasuredValue
                          ::Set(kEndpointId, tempValue);

    auto statusHum  = chip::app::Clusters::RelativeHumidityMeasurement
                          ::Attributes::MeasuredValue
                          ::Set(kEndpointId, humValue);

    // 4) 결과 확인
    if (statusTemp != chip::Protocols::InteractionModel::Status::Success) {
        printf("❌ 온도 업데이트 실패: 0x%02X\n", static_cast<uint8_t>(statusTemp));
    }
    if (statusHum != chip::Protocols::InteractionModel::Status::Success) {
        printf("❌ 습도 업데이트 실패: 0x%02X\n", static_cast<uint8_t>(statusHum));
    }
}
#endif


int main()
{
	CHIP_ERROR err = AppTask::Instance().StartApp();

	LOG_ERR("Exited with code %" CHIP_ERROR_FORMAT, err.Format());
	return err == CHIP_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
}
