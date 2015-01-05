#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpufreq_ondemand

#if !defined(_TRACE_CPUFREQ_ONDEMEND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CPUFREQ_ONDEMAND_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(od_sample,
	TP_PROTO(u32 cpu_id, unsigned int cur_load, unsigned int prev_freq,
		unsigned int max_load_freq, unsigned int load_at_max_freq,
		unsigned int max_load_other_cpu, unsigned int new_freq),
	TP_ARGS(cpu_id, cur_load, prev_freq, max_load_freq,
		load_at_max_freq, max_load_other_cpu, new_freq),

	TP_STRUCT__entry(
		__field(          u32, cpu_id    )
		__field(unsigned int, cur_load)
		__field(unsigned int, prev_freq)
		__field(unsigned int, max_load_freq)
		__field(unsigned int, load_at_max_freq)
		__field(unsigned int, max_load_other_cpu)
		__field(unsigned int, new_freq)
	),

	TP_fast_assign(
		__entry->cpu_id = (u32) cpu_id;
		__entry->cur_load = cur_load;
		__entry->prev_freq = prev_freq;
		__entry->max_load_freq = max_load_freq;
		__entry->load_at_max_freq = load_at_max_freq;
		__entry->max_load_other_cpu = max_load_other_cpu;
		__entry->new_freq = new_freq;
	),

	TP_printk("cpu=%u load=%u prev=%u maxloadfreq=%u maxfreqload=%u"
		"oload=%u next=%u", __entry->cpu_id, __entry->cur_load,
		__entry->prev_freq, __entry->max_load_freq,
		__entry->load_at_max_freq, __entry->max_load_other_cpu,
		__entry->new_freq)
);

DEFINE_EVENT(od_sample, cpufreq_ondemand_raise,
	TP_PROTO(u32 cpu_id, unsigned int cur_load, unsigned int prev_freq,
		unsigned int max_load_freq, unsigned int load_at_max_freq,
		unsigned int max_load_other_cpu, unsigned int new_freq),
	TP_ARGS(cpu_id, cur_load, prev_freq, max_load_freq,
		load_at_max_freq, max_load_other_cpu, new_freq)
);

DEFINE_EVENT(od_sample, cpufreq_ondemand_sync,
	TP_PROTO(u32 cpu_id, unsigned int cur_load, unsigned int prev_freq,
		unsigned int max_load_freq, unsigned int load_at_max_freq,
		unsigned int max_load_other_cpu, unsigned int new_freq),
	TP_ARGS(cpu_id, cur_load, prev_freq, max_load_freq,
		load_at_max_freq, max_load_other_cpu, new_freq)
);

DEFINE_EVENT(od_sample, cpufreq_ondemand_opt,
	TP_PROTO(u32 cpu_id, unsigned int cur_load, unsigned int prev_freq,
		unsigned int max_load_freq, unsigned int load_at_max_freq,
		unsigned int max_load_other_cpu, unsigned int new_freq),
	TP_ARGS(cpu_id, cur_load, prev_freq, max_load_freq,
		load_at_max_freq, max_load_other_cpu, new_freq)
);

DECLARE_EVENT_CLASS(od_nochange,
	TP_PROTO(unsigned int cpu_id, unsigned int cur_load,
		unsigned int freq, unsigned int max_load_other_cpu),
	TP_ARGS(cpu_id, cur_load, freq, max_load_other_cpu),

	TP_STRUCT__entry(
		__field(unsigned int, cpu_id	)
		__field(unsigned int, cur_load	)
		__field(unsigned int, freq	)
		__field(unsigned int, max_load_other_cpu )
	),

	TP_fast_assign(
		__entry->cpu_id = cpu_id;
		__entry->cur_load = cur_load;
		__entry->freq = freq;
		__entry->max_load_other_cpu = max_load_other_cpu;
	),

	TP_printk("cpu=%u load=%u freq=%u oload=%u",
		__entry->cpu_id, __entry->cur_load,
		__entry->freq, __entry->max_load_other_cpu)
);

DEFINE_EVENT(od_nochange, cpufreq_ondemand_nochange,
	TP_PROTO(unsigned int cpu_id, unsigned int cur_load,
		unsigned int freq, unsigned int max_load_other_cpu),
	TP_ARGS(cpu_id, cur_load, freq, max_load_other_cpu)
);

DECLARE_EVENT_CLASS(od_down_sample,
	TP_PROTO(unsigned int cpu_id, unsigned int cur_load,
		unsigned int prev_freq, unsigned int target_freq,
		unsigned int next_freq, unsigned int mc_sync,
		unsigned int mc_optimal, unsigned int max_load_other_cpu),
	TP_ARGS(cpu_id, cur_load, prev_freq, target_freq, next_freq,
		mc_sync, mc_optimal, max_load_other_cpu),

	TP_STRUCT__entry(
		__field(unsigned int, cpu_id	)
		__field(unsigned int, cur_load	)
		__field(unsigned int, prev_freq	)
		__field(unsigned int, target_freq )
		__field(unsigned int, next_freq	)
		__field(unsigned int, mc_sync )
		__field(unsigned int, mc_optimal )
		__field(unsigned int, max_load_other_cpu )
	),

	TP_fast_assign(
		__entry->cpu_id = cpu_id;
		__entry->cur_load = cur_load;
		__entry->prev_freq = prev_freq;
		__entry->target_freq = target_freq;
		__entry->next_freq = next_freq;
		__entry->mc_sync = mc_sync;
		__entry->mc_optimal = mc_optimal;
		__entry->max_load_other_cpu = max_load_other_cpu;
	),

	TP_printk("cpu=%u load=%u freq=%u target=%u next=%u mcs=%u"
		"mco=%u oload=%u", __entry->cpu_id, __entry->cur_load,
		__entry->prev_freq, __entry->target_freq, __entry->next_freq,
		__entry->mc_sync, __entry->mc_optimal,
		__entry->max_load_other_cpu)
);

DEFINE_EVENT(od_down_sample, cpufreq_ondemand_down,
	TP_PROTO(unsigned int cpu_id, unsigned int cur_load,
		unsigned int prev_freq, unsigned int target_freq,
		unsigned int next_freq,	unsigned int mc_sync,
		unsigned int mc_optimal, unsigned int max_load_other_cpu),
	TP_ARGS(cpu_id, cur_load, prev_freq, target_freq, next_freq,
		mc_sync, mc_optimal, max_load_other_cpu)
);

DECLARE_EVENT_CLASS(od_migration_sync,
	TP_PROTO(unsigned int cpu_id, unsigned int src_cpu,
		unsigned int cur_freq, unsigned int src_freq,
		unsigned int src_max_load),
	TP_ARGS(cpu_id, src_cpu, cur_freq, src_freq, src_max_load),

	TP_STRUCT__entry(
		__field(unsigned int, cpu_id	)
		__field(unsigned int, src_cpu	)
		__field(unsigned int, cur_freq	)
		__field(unsigned int, src_freq	)
		__field(unsigned int, src_max_load )
	),

	TP_fast_assign(
		__entry->cpu_id = cpu_id;
		__entry->src_cpu = src_cpu;
		__entry->cur_freq = cur_freq;
		__entry->src_freq = src_freq;
		__entry->src_max_load = src_max_load;
	),

	TP_printk("cpu=%u src_cpu=%u cur_freq=%u src_freq=%u src_max_load=%u",
		__entry->cpu_id, __entry->src_cpu, __entry->cur_freq,
		__entry->src_freq, __entry->src_max_load)
);

DEFINE_EVENT(od_migration_sync, cpufreq_ondemand_migration_sync,
	TP_PROTO(unsigned int cpu_id, unsigned int src_cpu,
		unsigned int cur_freq, unsigned int src_freq,
		unsigned int src_max_load),
	TP_ARGS(cpu_id, src_cpu, cur_freq, src_freq, src_max_load)
);

DECLARE_EVENT_CLASS(od_migration_fail,
	TP_PROTO(unsigned int cpu_id, unsigned int src_cpu,
		unsigned int cur_freq, unsigned int src_freq),
	TP_ARGS(cpu_id, src_cpu, cur_freq, src_freq),

	TP_STRUCT__entry(
		__field(unsigned int, cpu_id	)
		__field(unsigned int, src_cpu	)
		__field(unsigned int, cur_freq	)
		__field(unsigned int, src_freq	)
	),

	TP_fast_assign(
		__entry->cpu_id = cpu_id;
		__entry->src_cpu = src_cpu;
		__entry->cur_freq = cur_freq;
		__entry->src_freq = src_freq;
	),

	TP_printk("cpu=%u src_cpu=%u cur_freq=%u src_freq=%u",
		__entry->cpu_id, __entry->src_cpu, __entry->cur_freq,
		__entry->src_freq)

);

DEFINE_EVENT(od_migration_fail, cpufreq_ondemand_mig_fail_sema,
	TP_PROTO(unsigned int cpu_id, unsigned int src_cpu,
		unsigned int cur_freq, unsigned int src_freq),
	TP_ARGS(cpu_id, src_cpu, cur_freq, src_freq)
);

DEFINE_EVENT(od_migration_fail, cpufreq_ondemand_mig_fail,
	TP_PROTO(unsigned int cpu_id, unsigned int src_cpu,
		unsigned int cur_freq, unsigned int src_freq),
	TP_ARGS(cpu_id, src_cpu, cur_freq, src_freq)
);

#endif /* _TRACE_CPUFREQ_ONDEMAND_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
