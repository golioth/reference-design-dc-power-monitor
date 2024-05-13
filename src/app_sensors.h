/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __APP_SENSORS_H__
#define __APP_SENSORS_H__

#include <stdint.h>
#include <zephyr/drivers/spi.h>
#include <golioth/client.h>

extern struct k_sem adc_data_sem;

struct ontime {
	uint64_t ch0;
	uint64_t ch1;
};

typedef struct {
	const struct  device *const dev;
	uint8_t ch_num;
	int64_t laston;
	uint64_t runtime;
	uint64_t total_unreported;
	uint64_t total_cloud;
	bool loaded_from_cloud;
	bool device_ready;
} adc_node_t;

typedef struct {
	int16_t current;
	int16_t voltage;
	uint16_t power;
} vcp_raw_t;

void get_ontime(struct ontime *ot);
int reset_cumulative_totals(void);
void app_work_on_connect(void);
void app_sensors_set_client(struct golioth_client *sensors_client);
void app_sensors_read_and_stream(void);
void app_sensors_init(void);


#define LABEL_UP_COUNTER "Counter"
#define LABEL_DN_COUNTER "Anti-counter"
#define LABEL_BATTERY	 "Battery"
#define LABEL_FIRMWARE	 "Firmware"
#define SUMMARY_TITLE	 "Channel 0:"

/**
 * Each Ostentus slide needs a unique key. You may add additional slides by
 * inserting elements with the name of your choice to this enum.
 */
typedef enum {
	CH0_CURRENT,
	CH0_POWER,
	CH0_VOLTAGE,
	CH1_CURRENT,
	CH1_POWER,
	CH1_VOLTAGE,
#ifdef CONFIG_ALUDEL_BATTERY_MONITOR
	BATTERY_V,
	BATTERY_LVL,
#endif
	FIRMWARE
}slide_key;

/* Ostentus slide labels */
#define CH0_CUR_LABEL "Current ch0"
#define CH0_VOL_LABEL "Voltage ch0"
#define CH0_POW_LABEL "Power ch0"
#define CH1_CUR_LABEL "Current ch1"
#define CH1_VOL_LABEL "Voltage ch1"
#define CH1_POW_LABEL "Power ch1"

#endif /* __APP_SENSORS_H__ */
