# SPDX-License-Identifier: GPL-2.0
CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -O2 -static
PROGRAMS := mdm_helper_native pm_service_native sysmon_probe_native dms_nas_probe diag_reader ipa_qmi_proxy mhi_efs_sync qrtr_tftp_server
all: $(PROGRAMS)
%: %.c
	$(CC) $(CFLAGS) -o $@ $<
clean:
	$(RM) $(PROGRAMS)
.PHONY: all clean
