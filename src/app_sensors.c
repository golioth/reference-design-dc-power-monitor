/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_sensors, LOG_LEVEL_DBG);

#include <stdlib.h>
#include <golioth/client.h>
#include <golioth/lightdb_state.h>
#include <golioth/stream.h>
#include <zcbor_decode.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>

#include "app_sensors.h"
#include "app_state.h"
#include "app_settings.h"

/* FIXME: this is an awkward include */
#include "../drivers/sensor/ina260/ina260.h"

/* Convert DC reading to actual value */
int64_t calculate_reading(uint8_t upper, uint8_t lower)
{
	int16_t raw = (upper<<8) | lower;
	uint64_t big = raw * 125;
	return big;
}

#define SPI_OP	SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8) | SPI_LINES_SINGLE

#ifdef CONFIG_LIB_OSTENTUS
#include <libostentus.h>
static const struct device *o_dev = DEVICE_DT_GET_ANY(golioth_ostentus);
#endif
#ifdef CONFIG_ALUDEL_BATTERY_MONITOR
#include "battery_monitor/battery.h"
#endif

static struct golioth_client *client;

struct k_sem adc_data_sem;

/* Formatting string for sending sensor JSON to Golioth */
#define JSON_FMT "{\"cur\":{\"ch0\":%d,\"ch1\":%d},\"vol\":{\"ch0\":%d,\"ch1\":%d},\"pow\":{\"ch0\":%d,\"ch1\":%d}}"
#define JSON_FMT_SINGLE "{\"cur\":{\"%s\":%d},\"vol\":{\"%s\":%d},\"pow\":{\"%s\":%d}}"
#define CH0_PATH "ch0"
#define CH1_PATH "ch1"
#define ADC_STREAM_ENDP	"sensor"
#define ADC_CUMULATIVE_ENDP	"state/cumulative"

#define ADC_CH0 0
#define ADC_CH1 1

adc_node_t adc_ch0 = {
	.dev = DEVICE_DT_GET(DT_NODELABEL(ina260_ch0)),
	.ch_num = ADC_CH0,
	.laston = -1,
	.runtime = 0,
	.total_unreported = 0,
	.total_cloud = 0,
	.loaded_from_cloud = false,
	.device_ready = false
};

adc_node_t adc_ch1 = {
	.dev = DEVICE_DT_GET(DT_NODELABEL(ina260_ch1)),
	.ch_num = ADC_CH1,
	.laston = -1,
	.runtime = 0,
	.total_unreported = 0,
	.total_cloud = 0,
	.loaded_from_cloud = false,
	.device_ready = false
};

void get_ontime(struct ontime *ot)
{
	ot->ch0 = adc_ch0.runtime;
	ot->ch1 = adc_ch1.runtime;
}

/* Callback for LightDB Stream */
static void async_error_handler(struct golioth_client *client,
				const struct golioth_response *response,
				const char *path,
				void *arg)
{
	if (response->status != GOLIOTH_OK) {
		LOG_ERR("Async task failed: %d", response->status);
		return;
	}
}

static int get_adc_reading(adc_node_t *adc)
{
	int err;

	err = sensor_sample_fetch(adc->dev);
	if (err) {
		LOG_ERR("Error fetching sensor values from %s: %d", adc->dev->name, err);
		adc->device_ready = false;
		return err;
	}

	adc->device_ready = true;
	return 0;
}

static int log_sensor_values(adc_node_t *sensor, bool get_new_reading)
{
	int err;

	if (get_new_reading) {
		err = get_adc_reading(sensor);
		if (err) {
			return err;
		}
	}

	if (sensor->device_ready) {
		struct sensor_value cur, pow, vol;

		sensor_channel_get(sensor->dev,
				   (enum sensor_channel)SENSOR_CHAN_VOLTAGE,
				   &vol);
		sensor_channel_get(sensor->dev,
				   (enum sensor_channel)SENSOR_CHAN_CURRENT,
					   &cur);
		sensor_channel_get(sensor->dev,
				   (enum sensor_channel)SENSOR_CHAN_POWER,
				   &pow);

		LOG_INF("Device: %s, %f V, %f A, %f W",
			sensor->dev->name,
			sensor_value_to_double(&vol),
			sensor_value_to_double(&cur),
			sensor_value_to_double(&pow)
			);

		IF_ENABLED(CONFIG_LIB_OSTENTUS, (
			char ostentus_buf[32];
			uint8_t slide_num;

			snprintk(ostentus_buf, sizeof(ostentus_buf), "%.02f V",
				 sensor_value_to_double(&vol));
			slide_num = (sensor->ch_num == 0) ? CH0_VOLTAGE : CH1_VOLTAGE;
			ostentus_slide_set(o_dev, slide_num, ostentus_buf, strlen(ostentus_buf));

			snprintk(ostentus_buf, sizeof(ostentus_buf), "%.02f mA",
				 sensor_value_to_double(&cur) * 1000);
			slide_num = (sensor->ch_num == 0) ? CH0_CURRENT : CH1_CURRENT;
			ostentus_slide_set(o_dev, slide_num, ostentus_buf, strlen(ostentus_buf));

			snprintk(ostentus_buf, sizeof(ostentus_buf), "%.02f W",
				 sensor_value_to_double(&pow));
			slide_num = (sensor->ch_num == 0) ? CH0_POWER : CH1_POWER;
			ostentus_slide_set(o_dev, slide_num, ostentus_buf, strlen(ostentus_buf));
		));
	} else {
		return -ENODATA;
	}

	return 0;
}

static int get_raw_sensor_values(adc_node_t *sensor, vcp_raw_t *values, bool get_new_reading)
{
	int err;

	if (get_new_reading) {
		err = get_adc_reading(sensor);
		if (err) {
			return err;
		}
	}

	if (sensor->device_ready) {
		struct sensor_value raw;

		sensor_channel_get(sensor->dev,
				   (enum sensor_channel)SENSOR_CHAN_INA260_VOLTAGE_RAW,
				   &raw);
		values->voltage = raw.val1;

		sensor_channel_get(sensor->dev,
				   (enum sensor_channel)SENSOR_CHAN_INA260_CURRENT_RAW,
				   &raw);
		values->current = raw.val1;

		sensor_channel_get(sensor->dev,
				   (enum sensor_channel)SENSOR_CHAN_INA260_POWER_RAW,
				   &raw);
		values->power = raw.val1;

	} else {
		return -ENODATA;
	}

	return 0;
}

static int push_dual_adc_to_golioth(vcp_raw_t *ch0_raw, vcp_raw_t *ch1_raw)
{
	int err;
	char json_buf[128];

	snprintk(json_buf,
		 sizeof(json_buf),
		 JSON_FMT,
		 ch0_raw->current,
		 ch1_raw->current,
		 ch0_raw->voltage,
		 ch1_raw->voltage,
		 ch0_raw->power,
		 ch1_raw->power
		 );

	err = golioth_stream_set_async(client,
				       ADC_STREAM_ENDP,
				       GOLIOTH_CONTENT_TYPE_JSON,
				       json_buf,
				       strlen(json_buf),
				       async_error_handler,
				       NULL);
	if (err) {
		LOG_ERR("Failed to send sensor data to Golioth: %d", err);
		return err;
	}

	app_state_report_ontime(&adc_ch0, &adc_ch1);

	return 0;
}

static int push_single_adc_to_golioth(vcp_raw_t *single_ch_data, char *single_ch_path)
{
	int err;
	char json_buf[128];

	snprintk(json_buf,
		 sizeof(json_buf),
		 JSON_FMT_SINGLE,
		 single_ch_path,
		 single_ch_data->current,
		 single_ch_path,
		 single_ch_data->voltage,
		 single_ch_path,
		 single_ch_data->power
		 );

	err = golioth_stream_set_async(client,
				       ADC_STREAM_ENDP,
				       GOLIOTH_CONTENT_TYPE_JSON,
				       json_buf,
				       strlen(json_buf),
				       async_error_handler,
				       NULL);
	if (err) {
		LOG_ERR("Failed to send sensor data to Golioth: %d", err);
		return err;
	}

	app_state_report_ontime(&adc_ch0, &adc_ch1);

	return 0;
}

static int update_ontime(int16_t adc_value, adc_node_t *ch)
{
	if (k_sem_take(&adc_data_sem, K_MSEC(300)) == 0) {
		if (adc_value <= get_adc_floor(ch->ch_num)) {
			ch->runtime = 0;
			ch->laston = -1;
		}
		else {
			int64_t ts = k_uptime_get();
			int64_t duration;
			if (ch->laston > 0) {
				duration = ts - ch->laston;
			} else {
				duration = 1;
			}
			ch->runtime += duration;
			ch->laston = ts;
			ch->total_unreported += duration;
		}
		k_sem_give(&adc_data_sem);
		return 0;
	}
	else {
		return -EACCES;
	}
}

int reset_cumulative_totals(void)
{
	if (k_sem_take(&adc_data_sem, K_MSEC(5000)) == 0) {
		k_sem_give(&adc_data_sem);
		adc_ch0.total_cloud = 0;
		adc_ch1.total_cloud = 0;
		adc_ch0.total_unreported = 0;
		adc_ch1.total_unreported = 0;
		k_sem_give(&adc_data_sem);
	} else {
		LOG_ERR("Could not reset cumulative values; blocked by semaphore.");
		return -EACCES;
	}

	/* Send new values to Golioth */
	int err = app_state_report_ontime(&adc_ch0, &adc_ch1);

	if (err) {
		LOG_ERR("Unable to send ontime to server: %d", err);
	}

	return err;
}

/* This will be called by the main() loop */
/* Do all of your work here! */
void app_sensors_read_and_stream(void)
{
	int err;
	vcp_raw_t ch0_raw, ch1_raw;
	int ch0_invalid, ch1_invalid;

	IF_ENABLED(CONFIG_ALUDEL_BATTERY_MONITOR, (
		read_and_report_battery(client);
		IF_ENABLED(CONFIG_LIB_OSTENTUS, (
			ostentus_slide_set(o_dev,
					   BATTERY_V,
					   get_batt_v_str(),
					   strlen(get_batt_v_str()));
			ostentus_slide_set(o_dev,
					   BATTERY_LVL,
					   get_batt_lvl_str(),
					   strlen(get_batt_lvl_str()));
		));
	));

	/* Fetch new readings from sensors */
	get_adc_reading(&adc_ch0);
	get_adc_reading(&adc_ch1);

	/* Get raw readings from the sensor api */
	ch0_invalid = get_raw_sensor_values(&adc_ch0, &ch0_raw, false);
	ch1_invalid = get_raw_sensor_values(&adc_ch1, &ch1_raw, false);

	if (ch0_invalid && ch1_invalid) {
		LOG_WRN("Data not available from any sensor");
		return;
	}

	/* Log the readings */
	log_sensor_values(&adc_ch0, false);
	log_sensor_values(&adc_ch1, false);

	/* Calculate the "On" time if readings are not zero */
	if (!ch0_invalid) {
		err = update_ontime(ch0_raw.current, &adc_ch0);
		if (err) {
			LOG_ERR("Failed up update ontime: %d", err);
		}
	}
	if (!ch1_invalid) {
		err = update_ontime(ch1_raw.current, &adc_ch1);
		if (err) {
			LOG_ERR("Failed up update ontime: %d", err);
		}
	}
	LOG_DBG("Ontime:\t(ch0): %lld\t(ch1): %lld", adc_ch0.runtime, adc_ch1.runtime);

	/* Send sensor data to Golioth */
	if (!ch0_invalid && !ch1_invalid) {
		push_dual_adc_to_golioth(&ch0_raw, &ch1_raw);
	} else if (!ch0_invalid) {
		push_single_adc_to_golioth(&ch0_raw, CH0_PATH);
	} else if (!ch1_invalid) {
		push_single_adc_to_golioth(&ch1_raw, CH1_PATH);
	}
}

static void get_cumulative_handler(struct golioth_client *client,
				  const struct golioth_response *response,
				  const char *path,
				  const uint8_t *payload,
				  size_t payload_size,
				  void *arg)
{
	if (response->status != GOLIOTH_OK) {
		LOG_ERR("Failed to receive cumulative value: %d", response->status);
		return;
	}

	if ((payload_size == 1) && (payload[0] == 0xf6)) {
		/* 0xf6 is Null in CBOR */
		if (k_sem_take(&adc_data_sem, K_MSEC(300)) == 0) {
			adc_ch0.loaded_from_cloud = true;
			adc_ch1.loaded_from_cloud = true;
			k_sem_give(&adc_data_sem);
		}
		return;
	}

	uint64_t decoded_ch0 = 0;
	uint64_t decoded_ch1 = 0;
	bool found_ch0 = 0;
	bool found_ch1 = 0;

	struct zcbor_string key;
	uint64_t data;
	bool ok;

	ZCBOR_STATE_D(decoding_state, 1, payload, payload_size, 1, NULL);
	ok = zcbor_map_start_decode(decoding_state);
	if (!ok) {
		goto cumulative_decode_error;
	}

	while (decoding_state->elem_count > 1) {
		ok = zcbor_tstr_decode(decoding_state, &key) &&
		     zcbor_uint64_decode(decoding_state, &data);
		if (!ok) {
			goto cumulative_decode_error;
		}

		if (strncmp(key.value, "ch0", 3) == 0) {
			found_ch0 = true;
			decoded_ch0 = data;
		} else if (strncmp(key.value, "ch1", 3) == 0){
			found_ch1 = true;
			decoded_ch1 = data;
		} else {
			continue;
		}
	}

	if ((found_ch0 && found_ch1) == false) {
		goto cumulative_decode_error;
	} else {
		LOG_DBG("Decoded: ch0: %lld, ch1: %lld", decoded_ch0, decoded_ch1);
		if (k_sem_take(&adc_data_sem, K_MSEC(300)) == 0) {
			adc_ch0.total_cloud = decoded_ch0;
			adc_ch1.total_cloud = decoded_ch1;
			adc_ch0.loaded_from_cloud = true;
			adc_ch1.loaded_from_cloud = true;
			k_sem_give(&adc_data_sem);
		}
		return;
	}

cumulative_decode_error:
	LOG_ERR("ZCBOR Decoding Error");
	LOG_HEXDUMP_ERR(payload, payload_size, "cbor_payload");
}

void app_work_on_connect(void)
{
	/* Get cumulative "on" time from Golioth LightDB State */
	int err = golioth_lightdb_get_async(client,
					    ADC_CUMULATIVE_ENDP,
					    GOLIOTH_CONTENT_TYPE_CBOR,
					    get_cumulative_handler,
					    NULL);
	if (err) {
		LOG_WRN("failed to get cumulative channel data from LightDB: %d", err);
	}
}

void app_sensors_set_client(struct golioth_client *sensors_client)
{
	client = sensors_client;
}

void app_sensors_init(void)
{
	k_sem_init(&adc_data_sem, 0, 1);

	if (device_is_ready(adc_ch0.dev)) {
		get_adc_reading(&adc_ch0);
	}

	if (device_is_ready(adc_ch1.dev)) {
		get_adc_reading(&adc_ch1);
	}

	/* Semaphores to handle data access */
	k_sem_give(&adc_data_sem);
}
