#ifndef _MSM_THERMAL_IOCTL_H
#define _MSM_THERMAL_IOCTL_H

#include <linux/ioctl.h>

#define MSM_THERMAL_IOCTL_FILE "msm_thermal_query"

struct cpu_freq_arg {
	int cpu_num;
	uint32_t freq_req;
};

struct msm_thermal_ioctl {
	uint32_t size;
	union {
		struct cpu_freq_arg cpu_freq;
	};
};

enum {
	/*Set CPU Frequency*/
	SET_CPU_MAX_FREQ = 0x00,
	SET_CPU_MIN_FREQ = 0x01,

	MSM_THERMAL_CMD_MAX_NR,
};

#define MSM_THERMAL_MAGIC_NUM 0xCA /*Unique magic number*/

#define MSM_THERMAL_SET_CPU_MAX_FREQUENCY _IOW(MSM_THERMAL_MAGIC_NUM,\
		SET_CPU_MAX_FREQ, struct msm_thermal_ioctl)

#define MSM_THERMAL_SET_CPU_MIN_FREQUENCY _IOW(MSM_THERMAL_MAGIC_NUM,\
		SET_CPU_MIN_FREQ, struct msm_thermal_ioctl)

#endif
