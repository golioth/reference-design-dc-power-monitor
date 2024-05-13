/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_DBG);

#include <golioth/client.h>
#include <golioth/settings.h>
#include "main.h"
#include "app_settings.h"

static int32_t _loop_delay_s = 6;
#define LOOP_DELAY_S_MAX 43200
#define LOOP_DELAY_S_MIN 0

static int16_t _adc_floor[2] = { 0, 0 };
#define ADC_FLOOR_MAX 32767
#define ADC_FLOOR_MIN -32768

int32_t get_loop_delay_s(void)
{
	return _loop_delay_s;
}

int16_t get_adc_floor(uint8_t ch_num)
{
	if (ch_num >= sizeof(_adc_floor)) {
		return 0;
	} else {
		return _adc_floor[ch_num];
	}
}

static enum golioth_settings_status on_loop_delay_setting(int32_t new_value, void *arg)
{
	/* Only update if value has changed */
	if (_loop_delay_s == new_value) {
		LOG_DBG("Received LOOP_DELAY_S already matches local value.");
		return GOLIOTH_SETTINGS_SUCCESS;
	}

	_loop_delay_s = new_value;
	LOG_INF("Set loop delay to %i seconds", new_value);
	wake_system_thread();
	return GOLIOTH_SETTINGS_SUCCESS;
}

static enum golioth_settings_status on_adc_floor_setting(int32_t new_value, void *arg)
{
	uint8_t ch_num = (int)arg;

	/* Only update if value has changed */
	if (_adc_floor[ch_num] == new_value) {
		LOG_DBG("Received ADC_FLOOR_CH%d already matches local value.", ch_num);
		return GOLIOTH_SETTINGS_SUCCESS;
	}

	_adc_floor[ch_num] = new_value;
	LOG_INF("Set ADC_FLOOR_CH%d to %d", ch_num, _adc_floor[ch_num]);
	wake_system_thread();
	return GOLIOTH_SETTINGS_SUCCESS;
}

void app_settings_register(struct golioth_client *client)
{
	int err;
	struct golioth_settings *settings = golioth_settings_init(client);

	err = golioth_settings_register_int_with_range(settings,
							   "LOOP_DELAY_S",
							   LOOP_DELAY_S_MIN,
							   LOOP_DELAY_S_MAX,
							   on_loop_delay_setting,
							   NULL);

	if (err) {
		LOG_ERR("Failed to register loop delay settings callback: %d", err);
	}

	err = golioth_settings_register_int_with_range(settings,
							   "ADC_FLOOR_CH0",
							   ADC_FLOOR_MIN,
							   ADC_FLOOR_MAX,
							   on_adc_floor_setting,
							   (void *)0);

	if (err) {
		LOG_ERR("Failed to register ADC_FLOOR_CH0 settings callback: %d", err);
	}

	err = golioth_settings_register_int_with_range(settings,
							   "ADC_FLOOR_CH1",
							   ADC_FLOOR_MIN,
							   ADC_FLOOR_MAX,
							   on_adc_floor_setting,
							   (void *)1);

	if (err) {
		LOG_ERR("Failed to register ADC_FLOOR_CH1 settings callback: %d", err);
	}
}
