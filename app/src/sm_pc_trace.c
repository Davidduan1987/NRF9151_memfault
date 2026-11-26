/* SPDX-License-Identifier: LicenseRef-Nordic-5-Clause */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <memfault/core/custom_data_recording.h>
#include <memfault/core/log.h>
#include <nrf_modem.h>
#include <modem/nrf_modem_lib.h>
#include <modem/nrf_modem_lib_trace.h>

#include "sm_at_fota.h"
#include "sm_at_host.h"
#include "sm_pc_trace.h"
#include "sm_util.h"

LOG_MODULE_REGISTER(sm_pc_trace, CONFIG_SM_LOG_LEVEL);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_callback;
static atomic_t pending;
static bool cdr_processed;
static bool cdr_failed;
static uint32_t trace_size;
static const char *trace_mimetypes[] = { MEMFAULT_CDR_BINARY };

bool sm_pc_trace_pending(void)
{
	return atomic_get(&pending) != 0;
}

bool sm_pc_trace_cdr_complete(void)
{
	return cdr_processed;
}

bool sm_pc_trace_cdr_read_failed(void)
{
	return cdr_failed;
}

void sm_pc_trace_retry(void)
{
	cdr_processed = trace_size == 0;
	cdr_failed = false;
}

static bool trace_has_cdr(sMemfaultCdrMetadata *metadata)
{
	if (!sm_pc_trace_pending() || trace_size == 0 || cdr_processed) {
		return false;
	}

	*metadata = (sMemfaultCdrMetadata) {
		.start_time.type = kMemfaultCurrentTimeType_Unknown,
		.mimetypes = trace_mimetypes,
		.num_mimetypes = ARRAY_SIZE(trace_mimetypes),
		.data_size_bytes = trace_size,
		.collection_reason = "modem traces",
	};
	return true;
}

static bool trace_read_cdr(uint32_t offset, void *data, size_t len)
{
	int ret = nrf_modem_lib_trace_peek_at(offset, data, len);

	if (ret < 0 || (size_t)ret != len) {
		LOG_ERR("Trace read failed at %u: %d", offset, ret);
		cdr_failed = true;
		return false;
	}
	return true;
}

static void trace_cdr_processed(void)
{
	cdr_processed = true;
}

static const sMemfaultCdrSourceImpl trace_cdr = {
	.has_cdr_cb = trace_has_cdr,
	.read_data_cb = trace_read_cdr,
	.mark_cdr_read_cb = trace_cdr_processed,
};

int sm_pc_trace_uploaded(void)
{
	int err = nrf_modem_lib_trace_clear();

	if (err) {
		LOG_ERR("Failed to clear uploaded trace: %d", err);
		return err;
	}
	trace_size = 0;
	atomic_clear(&pending);
	err = nrf_modem_lib_trace_level_set(NRF_MODEM_LIB_TRACE_LEVEL_LTE_AND_IP);
	if (err) {
		LOG_ERR("Failed to resume modem trace: %d", err);
	}
	return err;
}

bool sm_pc_trace_prepare_modem_fault(void)
{
	int err;

	if (sm_pc_trace_pending() || sm_pc_trace_observability_busy() ||
	    sm_pc_trace_dfu_busy() || sm_fota_stage != FOTA_STAGE_INIT ||
	    sm_fota_type != SM_FOTA_TYPE_NONE) {
		LOG_ERR("Cannot freeze fault trace while another operation is active");
		return false;
	}
	atomic_set(&pending, 1);
	err = sm_pc_trace_disconnect_cloud();
	if (err == -EBUSY) {
		LOG_ERR("Cannot disconnect cloud for fault trace: %d", err);
		atomic_clear(&pending);
		return false;
	}
	if (err) {
		LOG_WRN("Cloud socket close failed after modem fault: %d", err);
	}
	return true;
}

void sm_pc_trace_finish_modem_fault(int init_result)
{
	if (init_result) {
		LOG_ERR("Cannot prepare fault trace: modem init failed: %d", init_result);
		urc_send("\r\n#XPCMODEMTRACE: ERROR,%d\r\n", init_result);
		return;
	}
	trace_size = nrf_modem_lib_trace_data_size();
	if (trace_size > CONFIG_NRF_MODEM_LIB_TRACE_BACKEND_FLASH_PARTITION_SIZE) {
		LOG_ERR("Invalid fault trace size: %u", trace_size);
		urc_send("\r\n#XPCMODEMTRACE: ERROR,%d\r\n", -EIO);
		return;
	}
	sm_pc_trace_retry();
	MEMFAULT_LOG_SAVE(kMemfaultPlatformLogLevel_Error, "Modem fault trace captured");
	memfault_log_trigger_collection();
	urc_send("\r\n#XPCMODEMTRACE: READY,%u\r\n", trace_size);
}

NRF_MODEM_LIB_ON_INIT(pc_trace_init_hook, pc_trace_modem_init, NULL)

static void pc_trace_modem_init(int ret, void *ctx)
{
	ARG_UNUSED(ctx);

	if (ret == 0 && !sm_pc_trace_pending()) {
		int err = nrf_modem_lib_trace_level_set(NRF_MODEM_LIB_TRACE_LEVEL_LTE_AND_IP);

		if (err) {
			LOG_ERR("Failed to enable modem trace: %d", err);
		}
	}
}

static void capture_work_handler(struct k_work *work)
{
	int err;
	uint32_t dropped_before;

	ARG_UNUSED(work);

	if (sm_pc_trace_pending() || sm_pc_trace_observability_busy() ||
	    sm_pc_trace_dfu_busy() || sm_fota_stage != FOTA_STAGE_INIT ||
	    sm_fota_type != SM_FOTA_TYPE_NONE) {
		urc_send("\r\n#XPCMODEMTRACE: ERROR,%d\r\n", -EBUSY);
		return;
	}

	atomic_set(&pending, 1);
	err = sm_pc_trace_disconnect_cloud();
	if (err) {
		goto failed;
	}
	err = sm_util_at_printf("AT+CFUN=4");
	if (err) {
		goto failed;
	}
	err = nrf_modem_lib_trace_level_set(NRF_MODEM_LIB_TRACE_LEVEL_OFF);
	if (err) {
		goto failed;
	}
	err = nrf_modem_lib_shutdown();
	if (err) {
		goto failed;
	}
	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("Modem reinitialization failed: %d", err);
		urc_send("\r\n#XPCMODEMTRACE: ERROR,%d\r\n", err);
		return;
	}

	trace_size = nrf_modem_lib_trace_data_size();
	if (trace_size > CONFIG_NRF_MODEM_LIB_TRACE_BACKEND_FLASH_PARTITION_SIZE) {
		err = -EIO;
		goto failed;
	}
	sm_pc_trace_retry();
	dropped_before = memfault_log_get_dropped_count();
	MEMFAULT_LOG_SAVE(kMemfaultPlatformLogLevel_Info,
			  "Hello world from nRF9151 Button 1");
	if (memfault_log_get_dropped_count() != dropped_before) {
		LOG_ERR("Memfault log buffer dropped the button log");
		urc_send("\r\n#XPCMODEMTRACE: LOG_ERROR,%d\r\n", -ENOSPC);
	}
	memfault_log_trigger_collection();
	urc_send("\r\n#XPCMODEMTRACE: READY,%u\r\n", trace_size);
	return;

failed:
	LOG_ERR("Trace capture failed: %d", err);
	atomic_clear(&pending);
	if (nrf_modem_is_initialized()) {
		int resume_err =
			nrf_modem_lib_trace_level_set(NRF_MODEM_LIB_TRACE_LEVEL_LTE_AND_IP);

		if (resume_err) {
			LOG_ERR("Failed to resume trace: %d", resume_err);
		}
	}
	urc_send("\r\n#XPCMODEMTRACE: ERROR,%d\r\n", err);
}
K_WORK_DELAYABLE_DEFINE(capture_work, capture_work_handler);

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	k_work_reschedule_for_queue(&sm_work_q, &capture_work, K_MSEC(50));
}

static int pc_trace_init(void)
{
	int err;

	if (!memfault_cdr_register_source(&trace_cdr)) {
		LOG_ERR("Failed to register trace CDR");
		return -ENOMEM;
	}
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Button 1 GPIO is unavailable");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err) {
		LOG_ERR("Failed to configure Button 1: %d", err);
		return err;
	}
	gpio_init_callback(&button_callback, button_pressed, BIT(button.pin));
	err = gpio_add_callback(button.port, &button_callback);
	if (err) {
		LOG_ERR("Failed to register Button 1 callback: %d", err);
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Failed to enable Button 1 interrupt: %d", err);
	}
	return err;
}
SYS_INIT(pc_trace_init, APPLICATION, 101);
