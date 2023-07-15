/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_DBG);

#include <net/golioth/settings.h>
#include "main.h"

#include "app_settings.h"

static struct golioth_client *client;

static int32_t _loop_delay_s = 6;
static int16_t _adc_floor[2] = { 0, 0 };

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

enum golioth_settings_status on_setting(const char *key, const struct golioth_settings_value *value)
{

	LOG_DBG("Received setting: key = %s, type = %d", key, value->type);
	if (strcmp(key, "LOOP_DELAY_S") == 0) {
		/* This setting is expected to be numeric, return an error if it's not */
		if (value->type != GOLIOTH_SETTINGS_VALUE_TYPE_INT64) {
			LOG_DBG("Received LOOP_DELAY_S is not an integer type.");
			return GOLIOTH_SETTINGS_VALUE_FORMAT_NOT_VALID;
		}

		/* Limit to 12 hour max delay: [1, 43200] */
		if (value->i64 < 1 || value->i64 > 43200) {
			LOG_DBG("Received LOOP_DELAY_S setting is outside allowed range.");
			return GOLIOTH_SETTINGS_VALUE_OUTSIDE_RANGE;
		}

		/* Only update if value has changed */
		if (_loop_delay_s == (int32_t)value->i64) {
			LOG_DBG("Received LOOP_DELAY_S already matches local value.");
		} else {
			_loop_delay_s = (int32_t)value->i64;
			LOG_INF("Set loop delay to %d seconds", _loop_delay_s);

			wake_system_thread();
		}
		return GOLIOTH_SETTINGS_SUCCESS;
	}

	if ((strcmp(key, "ADC_FLOOR_CH0") == 0) || (strcmp(key, "ADC_FLOOR_CH1") == 0)) {
		/* This setting is expected to be numeric, return an error if it's not */
		if (value->type != GOLIOTH_SETTINGS_VALUE_TYPE_INT64) {
			return GOLIOTH_SETTINGS_VALUE_FORMAT_NOT_VALID;
		}

		/* Limit to int16_t: [-(2**15), (2**15)-1] */
		if (value->i64 < -32768 || value->i64 > 32767) {
			return GOLIOTH_SETTINGS_VALUE_OUTSIDE_RANGE;
		}

		uint8_t ch_num = 0;
		if (strcmp(key, "ADC_FLOOR_CH1") == 0) {
			ch_num = 1;
		}

		/* Only update if value has changed */
		if (_adc_floor[ch_num] == (int16_t)value->i64) {
			LOG_DBG("Received ADC_FLOOR_CH%d already matches local value.", ch_num);
		}
		else {
			_adc_floor[ch_num] = (int16_t)value->i64;
			LOG_INF("Set ADC_FLOOR_CH%d to %d", ch_num, _adc_floor[ch_num]);

			wake_system_thread();
		}
		return GOLIOTH_SETTINGS_SUCCESS;
	}

	/* If the setting is not recognized, we should return an error */
	return GOLIOTH_SETTINGS_KEY_NOT_RECOGNIZED;
}

int app_settings_init(struct golioth_client *state_client)
{
	client = state_client;
	int err = app_settings_register(client);
	return err;
}

int app_settings_observe(void)
{
	int err = golioth_settings_observe(client);

	if (err) {
		LOG_ERR("Failed to observe settings: %d", err);
	}
	return err;
}

int app_settings_register(struct golioth_client *settings_client)
{
	int err = golioth_settings_register_callback(settings_client, on_setting);

	if (err) {
		LOG_ERR("Failed to register settings callback: %d", err);
	}

	return err;
}
