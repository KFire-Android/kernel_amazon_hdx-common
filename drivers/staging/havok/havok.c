/**
 *
 * Note: read entries from /dev/hv
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/rbtree.h>
#include <linux/wait.h>
#include <linux/eventpoll.h>
#include <linux/mount.h>
#include <linux/bitops.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/proc_fs.h>
#include <linux/havok.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>
#include <linux/completion.h>
#include <linux/types.h>
#include <linux/hrtimer.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <linux/time.h>
#include <linux/tick.h>
#include <linux/delay.h>

static const char hv_version[] = "0.9";
static const char hv_dev_name[] = HV_DEV_NAME;
static const char hv_dump_dev_name[] = HV_DUMP_DEV_NAME;
static const char hv_ctrl_dev_name[] = HV_CTRL_DEV_NAME;
#define HV_DEFAULT_BUFFER_SIZE 42000      /* # entries: chosen to fit in 1MB */
#define HV_PROC_NAME           "hv"

#define CPU_CLK_LOG_INTERVAL (HZ * 1)	/* 1 second */
#define MDATA_MAX_LEN (TASK_COMM_LEN + 1)

struct mdata {
	unsigned char type;
	pid_t pid;
	char buf[MDATA_MAX_LEN];
};

static struct mdata mdata_buf[100];
DECLARE_COMPLETION(mdata_ready);

struct havok_config {
	int bufferSize;         /* the buffer will hold this many entries */
	int count;              /* the buffer contains this many entries  */
	int total;              /* count of entries added since reset     */
};

/**
 * Internal data structures
 */

struct hv_event_info {
	struct completion comp;
	int blocked;
};

#define BASECOPY_DISABLE -1

/* base sequence number of bulk copy. if !-1, copy in progress
 */
static atomic_t basecopy = ATOMIC_INIT(BASECOPY_DISABLE);

/* seq to wake up bulk reader
 */
static atomic_t wake_up_seq = ATOMIC_INIT(HV_DEFAULT_BUFFER_SIZE * 2);
DECLARE_COMPLETION(dump_c);
#define NEAR_COPY_LIMIT 16
#define NEAR_COPY_SIZE 64

static struct hv_log {
	struct hv_entry *pBuffer;   /* base address of internal entry buffer  */
	struct hv_entry *pLimit;    /* first entry past the end of the buffer */
	struct hv_entry *pHead;     /* entries are added at the head          */
	struct hv_entry *pTail;     /* entries are removed from the tail      */
	int bufferSize;             /* the buffer will hold this many entries */
	int count;                  /* the buffer contains this many entries  */
	int total;                  /* count of entries added since reset     */
	int overtaken;              /* count of entries overtaken by wrapping */
	struct proc_dir_entry *pProc;   /* /proc node pointer */
	spinlock_t bufLock;             /* spin lock for SMP  */
} g_data = {
	.pBuffer = 0                    /* ==0 indicates uninitialized        */
};

static int havok_control_reset(void);
static int havok_proc_init(void);

static int havok_proc_write(struct file *pFile, const char __user *pBuff,
				unsigned long len, void *pData);
static void havok_get_g_data(struct hv_log *pData);
static int havok_proc_read(char *pPage, char **ppStart, off_t off,
				int count, int *pEof, void *pData);
static int havok_probe(struct platform_device *p);

static struct platform_driver havok_platform = {
	.probe = havok_probe,
	.driver = {
		.name = "havok"
	}
};

static int havok_open(struct inode *, struct file *);
static int havok_release(struct inode *, struct file *);
static ssize_t havok_read(struct file *, char *, size_t, loff_t *);
static ssize_t havok_bulk_read(struct file *, char *, size_t, loff_t *);
static ssize_t havok_read_id(struct file *, char *, size_t, loff_t *);
static ssize_t havok_write(struct file *, const char *, size_t, loff_t *);
static loff_t havok_llseek(struct file *filp, loff_t off, int whence);
static long havok_ioctl(struct file *, unsigned int, unsigned long);

/* Device definitions
*/
static const struct file_operations fops = {
	 .llseek = havok_llseek,
	 .read = havok_read,
	 .write = havok_write,
	 .open = havok_open,
	 .release = havok_release,
	 .unlocked_ioctl = havok_ioctl,
};

static const struct file_operations dump_fops = {
	 .llseek = havok_llseek,
	 .read = havok_bulk_read,
	 .write = havok_write,
	 .open = havok_open,
	 .release = havok_release,
	 .unlocked_ioctl = havok_ioctl,
};

static const struct file_operations ctrl_fops = {
	 .llseek = havok_llseek,
	 .read = havok_read_id,
	 .write = havok_write,
	 .open = havok_open,
	 .release = havok_release,
	 .unlocked_ioctl = havok_ioctl,
};

struct hv_device_info {
	struct miscdevice hv_device;
	int blocked;
	int dump_mode;
	wait_queue_head_t wq;
};

static struct hv_device_info hv_main_dev = {
	.hv_device.name = hv_dev_name,
	.hv_device.fops = &fops,
	.dump_mode = 0
};

static struct hv_device_info hv_dump_dev = {
	.hv_device.name = hv_dump_dev_name,
	.hv_device.fops = &dump_fops,
	.dump_mode = 1
};

static struct hv_device_info hv_ctrl_dev = {
	.hv_device.name = hv_ctrl_dev_name,
	.hv_device.fops = &ctrl_fops
};

/******************************************************************************/

/**
 * Kernel API used to log events.
 */
SYSCALL_DEFINE4(hv, int, type, int, category, int, id, int, extra)
{
	return hv(type, category, id, extra);
}

static void handle_wdata_timeout(struct work_struct *work);
DECLARE_DELAYED_WORK(hv_wifi_tx_dump, handle_wdata_timeout);

#define WDUMP_DELAY 10
#define W_TIMEOUT 5
#define W_TX_SHIFT 4
static atomic_t last_wdump_tx_bytes = ATOMIC_INIT(0);
static atomic_t w_tx_active = ATOMIC_INIT(0);

static int panel_state = 1;
static unsigned int last_gpu_level;

/* Helper log functions
*/

#define MAX_PWR_LEVELS 10
void hv_log_gpu_pwr(unsigned int active, unsigned int total, unsigned int level)
{
	static u64 prev_active[MAX_PWR_LEVELS];
	static u64 prev_total[MAX_PWR_LEVELS];
	unsigned int delta_active, delta_total, busy;

	delta_total = total - prev_total[level];
	delta_active = active - prev_active[level];

	busy = 63 * delta_active / delta_total;
	if (busy > 63)
		busy = 63;
	HV_PWR(1, 5, 3, (int)((busy << 4) | level));

	prev_active[level] = active;
	prev_total[level] = total;

	last_gpu_level = level;
}

void hv_log_gpu_rail(int state)
{
	HV_PWR(1, 5, 0, state | last_gpu_level << 1);
}
static int freq_translate_table[20] = {
	0,
	300000,
	422400,
	652800,
	729600,
	883200,
	960000,
	1036800,
	1190400,
	1267200,
	1497600,
	1574400,
	1728000,
	1958400,
	2150400
};

static atomic_t next_cpu_log[4];

static int freq_reset;

void hv_log_resume(void)
{
	HV_PWR(1, 3, 0, 1);
	freq_reset = 1;
}

void hv_log_suspend(void)
{
	int c;

	HV_PWR(1, 3, 0, 0);

	for_each_online_cpu(c)
		hv_log_cpu_clk(c, -1);
	freq_reset = 1;
}

void hv_check_cpu_log(unsigned int cpu)
{
	if (time_after(jiffies,
			(unsigned long)atomic_read(&(next_cpu_log[cpu]))))
		hv_log_cpu_clk((int)cpu, -1);
}

#define MAX_DELTA (2 << 51)

void hv_log_cpu_clk(unsigned int cpu, int new_clk)
{
	int new_idx;
	static unsigned char oldrate[4];
	static u64 prev_time[4];
	static u64 prev_idle[4];
	ktime_t now;
	u64 idle, delta_time;
	unsigned int delta_us_time, delta_idle, answer;
	int i;

	/* Allow for a dummy frequency log */
	if (new_clk == -1)
		new_idx = oldrate[cpu];
	else {
		for (new_idx = 0;
			new_idx < ARRAY_SIZE(freq_translate_table) - 1;
			new_idx++) {
			if ((new_clk >= freq_translate_table[new_idx]) &&
			    (new_clk < freq_translate_table[new_idx + 1]))
				break;
		}
	}

	/* If we're resetting the idle values, just log 0 idle */
	if (freq_reset) {
		freq_reset = 0;
		now = ktime_get();
		idle = get_cpu_idle_time_us2(cpu, NULL, &now);

		for (i = 0; i < 4; i++) {
			prev_time[i] = ktime_to_us(now);
			prev_idle[i] = idle;
		}

		HV_PWR(1, 1, 8 + cpu, (oldrate[cpu] << 5) | new_idx);
		HV_PWR(1, 1, 16 + cpu, 0);
	}

	else if (!timekeeping_suspended) {
		now = ktime_get();
		idle = get_cpu_idle_time_us2(cpu, NULL, &now);
		delta_time = ktime_to_us(now) - prev_time[cpu];
		prev_time[cpu] = ktime_to_us(now);
		if (likely(delta_time > 0)) {
			if (delta_time < 4000000) {
				delta_idle =
					(unsigned int)(idle - prev_idle[cpu]);
				delta_us_time = (unsigned int)delta_time;
			} else {
				delta_idle =
					(unsigned int)((idle - prev_idle[cpu])
						>> 10);
				delta_us_time =
					(unsigned int)(delta_time >> 10);
			}
			answer = 1023 * delta_idle / delta_us_time;
			if (answer > 1023)
				answer = 1023;
			HV_PWR(1, 1, 8 + cpu, (oldrate[cpu] << 5) | new_idx);
			HV_PWR(1, 1, 16 + cpu, (int)(1023 - answer));
		}
		prev_idle[cpu] = idle;
	}
	atomic_set(&(next_cpu_log[cpu]), (int)(jiffies + CPU_CLK_LOG_INTERVAL));
	oldrate[cpu] = new_idx;
	return;
}

void hv_log_panel_state(int state)
{
	HV_PWR(1, 2, 1, state);
	panel_state = state;
}

static void handle_wdata_timeout(struct work_struct *work)
{
	unsigned int tx_dump_bytes =
		(unsigned int)atomic_read(&last_wdump_tx_bytes);
	unsigned int log_value = tx_dump_bytes >> W_TX_SHIFT;
	if (log_value >> 10)
		log_value = 0x3ff;

	if (tx_dump_bytes) {
		HV_PWR(1, 4, 1, log_value);
		/* re-sched */
		schedule_delayed_work(&hv_wifi_tx_dump, WDUMP_DELAY);
	} else
		atomic_set(&w_tx_active, 0);

	atomic_sub(tx_dump_bytes, &last_wdump_tx_bytes);
	return;
}

void hv_dump_data(enum hv_data_i interface, int direction, int bytes)
{
	switch (interface) {
	case HV_WIFI:
		if (direction == 0) {
			/* Already in Tx mode */
			if (!atomic_read(&w_tx_active)) {
				int sched_done;
				sched_done =
					schedule_delayed_work(&hv_wifi_tx_dump,
						WDUMP_DELAY);
				if (sched_done) {
					atomic_set(&w_tx_active, 1);
					HV_PWR(1, 4, 1, 0);
				}
			}
			atomic_add(bytes, &last_wdump_tx_bytes);
		}
		break;
	case HV_WAN:
		break;
	}
}

int hv_time_offset(struct timespec *sdelta)
{
	ktime_t offset;
	unsigned long flags;
	struct hv_entry *pEntry;

	return 0;  /* This will be removed when TOD offset is improved */
	offset = ktime_get_monotonic_offset();

	/* Log new delta */
	if (!g_data.pBuffer)
		return -EBUSY;

	spin_lock_irqsave(&g_data.bufLock, flags);
	if (g_data.count >= g_data.bufferSize) {
		g_data.pTail++;
		if (g_data.pTail >= g_data.pLimit)
			g_data.pTail = g_data.pBuffer;
		g_data.count--;
		g_data.overtaken++;
	}

	pEntry = g_data.pHead++;
	if (g_data.pHead >= g_data.pLimit)
		g_data.pHead = g_data.pBuffer;
	g_data.count++;

	pEntry->sequence = g_data.total++;

	memcpy(&(pEntry->tv), sdelta, sizeof(struct timespec));
	pEntry->type = HV_CNTL_TYPE;
	pEntry->category = 1;        /* Suspend delta */
	pEntry->event_id = 0;
	pEntry->extra = 0;

	if (g_data.count >= g_data.bufferSize) {
		g_data.pTail++;
		if (g_data.pTail >= g_data.pLimit)
			g_data.pTail = g_data.pBuffer;
		g_data.count--;
		g_data.overtaken++;
	}

	pEntry = g_data.pHead++;
	if (g_data.pHead >= g_data.pLimit)
		g_data.pHead = g_data.pBuffer;
	g_data.count++;

	pEntry->sequence = g_data.total++;

	pEntry->tv = ktime_to_timespec(offset);
	pEntry->type = HV_CNTL_TYPE;
	pEntry->category = 2;        /* TOD offset */
	pEntry->event_id = 0;
	pEntry->extra = 0;

	spin_unlock_irqrestore(&g_data.bufLock, flags);

	/* unblock /dev/hv */
	if (hv_main_dev.blocked) {
		hv_main_dev.blocked = 0;
		wake_up_interruptible(&(hv_main_dev.wq));
	}

	return 0;
}

/**
 * Internal kernel API used to log events
 */
int hv(int type, int category, int id, int extra)
{
	unsigned long flags;
	struct hv_entry *pEntry;
	struct timespec tv;

	if (!g_data.pBuffer)
		return -EBUSY;

	/* If time is invalid there's no point in logging */
	if (timekeeping_suspended)
		return 0;

	getnstimeofday(&tv);

	spin_lock_irqsave(&g_data.bufLock, flags);
	if (g_data.count >= g_data.bufferSize) {

		/* Check for collision with buffer copy        */
		/* A collision should be a very rare occurance */
		if (g_data.pTail->sequence == atomic_read(&basecopy)) {
			printk(KERN_CRIT "Need to clear basecopy\n");
			atomic_set(&basecopy, BASECOPY_DISABLE);
		}

		g_data.pTail++;
		if (g_data.pTail >= g_data.pLimit)
			g_data.pTail = g_data.pBuffer;
		g_data.count--;
		g_data.overtaken++;
	}

	pEntry = g_data.pHead++;
	if (g_data.pHead >= g_data.pLimit)
		g_data.pHead = g_data.pBuffer;
	g_data.count++;

	pEntry->sequence = g_data.total++;

	pEntry->type = (short)type;
	pEntry->tv = tv;
	pEntry->category = (short)category;
	pEntry->event_id = id;
	pEntry->extra = extra;

	spin_unlock_irqrestore(&g_data.bufLock, flags);

	/* unblock /dev/hv */
	if (hv_main_dev.blocked) {
		hv_main_dev.blocked = 0;
		wake_up_interruptible(&(hv_main_dev.wq));
	}

	if ((atomic_read(&wake_up_seq) <= g_data.total) &&
		(atomic_read(&basecopy) == BASECOPY_DISABLE)) {
		atomic_set(&wake_up_seq, g_data.total + HV_DEFAULT_BUFFER_SIZE);
		complete(&dump_c);
	}
	return 0;
}

/**
 * Reset the Havok store.
 */
static int havok_control_reset(void)
{
	unsigned long flags;

	if (!g_data.pBuffer)
		return -EBUSY;

	spin_lock_irqsave(&g_data.bufLock, flags);
	g_data.total = g_data.count = g_data.overtaken = 0;
	g_data.pHead = g_data.pTail = g_data.pBuffer;
	spin_unlock_irqrestore(&g_data.bufLock, flags);
	return 0;
}

/**
 * Initialize the Havok /proc node.
 */
static int havok_proc_init(void)
{
	if (g_data.pProc)
		return 0;

	g_data.pProc = create_proc_entry(HV_PROC_NAME,
			S_IFREG | S_IRUGO | S_IWUSR | S_IWGRP, NULL);

	if (!g_data.pProc) {
		printk(KERN_INFO "havok_proc_init: cannot create /proc node\n");
		return -ENODEV;
	}

	g_data.pProc->read_proc = havok_proc_read;
	g_data.pProc->write_proc = havok_proc_write;
	return 0;
}

/**
 * Responds to writes on any Havok /proc/havok.
 * Currently does nothing.
 * It's thought that this could be a simple control interface.
 */
static int havok_proc_write(struct file *pFile, const char __user *pBuff,
			unsigned long len, void *pData)
{

	if (!g_data.pBuffer)
		return -EBUSY;
	/* do nothing */
	return 0;
}

/**
 * Retrieve a snapshot of g_data.
 */
static void havok_get_g_data(struct hv_log *pData)
{
	unsigned long flags;

	spin_lock_irqsave(&g_data.bufLock, flags);
	*pData = g_data;
	spin_unlock_irqrestore(&g_data.bufLock, flags);
}

/**
 * Responds to reads on the Havok /proc/havok.
 * Returns text block of Havok infrastructure info.
 * (See kernel/fs/proc/generic.c "How to be a proc read function")
 */
static int havok_proc_read(char *pPage, char **ppStart, off_t off, int count,
			int *pEof, void *pData)
{
	struct hv_log data;
	int rc;

	havok_get_g_data(&data);

	*pEof = 1;
	rc = snprintf(pPage, count,
				"Havok v0.0\n"
				"       size: %8d\n"
				"      count: %8d\n"
				"      total: %8d\n"
				"  overtaken: %8d\n",
		data.bufferSize, data.count, data.total, data.overtaken);

	return rc;
}

int havok_probe(struct platform_device *p)
{
	return 0;
}

static int havok_open(struct inode *inode, struct file *filp)
{
	unsigned long flags;
	int minor = MINOR(inode->i_rdev);

	if ((hv_main_dev.hv_device.minor == minor) ||
		(hv_dump_dev.hv_device.minor == minor) ||
		(hv_ctrl_dev.hv_device.minor == minor))
		filp->private_data = (void *)&hv_main_dev;
	else
		return -ENOENT;

	/* set offset to be the current lowest sequence */
	spin_lock_irqsave(&g_data.bufLock, flags);
	filp->f_pos = g_data.total - g_data.count;
	spin_unlock_irqrestore(&g_data.bufLock, flags);

	return 0;
}

static int havok_release(struct inode *inode, struct file *file)
{
	return 0;
}

static loff_t havok_llseek(struct file *filp, loff_t off, int whence)
{
	loff_t newpos;
	switch (whence) {
	case 0: /* SEEK_SET */
		newpos = off;
		break;

	case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	case 2: /* SEEK_END */
		newpos = g_data.total + off;
		break;

	default: /* can't happen */
		return -EINVAL;
	}
	if (newpos < 0)
		return -EINVAL;
	filp->f_pos = newpos;
	return newpos;
}

/**
 * Retrieve a single entry by sequence. If the sequence is less than
 * current minimum sequence, the minimum sequence entry is returned
 * return value is also set to be sequence number of entry returned
 */
static int hv_get_entries(struct hv_entry *entry, int sequence, int count)
{
	int rc = -1;
	unsigned long flags;

	spin_lock_irqsave(&g_data.bufLock, flags);
	{
		int base = g_data.total - g_data.count;
		if (sequence < base)
			sequence = base;
		if (sequence < g_data.total) {
			int index = sequence - base;
			struct hv_entry *tempe = g_data.pTail + index;
			if (tempe >= g_data.pLimit)
				tempe -= g_data.bufferSize;
			memcpy(entry, tempe, sizeof(struct hv_entry));
			rc = sequence;
		}
	}
	spin_unlock_irqrestore(&g_data.bufLock, flags);

	return rc;
}

/*
 * reads out havok entries from store
 */
static ssize_t havok_read(struct file *filp, char *buffer,
				size_t length, loff_t *offset)
{
	struct hv_device_info *pDevice =
		(struct hv_device_info *)filp->private_data;
	struct hv_entry entry;
	struct havok_entry_t ret_data;
	long ret;

	if (length < sizeof(ret_data))
		return 0;

	memset(&entry, 0, sizeof(entry));

	for (;;) {
		/*
		 * seq is the actual sequence number of entry being read, since
		 * it's possible the requested entry is already overtaken
		 */
		int seq = hv_get_entries(&entry, (int)(*offset), 1);
		if (seq != -1) {
			*offset = seq + 1;
				memset(&ret_data, 0,
					sizeof(struct havok_entry_t));
				ret_data.ts = entry.tv;
				ret_data.ctrl = 0xd0;
				ret_data.counter = seq;
				ret_data.extra_1 =
					(u_char)(entry.extra & 0xff);
				if (entry.type == HV_ACT_TYPE)
					ret_data.extra_2 =
						(u_char)((entry.extra >> 8));
				else
					ret_data.extra_2 =
					(u_char)((entry.extra >> 8) & 0x3);
				ret_data.comp_trace_id[0] =
					(u_char)entry.category;
				ret_data.comp_trace_id[1] =
					(u_char)entry.event_id;
				ret_data.comp_trace_id[2] =
					(u_char)entry.type;
				ret = copy_to_user(buffer, &ret_data,
					sizeof(struct havok_entry_t));

				return (ssize_t)(sizeof(struct havok_entry_t)
					- ret);
		} else {
			if (filp->f_flags & O_NONBLOCK)
				return 0;
			pDevice->blocked = 1;
			if (wait_event_interruptible(pDevice->wq,
				pDevice->blocked == 0) == -ERESTARTSYS)
				return 0;
		}
	}
}

/*
 * reads out havok entries from store
 */
static ssize_t havok_bulk_read(struct file *filp, char *buffer,
				size_t length, loff_t *offset)
{
	unsigned long flags;
	long wrc;
	long rr;
	size_t remaining_seq = length / sizeof(struct hv_entry);
	size_t total_copy = 0;
	char *to = buffer;
	int lseq = *offset;

	/* Make sure request is smaller than buffer */
	if (length > HV_DEFAULT_BUFFER_SIZE * sizeof(struct hv_entry))
		length = HV_DEFAULT_BUFFER_SIZE * sizeof(struct hv_entry);

	/* TODO: Make g_data.total atomic */
	while (((length / sizeof(struct hv_entry)) + *offset) > g_data.total) {
		atomic_set(&wake_up_seq,
			(*offset + (length / sizeof(struct hv_entry))));
		INIT_COMPLETION(dump_c);
		wrc = wait_for_completion_interruptible(&dump_c);
		if (wrc)
			return 0;
	}

	while (remaining_seq > 0) {
		int tail_seq;
		struct hv_entry *from;
		size_t lcount;

		/* Set up copy */
		spin_lock_irqsave(&g_data.bufLock, flags);
		tail_seq = g_data.total - g_data.count;
		if (lseq < tail_seq) {
			remaining_seq -= tail_seq - lseq;
			lseq = tail_seq;
		}
		if ((lseq + remaining_seq) > g_data.total)
			remaining_seq = g_data.total - lseq;

		from = g_data.pTail + lseq - tail_seq;
		if (from >= g_data.pLimit)
			from -= g_data.bufferSize;

		if (remaining_seq > (g_data.pLimit - from))
			lcount = g_data.pLimit - from;
		else
			lcount = remaining_seq;

		/* If we're close to the tail, do a small copy to minimize
		 * chance of an overrun
		 */
		if (((lseq - tail_seq) < NEAR_COPY_LIMIT) &&
				(lcount > NEAR_COPY_SIZE))
			lcount = NEAR_COPY_SIZE;
		if (lseq == BASECOPY_DISABLE)
			lseq++;
		atomic_set(&basecopy, lseq);
		spin_unlock_irqrestore(&g_data.bufLock, flags);

		/* Copy it */
		rr = copy_to_user(to, from, lcount * sizeof(struct hv_entry));
		if (rr) {
			printk(KERN_CRIT "Couldn't copy everything %ld\n", rr);
			atomic_set(&basecopy, BASECOPY_DISABLE);
			return -EBADF;
		}

		/* Check if we were overrun */
		if (atomic_read(&basecopy) != BASECOPY_DISABLE) {
			remaining_seq -= lcount;
			lseq += lcount;
			to += lcount * sizeof(struct hv_entry);
			total_copy += lcount;
		}
		atomic_set(&basecopy, BASECOPY_DISABLE);
		msleep(200);
	}
	*offset += total_copy;
	return (ssize_t)(sizeof(struct hv_entry) * total_copy);
}

static long handle_meta_data(void *buf, size_t len)
{
	int i;
	long rc = -1;
	long wrc;
	unsigned char tbuf[64];

	if (buf == NULL)
		return -EFAULT;
	memset(tbuf, 0, 64);
	for (i = 0; i < 100; i++) {
		if (mdata_buf[i].type)
			break;
	}
	if (i == 100) {
		wrc = wait_for_completion_interruptible(&mdata_ready);
		if (wrc)
			return 0;
		for (i = 0; i < 100; i++) {
			if (mdata_buf[i].type)
				break;
		}
	}

	/* Return data */
	if (i != 100) {
		tbuf[0] = mdata_buf[i].type;
		tbuf[1] = (unsigned char)(mdata_buf[i].pid & 0xff);
		tbuf[2] = (unsigned char)(mdata_buf[i].pid >> 8);
		strlcpy(&(tbuf[3]), mdata_buf[i].buf, MDATA_MAX_LEN);
	} else
		return 0;

	mdata_buf[i].type = 0;
	rc = copy_to_user((void __user *)buf, tbuf,
				strlen(mdata_buf[i].buf) + 3);

	if (!rc)
		return 1;
	else
		return 0;
}

/*
 * Return identifiers needed to be saved
 */
static ssize_t havok_read_id(struct file *filp, char *buffer,
				size_t length, loff_t *offset)
{
	long rc;
	/* TODO : check private_data to determine what to return here     */
	/*        private data defaults to meta_data, but can be set with */
	/*        an ioctl                                                */

	/* call appropriate helper routine                   */
	/* Helper routine returns the number of items copied */
	/* (item size depends on type of item)               */

	rc = handle_meta_data(buffer, length);
	return rc;
}

static ssize_t havok_write(struct file *filp, const char *buffer,
			size_t length, loff_t *offset)
{
	char tbuf[32];
	unsigned int cmd;
	pid_t pid;

	if (copy_from_user(tbuf, buffer, length))
		return length;
	tbuf[length] = 0;
	if (kstrtouint(tbuf, 10, &cmd))
		return length;

	switch (cmd) {
	case 1:  /* Log current pid */
		pid = current->tgid;
		HV_ACTION(1, 1, cmd, pid);
		break;
	}

	return length;
}

void hv_add_name(unsigned int t, pid_t pid, char *buf)
{
	int i;

	for (i = 0; i < 100; i++) {
		if (mdata_buf[i].type == 0) {
			mdata_buf[i].type = (unsigned char)t;
			mdata_buf[i].pid = pid;
			memset(mdata_buf[i].buf, 0, sizeof(struct mdata));
			strlcpy(mdata_buf[i].buf, buf, 64);
			break;
		}
	}
	if (i == 100)
		printk(KERN_CRIT "Couldn't add name\n");
	else
		complete(&mdata_ready);
}

void hv_rename(unsigned int t, pid_t pid, char *buf)
{
	int i;

	for (i = 0; i < 100; i++) {
		if (mdata_buf[i].type == 0) {
			mdata_buf[i].type = (unsigned char)t;
			mdata_buf[i].pid = pid;
			memset(mdata_buf[i].buf, 0, sizeof(struct mdata));
			strlcpy(mdata_buf[i].buf, buf, 64);
			break;
		}
	}
	if (i == 100)
		printk(KERN_CRIT "Couldn't add name\n");
	else
		complete(&mdata_ready);
}

static long havok_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct havok_config config;
	struct hv_log data;
	long rc = -1;

	switch (cmd) {
	case HV_GET_CONFIG:
		havok_get_g_data(&data);
		config.bufferSize = data.bufferSize;
		config.count = data.count;
		config.total = data.total;
		rc = copy_to_user((void __user *)arg,
					&config, sizeof(struct havok_config));
		break;
	case HV_SET_BUFFER_SIZE:
		break;
	case HV_RESET_LOG:
		rc = havok_control_reset();
		break;
	case HV_GET_VERSION:
		/* copy entire string with terminating null */
		rc = copy_to_user((void __user *)arg, hv_version,
				sizeof(hv_version));
		break;
	case HV_GET_DATA:
		break;
	default:
		break;
	}
	return rc;
}

/**
 * Initialize a Havok device.
 */
static int havok_device_init(struct hv_device_info *pDevice)
{
	int ret;

	pDevice->hv_device.minor = MISC_DYNAMIC_MINOR;
	pDevice->hv_device.parent = NULL;

	init_waitqueue_head(&pDevice->wq);
	pDevice->blocked = 0;

	ret = misc_register(&pDevice->hv_device);
	if (ret)
		printk(KERN_ERR "havok_init: cannot register misc device\n");

	return ret;
}

static void havok_values_init(void)
{
	HV_PWR(1, 3, 0, 1);
}

static int __init havok_init(void)
{
	if (g_data.pBuffer)
		return 0;

	g_data.bufferSize = HV_DEFAULT_BUFFER_SIZE;
	g_data.total = g_data.count = g_data.overtaken = 0;
	g_data.pProc = 0;
	spin_lock_init(&g_data.bufLock);
	g_data.pLimit = g_data.pHead = g_data.pTail = g_data.pBuffer =
		kmalloc(sizeof(struct hv_entry) * g_data.bufferSize,
			GFP_KERNEL);
	g_data.pLimit += g_data.bufferSize;

	if (!g_data.pBuffer) {
		printk(KERN_INFO "havok_init: cannot allocate kernel memory\n");
		return -ENOMEM;
	} else {
		havok_proc_init();

		havok_device_init(&hv_main_dev);
		havok_device_init(&hv_dump_dev);
		havok_device_init(&hv_ctrl_dev);

		platform_driver_register(&havok_platform);
	}

	/* Handle initial value logs */
	havok_values_init();

	return 0;
}

device_initcall(havok_init);

