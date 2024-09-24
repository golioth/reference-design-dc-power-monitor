/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_state, LOG_LEVEL_DBG);

#include <golioth/client.h>
#include <golioth/lightdb_state.h>
#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include <zephyr/kernel.h>

#include "app_state.h"
#include "app_sensors.h"

#define LIVE_RUNTIME_FMT "{\"live_runtime\":{\"ch0\":%lld,\"ch1\":%lld}"
#define CUMULATIVE_RUNTIME_FMT ",\"cumulative\":{\"ch0\":%lld,\"ch1\":%lld}}"
#define DEVICE_STATE_FMT LIVE_RUNTIME_FMT "}"
#define DEVICE_STATE_FMT_CUMULATIVE LIVE_RUNTIME_FMT CUMULATIVE_RUNTIME_FMT
#define DESIRED_RESET_KEY "reset_cumulative"

uint32_t _example_int0;
uint32_t _example_int1 = 1;

static struct golioth_client *client;

static struct ontime ot;

static void async_handler(struct golioth_client *client,
				       const struct golioth_response *response,
				       const char *path,
				       void *arg)
{
	if (response->status != GOLIOTH_OK) {
		LOG_WRN("Failed to set state: %d", response->status);
		return;
	}

	LOG_DBG("State successfully set");
}

/* Forward declaration */
static void app_state_desired_handler(struct golioth_client *client,
				      const struct golioth_response *response,
				      const char *path,
				      const uint8_t *payload,
				      size_t payload_size,
				      void *arg);

int app_state_reset_desired(void)
{
	uint8_t cbor_payload[32];
	bool ok;

	ZCBOR_STATE_E(encoding_state, 16, cbor_payload, sizeof(cbor_payload), 0);
	ok = zcbor_map_start_encode(encoding_state, 2) &&
	     zcbor_tstr_put_lit(encoding_state, DESIRED_RESET_KEY) &&
	     zcbor_bool_put(encoding_state, false) &&
	     zcbor_map_end_encode(encoding_state, 2);

	if (!ok) {
		LOG_ERR("Error encoding CBOR to reset desired endpoint");
		return -ENODATA;
	}

	LOG_HEXDUMP_DBG(cbor_payload, encoding_state->payload - cbor_payload, "cbor_payload");

	int err;
	err = golioth_lightdb_set_async(client,
					APP_STATE_DESIRED_ENDP,
					GOLIOTH_CONTENT_TYPE_CBOR,
					cbor_payload,
					encoding_state->payload - cbor_payload,
					async_handler,
					NULL);
	if (err) {
		LOG_ERR("Unable to write to LightDB State: %d", err);
		return err;
	}
	return 0;
}

int app_state_observe(struct golioth_client *state_client)
{
	client = state_client;

	app_state_update_actual();

	char observe_path[64];

	snprintk(observe_path, sizeof(observe_path), "%s/%s", APP_STATE_DESIRED_ENDP,
		 DESIRED_RESET_KEY);
	int err = golioth_lightdb_observe_async(client,
						observe_path,
						GOLIOTH_CONTENT_TYPE_JSON,
						app_state_desired_handler,
						NULL);
	if (err) {
		LOG_WRN("failed to observe lightdb path: %d", err);
	}
	return err;
}

int app_state_update_actual(void)
{
	get_ontime(&ot);
	char sbuf[sizeof(DEVICE_STATE_FMT) + 10]; /* space for uint16 values */

	snprintk(sbuf, sizeof(sbuf), DEVICE_STATE_FMT, ot.ch0, ot.ch1);

	int err;

	err = golioth_lightdb_set_async(client,
					APP_STATE_ACTUAL_ENDP,
					GOLIOTH_CONTENT_TYPE_JSON,
					sbuf,
					strlen(sbuf),
					async_handler,
					NULL);

	if (err) {
		LOG_ERR("Unable to write to LightDB State: %d", err);
	}
	return err;
}

int app_state_report_ontime(adc_node_t *ch0, adc_node_t *ch1)
{
	int err;
	char json_buf[128];

	if (k_sem_take(&adc_data_sem, K_MSEC(300)) == 0) {

		if (ch0->loaded_from_cloud) {
			snprintk(json_buf,
				 sizeof(json_buf),
				 DEVICE_STATE_FMT_CUMULATIVE,
				 ch0->runtime,
				 ch1->runtime,
				 ch0->total_cloud + ch0->total_unreported,
				 ch1->total_cloud + ch1->total_unreported);
		} else {
			snprintk(json_buf,
				 sizeof(json_buf),
				 DEVICE_STATE_FMT,
				 ch0->runtime,
				 ch1->runtime);
			/* Cumulative not yet loaded from LightDB State */
			/* Try to load it now */
			app_work_on_connect();
		}

		err = golioth_lightdb_set_async(client,
						APP_STATE_ACTUAL_ENDP,
						GOLIOTH_CONTENT_TYPE_JSON,
						json_buf,
						strlen(json_buf),
						async_handler,
						NULL);
		if (err) {
			LOG_ERR("Failed to send sensor data to Golioth: %d", err);
			k_sem_give(&adc_data_sem);
			return err;
		} else {
			if (ch0->loaded_from_cloud) {
				ch0->total_cloud += ch0->total_unreported;
				ch0->total_unreported = 0;
				ch1->total_cloud += ch1->total_unreported;
				ch1->total_unreported = 0;
			}
		}
		k_sem_give(&adc_data_sem);
	} else {
		return -EACCES;
	}

	return 0;
}

static void app_state_desired_handler(struct golioth_client *client,
				      const struct golioth_response *response,
				      const char *path,
				      const uint8_t *payload,
				      size_t payload_size,
				      void *arg)
{
	if (response->status != GOLIOTH_OK) {
		LOG_ERR("Failed to receive '%s' endpoint: %d",
			APP_STATE_DESIRED_ENDP,
			response->status);
		return;
	}

	LOG_HEXDUMP_DBG(payload, payload_size, APP_STATE_DESIRED_ENDP);

	if (strncmp(payload, "false", strlen("false")) == 0) {
		return;
	} else if (strncmp(payload, "true", strlen("true")) == 0) {
		LOG_INF("Request to reset cumulative values received. Processing now.");
		reset_cumulative_totals();
		app_state_reset_desired();
	} else {
		LOG_ERR("Desired State Decoding Error");
		LOG_HEXDUMP_ERR(payload, payload_size, "desired_state");
		app_state_reset_desired();
		return;
	}
}
