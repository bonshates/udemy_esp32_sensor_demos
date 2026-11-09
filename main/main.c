/**
 * Application entry point
 */

#include "sdkconfig.h"
#if CONFIG_HARDWARE_TEST_ENABLED
#include "hardware_test.h"
#else
#include "bme680_sensor.h"
#endif

void app_main(void)
{
#if CONFIG_HARDWARE_TEST_ENABLED
	hardware_test_run();
#else
	BME680_task_start();
#endif
}
