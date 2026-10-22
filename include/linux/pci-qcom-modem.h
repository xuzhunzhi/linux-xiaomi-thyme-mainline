/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_PCI_QCOM_MODEM_H
#define _LINUX_PCI_QCOM_MODEM_H
#include <linux/kconfig.h>
#include <linux/errno.h>
struct pci_dev;
#if IS_REACHABLE(CONFIG_PCIE_QCOM)
int qcom_pcie_retrain_link(struct pci_dev *pdev);
int qcom_pcie_perst_toggle(struct pci_dev *pdev);
int qcom_pcie_relink(struct pci_dev *pdev);
#else
static inline int qcom_pcie_retrain_link(struct pci_dev *pdev) { return -EOPNOTSUPP; }
static inline int qcom_pcie_perst_toggle(struct pci_dev *pdev) { return -EOPNOTSUPP; }
static inline int qcom_pcie_relink(struct pci_dev *pdev) { return -EOPNOTSUPP; }
#endif
#endif
