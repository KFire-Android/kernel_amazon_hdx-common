/*
 * Copyright (C) 2011 Amazon Technologies, Inc.
 * Portions Copyright (C) 2007-2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_METRICSLOG_H
#define _LINUX_METRICSLOG_H

#if defined(CONFIG_AMAZON_METRICS_LOG) || defined(CONFIG_AMAZON_LOG)
/* (from the Android system core "log.h", to match retrieved log output in userspace) */
typedef enum {
	ANDROID_LOG_UNKNOWN = 0,
	ANDROID_LOG_DEFAULT,
	ANDROID_LOG_VERBOSE,
	ANDROID_LOG_DEBUG,
	ANDROID_LOG_INFO,
	ANDROID_LOG_WARN,
	ANDROID_LOG_ERROR,
	ANDROID_LOG_FATAL,
	ANDROID_LOG_SILENT,
} android_LogPriority;
#endif
#ifdef CONFIG_AMAZON_METRICS_LOG
void log_to_metrics(android_LogPriority priority, const char *domain, const char *logmsg);
void log_counter_to_vitals(android_LogPriority priority,
	const char *domain, const char *program,
	const char *source, const char *key,
	long counter_value, const char *unit, bool fgtracking);
void log_timer_to_vitals(android_LogPriority priority,
	const char *domain, const char *program,
	const char *source, const char *key,
	long timer_value, const char *unit, bool fgtracking);
#endif
#ifdef CONFIG_AMAZON_LOG
void log_to_amzmain(android_LogPriority priority,
               const char *domain, const char *logmsg);
#endif

#endif /* _LINUX_METRICSLOG_H */

