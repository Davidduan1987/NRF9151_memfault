/* SPDX-License-Identifier: LicenseRef-Nordic-5-Clause */

#ifndef SM_PC_TRACE_H_
#define SM_PC_TRACE_H_

#include <stdbool.h>

bool sm_pc_trace_pending(void);
bool sm_pc_trace_cdr_complete(void);
bool sm_pc_trace_cdr_read_failed(void);
void sm_pc_trace_retry(void);
int sm_pc_trace_uploaded(void);
void sm_pc_trace_cloud_ready(void);
bool sm_pc_trace_observability_busy(void);
bool sm_pc_trace_dfu_busy(void);
int sm_pc_trace_disconnect_cloud(void);
bool sm_pc_trace_prepare_modem_fault(void);
void sm_pc_trace_finish_modem_fault(int init_result);

#endif
