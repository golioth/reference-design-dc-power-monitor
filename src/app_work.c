/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_work, LOG_LEVEL_DBG);

#include <stdlib.h>
#include <net/golioth/system_client.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>

#include "app_work.h"
#include "app_state.h"
#include "app_settings.h"
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_decode.h>
#include <qcbor/qcbor_spiffy_decode.h>

#define I2C_DEV_NAME DT_ALIAS(click_i2c)
const struct device *i2c_dev;

/* Convert DC reading to actual value */
int64_t calculate_reading(uint8_t upper, uint8_t lower) {
	int16_t raw = (upper<<8) | lower;
	uint64_t big = raw * 125;
	return big;
}

#define SPI_OP	SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8) | SPI_LINES_SINGLE

#include "app_work.h"
#include "libostentus/libostentus.h"

#ifdef CONFIG_ALUDEL_BATTERY_MONITOR
#include "battery_monitor/battery.h"
#endif

static struct golioth_client *client;

struct k_sem adc_data_sem;

/* Formatting string for sending sensor JSON to Golioth */
#define JSON_FMT "{\"ch0\":%d,\"ch1\":%d,\"vol\":{\"ch0\":%d,\"ch1\":%d},\"pow\":{\"ch0\":%d,\"ch1\":%d}}"
#define ADC_STREAM_ENDP	"sensor"
#define ADC_CUMULATIVE_ENDP	"state/cumulative"

#define ADC_CH0 0
#define ADC_CH1 1

adc_node_t adc_ch0 = {
	.i2c = SPI_DT_SPEC_GET(DT_NODELABEL(mcp3201_ch0), SPI_OP, 0),
	.ch_num = ADC_CH0,
	.laston = -1,
	.runtime = 0,
	.total_unreported = 0,
	.total_cloud = 0,
	.loaded_from_cloud = false,
	.i2c_addr = 0x40
};

adc_node_t adc_ch1 = {
	.i2c = SPI_DT_SPEC_GET(DT_NODELABEL(mcp3201_ch1), SPI_OP, 0),
	.ch_num = ADC_CH1,
	.laston = -1,
	.runtime = 0,
	.total_unreported = 0,
	.total_cloud = 0,
	.loaded_from_cloud = false,
	.i2c_addr = 0x41
};

/* Store two values for each ADC reading */
struct mcp3201_data {
	uint16_t val1;
	uint16_t val2;
	uint16_t voltage;
	uint16_t power;
};

void get_ontime(struct ontime *ot) {
	ot->ch0 = adc_ch0.runtime;
	ot->ch1 = adc_ch1.runtime;
}

/* Callback for LightDB Stream */
static int async_error_handler(struct golioth_req_rsp *rsp)
{
	if (rsp->err) {
		LOG_ERR("Async task failed: %d", rsp->err);
		return rsp->err;
	}
	return 0;
}

/*
 * Validate data received from MCP3201
 */
static int process_adc_reading(uint8_t buf_data[4], struct mcp3201_data *adc_data) {
	if (buf_data[0] & 1<<5) { return -ENOTSUP; }	/* Missing NULL bit */
	uint16_t data_msb = 0;
	uint16_t data_lsb = 0;
	data_msb = buf_data[0] & 0x1F;
	data_msb |= (data_msb<<7) | (buf_data[1]>>1);
	for (uint8_t i=0; i<12; i++) {
		bool bit_set = false;
		if (i < 2) {
			if (buf_data[1] & (1<<(1-i))) { bit_set = true; }
		}
		else if (i < 10) {
			if (buf_data[2] & (1<<(2+7-i))) { bit_set = true; }
		}
		else {
			if (buf_data[3] & (1<<(10+7-i))) { bit_set = true; }
		}

		if (bit_set) { data_lsb |= (1<<i); }
	}

	adc_data->val1 = data_msb;
	adc_data->val2 = data_lsb;
	return 0;
}

static int get_adc_reading(adc_node_t *adc, struct mcp3201_data *adc_data) {
	int err;

	uint8_t write_buf[6] = {0};
	uint8_t read_buf[6] = {0};
	int64_t reading_100k;
	char msg[32] = { '\0' };

	write_buf[0] = 0x01;
	err = i2c_write_read(i2c_dev, adc->i2c_addr, write_buf, 1, read_buf, 2);
	if (err) {
		LOG_ERR("I2C write-read err: %d", err);
		return err;
	} else {
		adc_data->val1 = (read_buf[0]<<8) | read_buf[1];

		reading_100k = calculate_reading(read_buf[0], read_buf[1]);
		snprintk(msg, sizeof(msg), "%lld.%02lld mA", reading_100k/100, llabs(reading_100k%100));
		slide_set(adc->ch_num == 0 ? CH0_CURRENT : CH1_CURRENT, msg, strlen(msg));
		LOG_INF("Current: %02X%02X -- %s", read_buf[0], read_buf[1], msg);
	}

	write_buf[0] = 0x02;
	err = i2c_write_read(i2c_dev, adc->i2c_addr, write_buf, 1, read_buf, 2);
	if (err) {
		LOG_ERR("I2C write-read err: %d", err);
		return err;
	} else {
		adc_data->voltage = (read_buf[0]<<8) | read_buf[1];

		reading_100k = calculate_reading(read_buf[0], read_buf[1]);
		snprintk(msg, sizeof(msg), "%lld.%02lld V", reading_100k/100000, llabs((reading_100k%100000)/1000));
		slide_set(adc->ch_num == 0 ? CH0_VOLTAGE : CH1_VOLTAGE, msg, strlen(msg));
		LOG_INF("Voltage Bus: %02X%02X -- %s", read_buf[0], read_buf[1], msg);
	}

	write_buf[0] = 0x03;
	err = i2c_write_read(i2c_dev, adc->i2c_addr, write_buf, 1, read_buf, 2);
	if (err) {
		LOG_ERR("I2C write-read err: %d", err);
		return err;
	} else {
		adc_data->power = (read_buf[0]<<8) | read_buf[1];

		reading_100k = calculate_reading(read_buf[0], read_buf[1]);
		snprintk(msg, sizeof(msg), "%lld.%02lld mW", reading_100k/100, llabs(reading_100k%100));
		slide_set(adc->ch_num == 0 ? CH0_POWER : CH1_POWER, msg, strlen(msg));
		LOG_INF("Power: %02X%02X -- %s", read_buf[0], read_buf[1], msg);
	}

	return 0;
}

static int push_adc_to_golioth(struct mcp3201_data *ch0_data, struct mcp3201_data *ch1_data) {
	int err;
	char json_buf[128];

	snprintk(
			json_buf,
			sizeof(json_buf),
			JSON_FMT,
			(int16_t)ch0_data->val1,
			(int16_t)ch1_data->val1,
			(int16_t)ch0_data->voltage,
			(int16_t)ch1_data->voltage,
			ch0_data->power,
			ch1_data->power
			);

	err = golioth_stream_push_cb(client, ADC_STREAM_ENDP,
			GOLIOTH_CONTENT_FORMAT_APP_JSON, json_buf, strlen(json_buf),
			async_error_handler, NULL);

	if (err) {
		LOG_ERR("Failed to send sensor data to Golioth: %d", err);
		return err;
	}

	app_state_report_ontime(&adc_ch0, &adc_ch1);

	return 0;
}

static int update_ontime(uint16_t adc_value, adc_node_t *ch) {
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

int reset_cumulative_totals(void) {
	if (k_sem_take(&adc_data_sem, K_MSEC(5000)) == 0) {
		k_sem_give(&adc_data_sem);
		adc_ch0.total_cloud = 0;
		adc_ch1.total_cloud = 0;
		adc_ch0.total_unreported = 0;
		adc_ch1.total_unreported = 0;
		k_sem_give(&adc_data_sem);
		return 0;
	} else {
		LOG_ERR("Could not reset cumulative values; blocked by semaphore.");
		return -EACCES;
	}
}

/* This will be called by the main() loop */
/* Do all of your work here! */
void app_work_sensor_read(void)
{
	int err;
	struct mcp3201_data ch0_data, ch1_data;

	get_adc_reading(&adc_ch0, &ch0_data);
	get_adc_reading(&adc_ch1, &ch1_data);

	/* Calculate the "On" time if readings are not zero */
	err = update_ontime(ch0_data.val1, &adc_ch0);
	if (err) {
		LOG_ERR("Failed up update ontime: %d", err);
	}
	err = update_ontime(ch1_data.val1, &adc_ch1);
	if (err) {
		LOG_ERR("Failed up update ontime: %d", err);
	}
	LOG_DBG("Ontime:\t(ch0): %lld\t(ch1): %lld", adc_ch0.runtime, adc_ch1.runtime);

	/* Send sensor data to Golioth */

	/* Two values were read for each sensor but we'll record only on form each
	 * channel as it's unlikely the two readings will be substantially
	 * different.
	 */
	push_adc_to_golioth(&ch0_data, &ch1_data);
}

static int get_cumulative_handler(struct golioth_req_rsp *rsp)
{
	int err;
	uint64_t decoded_ch0, decoded_ch1;

	if (rsp->err) {
		LOG_ERR("Failed to receive cumulative value: %d", rsp->err);
		return rsp->err;
	}

	LOG_HEXDUMP_DBG(rsp->data, rsp->len, ADC_CUMULATIVE_ENDP);

	QCBORDecodeContext decode_ctx;
	UsefulBufC payload = { rsp->data, rsp->len };

	QCBORDecode_Init(&decode_ctx, payload, QCBOR_DECODE_MODE_NORMAL);
	QCBORDecode_EnterMap(&decode_ctx, NULL);
	QCBORDecode_GetUInt64InMapSZ(&decode_ctx, "ch0", &decoded_ch0);
	QCBORDecode_GetUInt64InMapSZ(&decode_ctx, "ch1", &decoded_ch1);
	QCBORDecode_ExitMap(&decode_ctx);
	err = QCBORDecode_Finish(&decode_ctx);
	if (err) {
		LOG_ERR("QCBOR decode error: %d", err);
		decoded_ch0 = 0;
		decoded_ch1 = 0;
	} else {
		LOG_DBG("Decoded: ch0: %lld, ch1: %lld", decoded_ch0, decoded_ch1);
	}

	if (k_sem_take(&adc_data_sem, K_MSEC(300)) == 0) {
		adc_ch0.total_cloud = decoded_ch0;
		adc_ch1.total_cloud = decoded_ch1;
		adc_ch0.loaded_from_cloud = true;
		adc_ch1.loaded_from_cloud = true;

		LOG_DBG("CH0: %lld, %d\tCH1: %lld, %d", adc_ch0.total_cloud,
				adc_ch0.loaded_from_cloud,adc_ch1.total_cloud,
				adc_ch1.loaded_from_cloud);

		k_sem_give(&adc_data_sem);
	}
	return 0;
}

void app_work_on_connect(void) {
	/* Get cumulative "on" time from Golioth LightDB State */
	int err;
	err = golioth_lightdb_get_cb(client, ADC_CUMULATIVE_ENDP,
				     GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				     get_cumulative_handler, NULL);
	if (err) {
		LOG_WRN("failed to get cumulative channel data from LightDB: %d", err);
	}
}

void app_work_init(struct golioth_client *work_client)
{
	client = work_client;
	k_sem_init(&adc_data_sem, 0, 1);


	LOG_DBG("Setting up current clamp ADCs...");
	LOG_DBG("mcp3201_ch0.bus = %p", adc_ch0.i2c.bus);
	LOG_DBG("mcp3201_ch0.config.cs->gpio.port = %p", adc_ch0.i2c.config.cs->gpio.port);
	LOG_DBG("mcp3201_ch0.config.cs->gpio.pin = %u", adc_ch0.i2c.config.cs->gpio.pin);
	LOG_DBG("mcp3201_ch1.bus = %p", adc_ch1.i2c.bus);
	LOG_DBG("mcp3201_ch1.config.cs->gpio.port = %p", adc_ch1.i2c.config.cs->gpio.port);
	LOG_DBG("mcp3201_ch1.config.cs->gpio.pin = %u", adc_ch1.i2c.config.cs->gpio.pin);

	/* Get i2c from devicetree */
	i2c_dev = DEVICE_DT_GET(I2C_DEV_NAME);
	LOG_DBG("Got i2c_dev");
	i2c_configure(i2c_dev, I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_CONTROLLER);
	if (!i2c_dev)
	{
		LOG_ERR("Cannot get I2C device");
		return;
	}

	/* Semaphores to handle data access */
	k_sem_give(&adc_data_sem);
}
