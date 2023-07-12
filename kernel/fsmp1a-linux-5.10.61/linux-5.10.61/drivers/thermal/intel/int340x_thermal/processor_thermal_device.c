// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor_thermal_device.c
 * Copyright (c) 2014, Intel Corporation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/cpuhotplug.h>
#include <linux/intel_rapl.h>
#include "int340x_thermal_zone.h"
#include "../intel_soc_dts_iosf.h"

/* Broadwell-U/HSB thermal reporting device */
#define PCI_DEVICE_ID_PROC_BDW_THERMAL	0x1603
#define PCI_DEVICE_ID_PROC_HSB_THERMAL	0x0A03

/* Skylake thermal reporting device */
#define PCI_DEVICE_ID_PROC_SKL_THERMAL	0x1903

/* CannonLake thermal reporting device */
#define PCI_DEVICE_ID_PROC_CNL_THERMAL	0x5a03
#define PCI_DEVICE_ID_PROC_CFL_THERMAL	0x3E83

/* Braswell thermal reporting device */
#define PCI_DEVICE_ID_PROC_BSW_THERMAL	0x22DC

/* Broxton thermal reporting device */
#define PCI_DEVICE_ID_PROC_BXT0_THERMAL  0x0A8C
#define PCI_DEVICE_ID_PROC_BXT1_THERMAL  0x1A8C
#define PCI_DEVICE_ID_PROC_BXTX_THERMAL  0x4A8C
#define PCI_DEVICE_ID_PROC_BXTP_THERMAL  0x5A8C

/* GeminiLake thermal reporting device */
#define PCI_DEVICE_ID_PROC_GLK_THERMAL	0x318C

/* IceLake thermal reporting device */
#define PCI_DEVICE_ID_PROC_ICL_THERMAL	0x8a03

/* JasperLake thermal reporting device */
#define PCI_DEVICE_ID_PROC_JSL_THERMAL	0x4E03

/* TigerLake thermal reporting device */
#define PCI_DEVICE_ID_PROC_TGL_THERMAL	0x9A03

#define DRV_NAME "proc_thermal"

struct power_config {
	u32	index;
	u32	min_uw;
	u32	max_uw;
	u32	tmin_us;
	u32	tmax_us;
	u32	step_uw;
};

struct proc_thermal_device {
	struct device *dev;
	struct acpi_device *adev;
	struct power_config power_limits[2];
	struct int34x_thermal_zone *int340x_zone;
	struct intel_soc_dts_sensors *soc_dts;
	void __iomem *mmio_base;
};

enum proc_thermal_emum_mode_type {
	PROC_THERMAL_NONE,
	PROC_THERMAL_PCI,
	PROC_THERMAL_PLATFORM_DEV
};

struct rapl_mmio_regs {
	u64 reg_unit;
	u64 regs[RAPL_DOMAIN_MAX][RAPL_DOMAIN_REG_MAX];
	int limits[RAPL_DOMAIN_MAX];
};

/*
 * We can have only one type of enumeration, PCI or Platform,
 * not both. So we don't need instance specific data.
 */
static enum proc_thermal_emum_mode_type proc_thermal_emum_mode =
							PROC_THERMAL_NONE;

#define POWER_LIMIT_SHOW(index, suffix) \
static ssize_t power_limit_##index##_##suffix##_show(struct device *dev, \
					struct device_attribute *attr, \
					char *buf) \
{ \
	struct proc_thermal_device *proc_dev = dev_get_drvdata(dev); \
	\
	if (proc_thermal_emum_mode == PROC_THERMAL_NONE) { \
		dev_warn(dev, "Attempted to get power limit before device was initialized!\n"); \
		return 0; \
	} \
	\
	return sprintf(buf, "%lu\n",\
	(unsigned long)proc_dev->power_limits[index].suffix * 1000); \
}

POWER_LIMIT_SHOW(0, min_uw)
POWER_LIMIT_SHOW(0, max_uw)
POWER_LIMIT_SHOW(0, step_uw)
POWER_LIMIT_SHOW(0, tmin_us)
POWER_LIMIT_SHOW(0, tmax_us)

POWER_LIMIT_SHOW(1, min_uw)
POWER_LIMIT_SHOW(1, max_uw)
POWER_LIMIT_SHOW(1, step_uw)
POWER_LIMIT_SHOW(1, tmin_us)
POWER_LIMIT_SHOW(1, tmax_us)

static DEVICE_ATTR_RO(power_limit_0_min_uw);
static DEVICE_ATTR_RO(power_limit_0_max_uw);
static DEVICE_ATTR_RO(power_limit_0_step_uw);
static DEVICE_ATTR_RO(power_limit_0_tmin_us);
static DEVICE_ATTR_RO(power_limit_0_tmax_us);

static DEVICE_ATTR_RO(power_limit_1_min_uw);
static DEVICE_ATTR_RO(power_limit_1_max_uw);
static DEVICE_ATTR_RO(power_limit_1_step_uw);
static DEVICE_ATTR_RO(power_limit_1_tmin_us);
static DEVICE_ATTR_RO(power_limit_1_tmax_us);

static struct attribute *power_limit_attrs[] = {
	&dev_attr_power_limit_0_min_uw.attr,
	&dev_attr_power_limit_1_min_uw.attr,
	&dev_attr_power_limit_0_max_uw.attr,
	&dev_attr_power_limit_1_max_uw.attr,
	&dev_attr_power_limit_0_step_uw.attr,
	&dev_attr_power_limit_1_step_uw.attr,
	&dev_attr_power_limit_0_tmin_us.attr,
	&dev_attr_power_limit_1_tmin_us.attr,
	&dev_attr_power_limit_0_tmax_us.attr,
	&dev_attr_power_limit_1_tmax_us.attr,
	NULL
};

static const struct attribute_group power_limit_attribute_group = {
	.attrs = power_limit_attrs,
	.name = "power_limits"
};

static ssize_t tcc_offset_degree_celsius_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	u64 val;
	int err;

	err = rdmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, &val);
	if (err)
		return err;

	val = (val >> 24) & 0x3f;
	return sprintf(buf, "%d\n", (int)val);
}

static int tcc_offset_update(unsigned int tcc)
{
	u64 val;
	int err;

	if (tcc > 63)
		return -EINVAL;

	err = rdmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, &val);
	if (err)
		return err;

	if (val & BIT(31))
		return -EPERM;

	val &= ~GENMASK_ULL(29, 24);
	val |= (tcc & 0x3f) << 24;

	err = wrmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, val);
	if (err)
		return err;

	return 0;
}

static unsigned int tcc_offset_save;

static ssize_t tcc_offset_degree_celsius_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	unsigned int tcc;
	u64 val;
	int err;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, &val);
	if (err)
		return err;

	if (!(val & BIT(30)))
		return -EACCES;

	if (kstrtouint(buf, 0, &tcc))
		return -EINVAL;

	err = tcc_offset_update(tcc);
	if (err)
		return err;

	tcc_offset_save = tcc;

	return count;
}

static DEVICE_ATTR_RW(tcc_offset_degree_celsius);

static int stored_tjmax; /* since it is fixed, we can have local storage */

static int get_tjmax(void)
{
	u32 eax, edx;
	u32 val;
	int err;

	err = rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &eax, &edx);
	if (err)
		return err;

	val = (eax >> 16) & 0xff;
	if (val)
		return val;

	return -EINVAL;
}

static int read_temp_msr(int *temp)
{
	int cpu;
	u32 eax, edx;
	int err;
	unsigned long curr_temp_off = 0;

	*temp = 0;

	for_each_online_cpu(cpu) {
		err = rdmsr_safe_on_cpu(cpu, MSR_IA32_THERM_STATUS, &eax,
					&edx);
		if (err)
			goto err_ret;
		else {
			if (eax & 0x80000000) {
				curr_temp_off = (eax >> 16) & 0x7f;
				if (!*temp || curr_temp_off < *temp)
					*temp = curr_temp_off;
			} else {
				err = -EINVAL;
				goto err_ret;
			}
		}
	}

	return 0;
err_ret:
	return err;
}

static int proc_thermal_get_zone_temp(struct thermal_zone_device *zone,
					 int *temp)
{
	int ret;

	ret = read_temp_msr(temp);
	if (!ret)
		*temp = (stored_tjmax - *temp) * 1000;

	return ret;
}

static struct thermal_zone_device_ops proc_thermal_local_ops = {
	.get_temp       = proc_thermal_get_zone_temp,
};

static int proc_thermal_read_ppcc(struct proc_thermal_device *proc_priv)
{
	int i;
	acpi_status status;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *elements, *ppcc;
	union acpi_object *p;
	int ret = 0;

	status = acpi_evaluate_object(proc_priv->adev->handle, "PPCC",
				      NULL, &buf);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buf.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		dev_err(proc_priv->dev, "Invalid PPCC data\n");
		ret = -EFAULT;
		goto free_buffer;
	}

	if (!p->package.count) {
		dev_err(proc_priv->dev, "Invalid PPCC package size\n");
		ret = -EFAULT;
		goto free_buffer;
	}

	for (i = 0; i < min((int)p->package.count - 1, 2); ++i) {
		elements = &(p->package.elements[i+1]);
		if (elements->type != ACPI_TYPE_PACKAGE ||
		    elements->package.count != 6) {
			ret = -EFAULT;
			goto free_buffer;
		}
		ppcc = elements->package.elements;
		proc_priv->power_limits[i].index = ppcc[0].integer.value;
		proc_priv->power_limits[i].min_uw = ppcc[1].integer.value;
		proc_priv->power_limits[i].max_uw = ppcc[2].integer.value;
		proc_priv->power_limits[i].tmin_us = ppcc[3].integer.value;
		proc_priv->power_limits[i].tmax_us = ppcc[4].integer.value;
		proc_priv->power_limits[i].step_uw = ppcc[5].integer.value;
	}

free_buffer:
	kfree(buf.pointer);

	return ret;
}

#define PROC_POWER_CAPABILITY_CHANGED	0x83
static void proc_thermal_notify(acpi_handle handle, u32 event, void *data)
{
	struct proc_thermal_device *proc_priv = data;

	if (!proc_priv)
		return;

	switch (event) {
	case PROC_POWER_CAPABILITY_CHANGED:
		proc_thermal_read_ppcc(proc_priv);
		int340x_thermal_zone_device_update(proc_priv->int340x_zone,
				THERMAL_DEVICE_POWER_CAPABILITY_CHANGED);
		break;
	default:
		dev_dbg(proc_priv->dev, "Unsupported event [0x%x]\n", event);
		break;
	}
}


static int proc_thermal_add(struct device *dev,
			    struct proc_thermal_device **priv)
{
	struct proc_thermal_device *proc_priv;
	struct acpi_device *adev;
	acpi_status status;
	unsigned long long tmp;
	struct thermal_zone_device_ops *ops = NULL;
	int ret;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	proc_priv = devm_kzalloc(dev, sizeof(*proc_priv), GFP_KERNEL);
	if (!proc_priv)
		return -ENOMEM;

	proc_priv->dev = dev;
	proc_priv->adev = adev;
	*priv = proc_priv;

	ret = proc_thermal_read_ppcc(proc_priv);
	if (ret)
		return ret;

	status = acpi_evaluate_integer(adev->handle, "_TMP", NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		/* there is no _TMP method, add local method */
		stored_tjmax = get_tjmax();
		if (stored_tjmax > 0)
			ops = &proc_thermal_local_ops;
	}

	proc_priv->int340x_zone = int340x_thermal_zone_add(adev, ops);
	if (IS_ERR(proc_priv->int340x_zone)) {
		return PTR_ERR(proc_priv->int340x_zone);
	} else
		ret = 0;

	ret = acpi_install_notify_handler(adev->handle, ACPI_DEVICE_NOTIFY,
					  proc_thermal_notify,
					  (void *)proc_priv);
	if (ret)
		goto remove_zone;

	return 0;

remove_zone:
	int340x_thermal_zone_remove(proc_priv->int340x_zone);

	return ret;
}

static void proc_thermal_remove(struct proc_thermal_device *proc_priv)
{
	acpi_remove_notify_handler(proc_priv->adev->handle,
				   ACPI_DEVICE_NOTIFY, proc_thermal_notify);
	int340x_thermal_zone_remove(proc_priv->int340x_zone);
	sysfs_remove_file(&proc_priv->dev->kobj, &dev_attr_tcc_offset_degree_celsius.attr);
	sysfs_remove_group(&proc_priv->dev->kobj,
			   &power_limit_attribute_group);
}

static int int3401_add(struct platform_device *pdev)
{
	struct proc_thermal_device *proc_priv;
	int ret;

	if (proc_thermal_emum_mode == PROC_THERMAL_PCI) {
		dev_err(&pdev->dev, "error: enumerated as PCI dev\n");
		return -ENODEV;
	}

	ret = proc_thermal_add(&pdev->dev, &proc_priv);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, proc_priv);
	proc_thermal_emum_mode = PROC_THERMAL_PLATFORM_DEV;

	dev_info(&pdev->dev, "Creating sysfs group for PROC_THERMAL_PLATFORM_DEV\n");

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_tcc_offset_degree_celsius.attr);
	if (ret)
		return ret;

	ret = sysfs_create_group(&pdev->dev.kobj, &power_limit_attribute_group);
	if (ret)
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_tcc_offset_degree_celsius.attr);

	return ret;
}

static int int3401_remove(struct platform_device *pdev)
{
	proc_thermal_remove(platform_get_drvdata(pdev));

	return 0;
}

static irqreturn_t proc_thermal_pci_msi_irq(int irq, void *devid)
{
	struct proc_thermal_device *proc_priv;
	struct pci_dev *pdev = devid;

	proc_priv = pci_get_drvdata(pdev);

	intel_soc_dts_iosf_interrupt_handler(proc_priv->soc_dts);

	return IRQ_HANDLED;
}

#ifdef CONFIG_PROC_THERMAL_MMIO_RAPL

#define MCHBAR 0

/* RAPL Support via MMIO interface */
static struct rapl_if_priv rapl_mmio_priv;

static int rapl_mmio_cpu_online(unsigned int cpu)
{
	struct rapl_package *rp;

	/* mmio rapl supports package 0 only for now */
	if (topology_physical_package_id(cpu))
		return 0;

	rp = rapl_find_package_domain(cpu, &rapl_mmio_priv);
	if (!rp) {
		rp = rapl_add_package(cpu, &rapl_mmio_priv);
		if (IS_ERR(rp))
			return PTR_ERR(rp);
	}
	cpumask_set_cpu(cpu, &rp->cpumask);
	return 0;
}

static int rapl_mmio_cpu_down_prep(unsigned int cpu)
{
	struct rapl_package *rp;
	int lead_cpu;

	rp = rapl_find_package_domain(cpu, &rapl_mmio_priv);
	if (!rp)
		return 0;

	cpumask_clear_cpu(cpu, &rp->cpumask);
	lead_cpu = cpumask_first(&rp->cpumask);
	if (lead_cpu >= nr_cpu_ids)
		rapl_remove_package(rp);
	else if (rp->lead_cpu == cpu)
		rp->lead_cpu = lead_cpu;
	return 0;
}

static int rapl_mmio_read_raw(int cpu, struct reg_action *ra)
{
	if (!ra->reg)
		return -EINVAL;

	ra->value = readq((void __iomem *)ra->reg);
	ra->value &= ra->mask;
	return 0;
}

static int rapl_mmio_write_raw(int cpu, struct reg_action *ra)
{
	u64 val;

	if (!ra->reg)
		return -EINVAL;

	val = readq((void __iomem *)ra->reg);
	val &= ~ra->mask;
	val |= ra->value;
	writeq(val, (void __iomem *)ra->reg);
	return 0;
}

static int proc_thermal_rapl_add(struct pci_dev *pdev,
				 struct proc_thermal_device *proc_priv,
				 struct rapl_mmio_regs *rapl_regs)
{
	enum rapl_domain_reg_id reg;
	enum rapl_domain_type domain;
	int ret;

	if (!rapl_regs)
		return 0;

	ret = pcim_iomap_regions(pdev, 1 << MCHBAR, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "cannot reserve PCI memory region\n");
		return -ENOMEM;
	}

	proc_priv->mmio_base = pcim_iomap_table(pdev)[MCHBAR];

	for (domain = RAPL_DOMAIN_PACKAGE; domain < RAPL_DOMAIN_MAX; domain++) {
		for (reg = RAPL_DOMAIN_REG_LIMIT; reg < RAPL_DOMAIN_REG_MAX; reg++)
			if (rapl_regs->regs[domain][reg])
				rapl_mmio_priv.regs[domain][reg] =
						(u64)proc_priv->mmio_base +
						rapl_regs->regs[domain][reg];
		rapl_mmio_priv.limits[domain] = rapl_regs->limits[domain];
	}
	rapl_mmio_priv.reg_unit = (u64)proc_priv->mmio_base + rapl_regs->reg_unit;

	rapl_mmio_priv.read_raw = rapl_mmio_read_raw;
	rapl_mmio_priv.write_raw = rapl_mmio_write_raw;

	rapl_mmio_priv.control_type = powercap_register_control_type(NULL, "intel-rapl-mmio", NULL);
	if (IS_ERR(rapl_mmio_priv.control_type)) {
		pr_debug("failed to register powercap control_type.\n");
		return PTR_ERR(rapl_mmio_priv.control_type);
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "powercap/rapl:online",
				rapl_mmio_cpu_online, rapl_mmio_cpu_down_prep);
	if (ret < 0) {
		powercap_unregister_control_type(rapl_mmio_priv.control_type);
		rapl_mmio_priv.control_type = NULL;
		return ret;
	}
	rapl_mmio_priv.pcap_rapl_online = ret;

	return 0;
}

static void proc_thermal_rapl_remove(void)
{
	if (IS_ERR_OR_NULL(rapl_mmio_priv.control_type))
		return;

	cpuhp_remove_state(rapl_mmio_priv.pcap_rapl_online);
	powercap_unregister_control_type(rapl_mmio_priv.control_type);
}

static const struct rapl_mmio_regs rapl_mmio_hsw = {
	.reg_unit = 0x5938,
	.regs[RAPL_DOMAIN_PACKAGE] = { 0x59a0, 0x593c, 0x58f0, 0, 0x5930},
	.regs[RAPL_DOMAIN_DRAM] = { 0x58e0, 0x58e8, 0x58ec, 0, 0},
	.limits[RAPL_DOMAIN_PACKAGE] = 2,
	.limits[RAPL_DOMAIN_DRAM] = 2,
};

#else

static int proc_thermal_rapl_add(struct pci_dev *pdev,
				 struct proc_thermal_device *proc_priv,
				 struct rapl_mmio_regs *rapl_regs)
{
	return 0;
}
static void proc_thermal_rapl_remove(void) {}
static const struct rapl_mmio_regs rapl_mmio_hsw;

#endif /* CONFIG_MMIO_RAPL */

static int  proc_thermal_pci_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct proc_thermal_device *proc_priv;
	int ret;

	if (proc_thermal_emum_mode == PROC_THERMAL_PLATFORM_DEV) {
		dev_err(&pdev->dev, "error: enumerated as platform dev\n");
		return -ENODEV;
	}

	ret = pcim_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "error: could not enable device\n");
		return ret;
	}

	ret = proc_thermal_add(&pdev->dev, &proc_priv);
	if (ret)
		return ret;

	ret = proc_thermal_rapl_add(pdev, proc_priv,
				(struct rapl_mmio_regs *)id->driver_data);
	if (ret) {
		dev_err(&pdev->dev, "failed to add RAPL MMIO interface\n");
		proc_thermal_remove(proc_priv);
		return ret;
	}

	pci_set_drvdata(pdev, proc_priv);
	proc_thermal_emum_mode = PROC_THERMAL_PCI;

	if (pdev->device == PCI_DEVICE_ID_PROC_BSW_THERMAL) {
		/*
		 * Enumerate additional DTS sensors available via IOSF.
		 * But we are not treating as a failure condition, if
		 * there are no aux DTSs enabled or fails. This driver
		 * already exposes sensors, which can be accessed via
		 * ACPI/MSR. So we don't want to fail for auxiliary DTSs.
		 */
		proc_priv->soc_dts = intel_soc_dts_iosf_init(
					INTEL_SOC_DTS_INTERRUPT_MSI, 2, 0);

		if (!IS_ERR(proc_priv->soc_dts) && pdev->irq) {
			ret = pci_enable_msi(pdev);
			if (!ret) {
				ret = request_threaded_irq(pdev->irq, NULL,
						proc_thermal_pci_msi_irq,
						IRQF_ONESHOT, "proc_thermal",
						pdev);
				if (ret) {
					intel_soc_dts_iosf_exit(
							proc_priv->soc_dts);
					pci_disable_msi(pdev);
					proc_priv->soc_dts = NULL;
				}
			}
		} else
			dev_err(&pdev->dev, "No auxiliary DTSs enabled\n");
	}

	dev_info(&pdev->dev, "Creating sysfs group for PROC_THERMAL_PCI\n");

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_tcc_offset_degree_celsius.attr);
	if (ret)
		return ret;

	ret = sysfs_create_group(&pdev->dev.kobj, &power_limit_attribute_group);
	if (ret)
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_tcc_offset_degree_celsius.attr);

	return ret;
}

static void  proc_thermal_pci_remove(struct pci_dev *pdev)
{
	struct proc_thermal_device *proc_priv = pci_get_drvdata(pdev);

	if (proc_priv->soc_dts) {
		intel_soc_dts_iosf_exit(proc_priv->soc_dts);
		if (pdev->irq) {
			free_irq(pdev->irq, pdev);
			pci_disable_msi(pdev);
		}
	}
	proc_thermal_rapl_remove();
	proc_thermal_remove(proc_priv);
}

#ifdef CONFIG_PM_SLEEP
static int proc_thermal_resume(struct device *dev)
{
	struct proc_thermal_device *proc_dev;

	proc_dev = dev_get_drvdata(dev);
	proc_thermal_read_ppcc(proc_dev);

	tcc_offset_update(tcc_offset_save);

	return 0;
}
#else
#define proc_thermal_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(proc_thermal_pm, NULL, proc_thermal_resume);

static const struct pci_device_id proc_thermal_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_BDW_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_HSB_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_SKL_THERMAL),
		.driver_data = (kernel_ulong_t)&rapl_mmio_hsw, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_BSW_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_BXT0_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_BXT1_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_BXTX_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_BXTP_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_CNL_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_CFL_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_GLK_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_ICL_THERMAL),
		.driver_data = (kernel_ulong_t)&rapl_mmio_hsw, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_JSL_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_TGL_THERMAL),
		.driver_data = (kernel_ulong_t)&rapl_mmio_hsw, },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, proc_thermal_pci_ids);

static struct pci_driver proc_thermal_pci_driver = {
	.name		= DRV_NAME,
	.probe		= proc_thermal_pci_probe,
	.remove		= proc_thermal_pci_remove,
	.id_table	= proc_thermal_pci_ids,
	.driver.pm	= &proc_thermal_pm,
};

static const struct acpi_device_id int3401_device_ids[] = {
	{"INT3401", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, int3401_device_ids);

static struct platform_driver int3401_driver = {
	.probe = int3401_add,
	.remove = int3401_remove,
	.driver = {
		.name = "int3401 thermal",
		.acpi_match_table = int3401_device_ids,
		.pm = &proc_thermal_pm,
	},
};

static int __init proc_thermal_init(void)
{
	int ret;

	ret = platform_driver_register(&int3401_driver);
	if (ret)
		return ret;

	ret = pci_register_driver(&proc_thermal_pci_driver);

	return ret;
}

static void __exit proc_thermal_exit(void)
{
	platform_driver_unregister(&int3401_driver);
	pci_unregister_driver(&proc_thermal_pci_driver);
}

module_init(proc_thermal_init);
module_exit(proc_thermal_exit);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Processor Thermal Reporting Device Driver");
MODULE_LICENSE("GPL v2");
