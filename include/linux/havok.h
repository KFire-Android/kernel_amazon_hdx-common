/**
 * Havok
 *
 */


#ifndef _LINUX_HAVOK_H
#define _LINUX_HAVOK_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/ioctl.h>

enum hv_data_i {
	HV_WIFI,
	HV_WAN
};

#ifdef __KERNEL__
long sys_hv(int type, int category, int id, int extra);
void hv_check_cpu_log(unsigned int cpu);

int hv(int type, int category, int id, int extra);
void hv_add_name(unsigned int t, pid_t pid, char *buf);
void hv_rename(unsigned int t, pid_t pid, char *buf);
int hv_time_offset(struct timespec *d);
void hv_dump_data(enum hv_data_i interface, int direction, int bytes);
void hv_log_proc(struct task_struct *oldt, struct task_struct *newt);
void hv_log_panel_state(int state);
void hv_log_suspend(void);
void hv_log_resume(void);
void hv_log_cpu_clk(unsigned int cpu, int new_freq);
void hv_log_gpu_pwr(unsigned int active, unsigned int total, unsigned int level);
void hv_log_gpu_rail(int state);
#endif /* __KERNEL__ */

#define HV_DEV_NAME            "hv"
#define HV_DUMP_DEV_NAME       "hvd"
#define HV_CTRL_DEV_NAME       "hvc"

/* Internal Havok log entry type (Only used within driver)
*/
struct hv_entry {
	struct timespec tv;         /* time stamp			*/
	int sequence;               /* assigned sequentially		*/
	short type;                 /* internal type of event           */
	short category;             /* category				*/
	int event_id;               /* event id				*/
	int extra;                  /* entry specific extra info	*/
};

/* Havok entry as read from device node
*/
struct havok_entry_t {
	union {
		struct {
			unsigned char ctrl;
			int counter;
			unsigned char extra_1;
			unsigned char extra_2;
			unsigned char cpu;
			unsigned char comp_trace_id[3];
			struct timespec ts;
};
		struct {
			unsigned char ctrl_e;
			unsigned char counter_e;
			unsigned char format;
			unsigned char unused;
			unsigned int extras[4];
		};
	};
};

/* macros for splitting out fields
 */
#define HAVOK_LEVEL_OFFSET     30
#define HAVOK_FLAG_OFFSET      22
#define HAVOK_TRACE_ID_OFFSET   7
#define HAVOK_STRACE_ID_OFFSET  0

#define HAVOK_LEVEL_MASK        0xc0000000
#define HAVOK_FLAG_MASK         0x3fc00000
#define HAVOK_TRACE_ID_MASK     0x3fff80
#define HAVOK_STRACE_ID_MASK    0x7f

#define HAVOK_GET_LEVEL(x) ((x & HAVOK_LEVEL_MASK) >> HAVOK_LEVEL_OFFSET)
#define HAVOK_GET_FLAGS(x) ((x & HAVOK_FLAG_MASK) >> HAVOK_FLAG_OFFSET)
#define HAVOK_GET_TRACE_ID(x) ((x & HAVOK_TRACE_ID_MASK) >> \
				HAVOK_TRACE_ID_OFFSET)
#define HAVOK_GET_STRACE_ID(x) ((x & HAVOK_STRACE_ID_MASK) >> \
				HAVOK_STRACE_ID_OFFSET)

#define HAVOK_LEVEL(x) (x << HAVOK_LEVEL_OFFSET)
#define HAVOK_FLAGS(x) (x << HAVOK_FLAG_OFFSET)
#define HAVOK_TRACE_ID(x) (x << HAVOK_TRACE_ID_OFFSET)
#define HAVOK_STRACE_ID(x) (x << HAVOK_STRACE_ID_OFFSET)

#define HV_GET_TYPE(e) (e.comp_trace_id[2])

#ifdef __KERNEL__
#ifdef CONFIG_HAVOK
#define HV_PWR(level, cat, id, extra) hv(HV_PWR_TYPE, cat, id, extra);
#define HV_ACTION(level, cat, id, extra) hv(HV_ACT_TYPE, cat, id, extra);
#define HV_WAKE(level, cat, id, extra) hv(HV_WAKE_TYPE, cat, id, extra);
#define HV_CNTL(level, cat, id, extra) hv(HV_CNTL_TYPE, cat, id, extra);

#define HV_ADD_NAME(t, pid, buf) hv_add_name(t, pid, buf)
#define HV_RENAME(t, pid, buf) hv_rename(t, pid, buf)
#define HV_DUMP_DATA(i, dir, bytes) hv_dump_data(i, dir, bytes)
#define HV_LOG_PANEL_STATE(state) hv_log_panel_state(state)
#define HV_TIME_OFFSET(d) hv_time_offset(d)
#define HV_LOG_CPU_CLK(cpu, t_idx) hv_log_cpu_clk(cpu, t_idx)
#define HV_LOG_GPU_PWR(a, t, l) hv_log_gpu_pwr(a, t, l)
#define HV_LOG_GPU_RAIL(s) hv_log_gpu_rail(s)
#define HV_CHECK_CPU_LOG(cpu) hv_check_cpu_log(cpu)
#define HV_LOG_SUSPEND() hv_log_suspend()
#define HV_LOG_RESUME() hv_log_resume()
#else
#define HV_PWR(level, cat, id, extra)
#define HV_ACTION(level, cat, id, extra)
#define HV_WAKE(level, cat, id, extra)
#define HV_CNTL(level, cat, id, extra)

#define HV_ADD_NAME(t, pid, buf)
#define HV_RENAME(t, pid, buf)
#define HV_DUMP_DATA(i, dir, bytes)
#define HV_LOG_PANEL_STATE(state)
#define HV_TIME_OFFSET(d)
#define HV_LOG_CPU_CLK(cpu, t_idx)
#define HV_LOG_GPU_PWR(a, t, l)
#define HV_LOG_GPU_RAIL(s)
#define HV_CHECK_CPU_LOG(cpu)
#define HV_LOG_SUSPEND()
#define HV_LOG_RESUME()
#endif

/* Quick Havok logs */

#define HVH(cat, trace_id, strace_id) \
		HV(HV_HI, 0, cat, trace_id, strace_id, 0, 0)
#define HVM(cat, trace_id, strace_id) \
		HV(HV_MED, 0, cat, trace_id, strace_id, 0, 0)
#define HVL(cat, trace_id, strace_id) \
		HV(HV_LOW, 0, cat, trace_id, strace_id, 0, 0)
#define HVI(cat, trace_id, strace_id) \
		HV(HV_INFO, 0, cat, trace_id, strace_id, 0, 0)
#define HV HVH

#endif /* __KERNEL__ */

/* Level definitions
 */
#define HV_HI     3
#define HV_MED    2
#define HV_LOW    1
#define HV_INFO   0

/* log entry type */
#define HV_PWR_TYPE	0
#define HV_ACT_TYPE	1
#define HV_WAKE_TYPE	2
#define HV_CNTL_TYPE	3

struct hv_config {
	int bufferSize;      /* the buffer will hold this many entries        */
	int count;           /* the buffer contains this many entries         */
	int total;           /* count of entries added since the last reset   */
	int overtaken;       /* count of entries overtaken due to buffer wrap */
};

#define HV_PROC_NAME_CMD 0x1

#define __HVIO	0xAF

#define HV_GET_CONFIG		_IO(__HVIO, 1) /* get configuration */
#define HV_SET_BUFFER_SIZE	_IO(__HVIO, 2) /* set log size */
#define HV_RESET_LOG		_IO(__HVIO, 4) /* clear log */
#define HV_GET_VERSION		_IO(__HVIO, 5) /* get Havok driver version */
#define HV_SET_LOG_LEVEL	_IO(__HVIO, 6) /* set minimum log level */
#define HV_GET_LOG_LEVEL	_IO(__HVIO, 7) /* get minimum log level */
#define HV_GET_DATA		_IO(__HVIO, 8) /* get meta data (Ex. pname */

#endif  /* _LINUX_HAVOK_H */
