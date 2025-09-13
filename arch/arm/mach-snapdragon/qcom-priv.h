// SPDX-License-Identifier: GPL-2.0

#ifndef __QCOM_PRIV_H__
#define __QCOM_PRIV_H__

#include <linux/types.h>

typedef struct qcom_mem_bank {
	phys_addr_t start;
	phys_size_t size;
} qcom_mem_bank;

#if IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT)
void qcom_configure_capsule_updates(void);
#else
void qcom_configure_capsule_updates(void) {}
#endif /* EFI_HAVE_CAPSULE_SUPPORT */

#if CONFIG_IS_ENABLED(OF_LIVE)
/**
 * qcom_of_fixup_nodes() - Fixup Qualcomm DT nodes
 *
 * Adjusts nodes in the live tree to improve compatibility with U-Boot.
 */
void qcom_of_fixup_nodes(void);
#else
static inline void qcom_of_fixup_nodes(void)
{
	log_debug("Unable to dynamically fixup USB nodes, please enable CONFIG_OF_LIVE\n");
}
#endif /* OF_LIVE */

/* Allows board-specific code to patch parsed memory banks */
qcom_mem_bank* qcom_get_memory_banks(void);
void qcom_sort_memory_banks(qcom_mem_bank* banks, size_t size);

#endif /* __QCOM_PRIV_H__ */
