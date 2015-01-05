/*
 *  drivers/cpufreq/cpufreq_stats.c
 *
 *  Copyright (C) 2003-2004 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *  (C) 2004 Zou Nan hai <nanhai.zou@intel.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sysfs.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/percpu.h>
#include <linux/kobject.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <asm/cputime.h>

static spinlock_t cpufreq_stats_lock;

#define CPUFREQ_STATDEVICE_ATTR(_name, _mode, _show) \
static struct freq_attr _attr_##_name = {\
	.attr = {.name = __stringify(_name), .mode = _mode, }, \
	.show = _show,\
};

struct cpufreq_stats {
	unsigned int cpu;
	unsigned int total_trans;
	unsigned long long  last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	cputime64_t *time_in_state;
	unsigned int *freq_table;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	unsigned int *trans_table;
#endif
};

static DEFINE_PER_CPU(struct cpufreq_stats *, cpufreq_stats_table);

struct cpufreq_stats_attribute {
	struct attribute attr;
	ssize_t(*show) (struct cpufreq_stats *, char *);
};

#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT

#define PERSISTENT_STATS_ATTR(_name)		\
static struct global_attr persist_##_name =	\
__ATTR(_name, 0444, show_persist_##_name, NULL)

struct persist_cpufreq_stats {
	unsigned int cpu;
	unsigned long long total_trans;
	unsigned int max_state;
	unsigned int state_num;
	cputime64_t *time_in_state;
	unsigned int *freq_table;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	unsigned int *trans_table;
#endif
	struct list_head link;
};

LIST_HEAD(persist_per_cpu_stats);
static spinlock_t persist_stats_lock;

static int cpufreq_stats_update(unsigned int cput);

#define for_each_persist_stat(pstat)		\
	list_for_each_entry(pstat, &persist_per_cpu_stats, link)

#define for_each_persist_stat_safe(pstat, tmp)	\
	list_for_each_entry_safe(pstat, tmp, &persist_per_cpu_stats, link)

static struct persist_cpufreq_stats *get_persist_stat(unsigned int cpu)
{
	struct persist_cpufreq_stats *itr;
	struct persist_cpufreq_stats *pstat = NULL;

	spin_lock(&persist_stats_lock);
	if (list_empty(&persist_per_cpu_stats))
		goto out_unlock;
	for_each_persist_stat(itr) {
		if (itr->cpu == cpu) {
			pstat = itr;
			break;
		}
	}
out_unlock:
	spin_unlock(&persist_stats_lock);
	return pstat;
}

static void remove_persist_stat(struct persist_cpufreq_stats *pstat)
{
	struct persist_cpufreq_stats *itr, *tmp;

	spin_lock(&persist_stats_lock);
	BUG_ON(list_empty(&persist_per_cpu_stats));
	for_each_persist_stat_safe(itr, tmp) {
		if (itr == pstat) {
			list_del(&itr->link);
			kfree(itr->time_in_state);
			kfree(itr);
			break;
		}
	}
	spin_unlock(&persist_stats_lock);
}

static void remove_persist_stats_all(void)
{
	struct persist_cpufreq_stats *itr, *tmp;

	spin_lock(&persist_stats_lock);
	if (list_empty(&persist_per_cpu_stats))
		goto out_unlock;

	for_each_persist_stat_safe(itr, tmp) {
		list_del(&itr->link);
		kfree(itr->time_in_state);
		kfree(itr);
	}
out_unlock:
	spin_unlock(&persist_stats_lock);
	return;
}

static void add_persist_stat(struct persist_cpufreq_stats *pstat)
{

	if (!pstat)
		return;

	/*
	 * FIXME: keep this bug_on for a while
	 * Remove later when enough testing is done.
	 */
	BUG_ON(get_persist_stat(pstat->cpu));
	spin_lock(&persist_stats_lock);
	list_add_tail(&pstat->link, &persist_per_cpu_stats);
	spin_unlock(&persist_stats_lock);
}

static void persist_cpufreq_update_trans(struct cpufreq_stats *stat)
{
	struct persist_cpufreq_stats *pstat;

	if (!stat || !cpu_online(stat->cpu))
		goto out;

	pstat = get_persist_stat(stat->cpu);
	/*
	 * FIXME: keep this bug_on for a while
	 * Remove later when enough testing is done.
	 */
	BUG_ON(!pstat);
	pstat->total_trans++;
out:
	return;

}

static void persist_cpufreq_stats_update(struct cpufreq_stats *stat,
				unsigned long long delta)
{
	struct persist_cpufreq_stats *pstat;

	if (!stat || !cpu_online(stat->cpu))
		goto out;

	pstat = get_persist_stat(stat->cpu);
	/*
	 * FIXME: keep this bug_on for a while
	 * Remove later when enough testing is done.
	 */
	BUG_ON(!pstat);
	pstat->time_in_state[stat->last_index] += delta;
out:
	return;
}
#endif /* CPUF_FREQ_STAT_PERSISTENT */

static int cpufreq_stats_update(unsigned int cpu)
{
	struct cpufreq_stats *stat;
	unsigned long long cur_time, delta;

	cur_time = get_jiffies_64();
	spin_lock(&cpufreq_stats_lock);
	stat = per_cpu(cpufreq_stats_table, cpu);
	if (stat->time_in_state) {
		delta = cur_time - stat->last_time;
		stat->time_in_state[stat->last_index] += delta;
#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
		persist_cpufreq_stats_update(stat, delta);
#endif
	}
	stat->last_time = cur_time;
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static ssize_t show_total_trans(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	return sprintf(buf, "%d\n",
			per_cpu(cpufreq_stats_table, stat->cpu)->total_trans);
}

static ssize_t show_time_in_state(struct cpufreq_policy *policy, char *buf)
{
	ssize_t len = 0;
	int i;
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	cpufreq_stats_update(stat->cpu);
	for (i = 0; i < stat->state_num; i++) {
		len += sprintf(buf + len, "%u %llu\n", stat->freq_table[i],
			(unsigned long long)
			cputime64_to_clock_t(stat->time_in_state[i]));
	}
	return len;
}

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
static ssize_t show_trans_table(struct cpufreq_policy *policy, char *buf)
{
	ssize_t len = 0;
	int i, j;

	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	cpufreq_stats_update(stat->cpu);
	len += snprintf(buf + len, PAGE_SIZE - len, "   From  :    To\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "         : ");
	for (i = 0; i < stat->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
				stat->freq_table[i]);
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	for (i = 0; i < stat->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;

		len += snprintf(buf + len, PAGE_SIZE - len, "%9u: ",
				stat->freq_table[i]);

		for (j = 0; j < stat->state_num; j++)   {
			if (len >= PAGE_SIZE)
				break;
			len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
					stat->trans_table[i*stat->max_state+j]);
		}
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;
	return len;
}
CPUFREQ_STATDEVICE_ATTR(trans_table, 0444, show_trans_table);
#endif

CPUFREQ_STATDEVICE_ATTR(total_trans, 0444, show_total_trans);
CPUFREQ_STATDEVICE_ATTR(time_in_state, 0444, show_time_in_state);

static struct attribute *default_attrs[] = {
	&_attr_total_trans.attr,
	&_attr_time_in_state.attr,
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	&_attr_trans_table.attr,
#endif
	NULL
};
static struct attribute_group stats_attr_group = {
	.attrs = default_attrs,
	.name = "stats"
};

#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
static ssize_t show_persist_total_trans(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	struct persist_cpufreq_stats *pstat;
	ssize_t len = 0;

	for_each_persist_stat(pstat) {
		len += sprintf(buf + len, "cpu%d: ", pstat->cpu);
		len += sprintf(buf + len, "%lld\n", pstat->total_trans);
	}

	return len;
}

static ssize_t show_persist_time_in_state(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	struct persist_cpufreq_stats *pstat;
	struct persist_cpufreq_stats *p;
	int i;
	ssize_t len = 0;

	/* Use cpu0's freq table */
	p = get_persist_stat(0);
	len += sprintf(buf + len, "%-20s", "freq");
	for_each_persist_stat(pstat) {
		len += sprintf(buf + len, "cpu%-17d", pstat->cpu);
	}
	len += sprintf(buf + len, "%s", "\n");
	for (i = 0; i < p->state_num; i++) {
		len += sprintf(buf + len, "%-20u", p->freq_table[i]);
		for_each_persist_stat(pstat) {
			len += sprintf(buf + len, "%-20llu",
				(unsigned long long)
				cputime64_to_clock_t(pstat->time_in_state[i]));
		}
		len += sprintf(buf + len, "%s", "\n");
	}
	return len;
}

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
static ssize_t show_persist_trans_table(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	/*
	 * FIXME: Implement a full persistent freq transistion table
	 */
	return sprintf(buf, "%s\n", "Not-Implemented");
}
PERSISTENT_STATS_ATTR(trans_table);
#endif

PERSISTENT_STATS_ATTR(total_trans);
PERSISTENT_STATS_ATTR(time_in_state);

static struct attribute *persist_stat_attrs[] = {
	&persist_total_trans.attr,
	&persist_time_in_state.attr,
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	&persist_trans_table.attr,
#endif
	NULL
};

static struct attribute_group persist_stats_attr_group = {
	.attrs = persist_stat_attrs,
	.name = "persist_stats",
};

static int persist_freq_table_get_index(struct persist_cpufreq_stats *pstat,
					unsigned int freq)
{
	int index;
	for (index = 0; index < pstat->max_state; index++)
		if (pstat->freq_table[index] == freq)
			return index;
	return -1;
}
#endif /* CPU_FREQ_STAT_PERSISTENT */

static int freq_table_get_index(struct cpufreq_stats *stat, unsigned int freq)
{
	int index;
	for (index = 0; index < stat->max_state; index++)
		if (stat->freq_table[index] == freq)
			return index;
	return -1;
}

/* should be called late in the CPU removal sequence so that the stats
 * memory is still available in case someone tries to use it.
 */
static void cpufreq_stats_free_table(unsigned int cpu)
{
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, cpu);
	if (stat) {
		kfree(stat->time_in_state);
		kfree(stat);
	}
	per_cpu(cpufreq_stats_table, cpu) = NULL;
}

/* must be called early in the CPU removal sequence (before
 * cpufreq_remove_dev) so that policy is still valid.
 */
static void cpufreq_stats_free_sysfs(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	if (policy && policy->cpu == cpu)
		sysfs_remove_group(&policy->kobj, &stats_attr_group);
	if (policy)
		cpufreq_cpu_put(policy);
}


static int cpufreq_stats_create_table(struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table)
{
	unsigned int i, j, count = 0, ret = 0;
	struct cpufreq_stats *stat;
	struct cpufreq_policy *data;
	unsigned int alloc_size;
	unsigned int cpu = policy->cpu;
#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
	struct persist_cpufreq_stats *pstat;
	int pstat_added = 0;
#endif

	if (per_cpu(cpufreq_stats_table, cpu))
		return -EBUSY;
	stat = kzalloc(sizeof(struct cpufreq_stats), GFP_KERNEL);
	if ((stat) == NULL)
		return -ENOMEM;

	data = cpufreq_cpu_get(cpu);
	if (data == NULL) {
		ret = -EINVAL;
		goto error_get_fail;
	}

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		count++;
	}

	alloc_size = count * sizeof(int) + count * sizeof(cputime64_t);
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	alloc_size += count * count * sizeof(int);
#endif
	stat->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
	if (!stat->time_in_state) {
		ret = -ENOMEM;
		goto error_alloc;
	}
	stat->freq_table = (unsigned int *)(stat->time_in_state + count);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stat->trans_table = stat->freq_table + count;
#endif
	stat->cpu = cpu;
	stat->max_state = count;

#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
	pstat = get_persist_stat(cpu);
	if (!pstat) {
		pstat = kzalloc(sizeof(struct persist_cpufreq_stats),
				GFP_KERNEL);
		if (pstat == NULL) {
			ret = -ENOMEM;
			pr_err("%s: failed to allocate persist stats\n",
					__func__);
			goto error_pstat_alloc;
		}
		pstat->cpu = cpu;
		pstat->max_state = count;
		pstat->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
		if (!pstat->time_in_state) {
			ret = -ENOMEM;
			pr_err("%s: failed to allocate pstat->time_in_state\n",
					__func__);
			kfree(pstat);
			goto error_pstat_alloc;
		}
		pstat->freq_table = (unsigned int *)(pstat->time_in_state +
					pstat->max_state);
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
		pstat->trans_table = pstat->freq_table + count;
#endif
		add_persist_stat(pstat);
		pstat_added = 1;
	}
#endif

	ret = sysfs_create_group(&data->kobj, &stats_attr_group);
	if (ret)
		goto error_sysfs;

	j = 0;
	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if (freq_table_get_index(stat, freq) == -1)
			stat->freq_table[j] = freq;
#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
		if (pstat_added &&
			persist_freq_table_get_index(pstat, freq) == -1)
			pstat->freq_table[j] = freq;
#endif
		j++;
	}
	stat->state_num = j;
#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
	if (pstat_added)
		pstat->state_num = j;
#endif
	spin_lock(&cpufreq_stats_lock);
	stat->last_time = get_jiffies_64();
	stat->last_index = freq_table_get_index(stat, policy->cur);
	spin_unlock(&cpufreq_stats_lock);
	per_cpu(cpufreq_stats_table, cpu) = stat;

	cpufreq_cpu_put(data);
	return 0;

error_sysfs:
#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
	if (pstat_added)
		remove_persist_stat(pstat);
error_pstat_alloc:
#endif
	kfree(stat->time_in_state);
error_alloc:
	cpufreq_cpu_put(data);
error_get_fail:
	kfree(stat);
	per_cpu(cpufreq_stats_table, cpu) = NULL;
	return ret;
}

static int cpufreq_stat_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	int ret;
	struct cpufreq_policy *policy = data;
	struct cpufreq_frequency_table *table;
	unsigned int cpu = policy->cpu;
	if (val != CPUFREQ_NOTIFY)
		return 0;
	table = cpufreq_frequency_get_table(cpu);
	if (!table)
		return 0;
	ret = cpufreq_stats_create_table(policy, table);
	if (ret)
		return ret;
	return 0;
}

static int cpufreq_stat_notifier_trans(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_stats *stat;
	int old_index, new_index;

	if (val != CPUFREQ_POSTCHANGE)
		return 0;

	stat = per_cpu(cpufreq_stats_table, freq->cpu);
	if (!stat)
		return 0;

	old_index = stat->last_index;
	new_index = freq_table_get_index(stat, freq->new);

	/* We can't do stat->time_in_state[-1]= .. */
	if (old_index == -1 || new_index == -1)
		return 0;

	cpufreq_stats_update(freq->cpu);

	if (old_index == new_index)
		return 0;

	spin_lock(&cpufreq_stats_lock);
	stat->last_index = new_index;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stat->trans_table[old_index * stat->max_state + new_index]++;
#endif
	stat->total_trans++;
#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
	persist_cpufreq_update_trans(stat);
#endif
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static int cpufreq_stats_create_table_cpu(unsigned int cpu)
{
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *table;
	int ret = -ENODEV;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -ENODEV;

	table = cpufreq_frequency_get_table(cpu);
	if (!table)
		goto out;

	ret = cpufreq_stats_create_table(policy, table);

out:
	cpufreq_cpu_put(policy);
	return ret;
}

static int __cpuinit cpufreq_stat_cpu_callback(struct notifier_block *nfb,
					       unsigned long action,
					       void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		cpufreq_update_policy(cpu);
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		cpufreq_stats_free_sysfs(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		cpufreq_stats_free_table(cpu);
		break;
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		cpufreq_stats_create_table_cpu(cpu);
		break;
	}
	return NOTIFY_OK;
}

/* priority=1 so this will get called before cpufreq_remove_dev */
static struct notifier_block cpufreq_stat_cpu_notifier __refdata = {
	.notifier_call = cpufreq_stat_cpu_callback,
	.priority = 1,
};

static struct notifier_block notifier_policy_block = {
	.notifier_call = cpufreq_stat_notifier_policy
};

static struct notifier_block notifier_trans_block = {
	.notifier_call = cpufreq_stat_notifier_trans
};

static int __init cpufreq_stats_init(void)
{
	int ret;
	unsigned int cpu;

	spin_lock_init(&cpufreq_stats_lock);
#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
	spin_lock_init(&persist_stats_lock);
#endif
	ret = cpufreq_register_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		return ret;

	ret = cpufreq_register_notifier(&notifier_trans_block,
				CPUFREQ_TRANSITION_NOTIFIER);
	if (ret)
		goto error_trans_notifier;

#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
	ret = sysfs_create_group(cpufreq_global_kobject,
			&persist_stats_attr_group);
	if (ret)
		goto error_sysfs_group;
#endif

	register_hotcpu_notifier(&cpufreq_stat_cpu_notifier);
	for_each_online_cpu(cpu) {
		cpufreq_update_policy(cpu);
	}

	return 0;

#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
error_sysfs_group:
	cpufreq_unregister_notifier(&notifier_trans_block,
			CPUFREQ_TRANSITION_NOTIFIER);
#endif
error_trans_notifier:
	cpufreq_unregister_notifier(&notifier_policy_block,
			CPUFREQ_POLICY_NOTIFIER);
	return ret;
}

static void __exit cpufreq_stats_exit(void)
{
	unsigned int cpu;

#ifdef CONFIG_CPU_FREQ_STAT_PERSISTENT
	sysfs_remove_group(cpufreq_global_kobject,
				&persist_stats_attr_group);
	/* Remove persist stats for cpus that are *NOT* online */
	remove_persist_stats_all();
#endif
	cpufreq_unregister_notifier(&notifier_policy_block,
			CPUFREQ_POLICY_NOTIFIER);
	cpufreq_unregister_notifier(&notifier_trans_block,
			CPUFREQ_TRANSITION_NOTIFIER);
	unregister_hotcpu_notifier(&cpufreq_stat_cpu_notifier);
	for_each_online_cpu(cpu) {
		cpufreq_stats_free_table(cpu);
		cpufreq_stats_free_sysfs(cpu);
	}

}

MODULE_AUTHOR("Zou Nan hai <nanhai.zou@intel.com>");
MODULE_DESCRIPTION("'cpufreq_stats' - A driver to export cpufreq stats "
				"through sysfs filesystem");
MODULE_LICENSE("GPL");

module_init(cpufreq_stats_init);
module_exit(cpufreq_stats_exit);
