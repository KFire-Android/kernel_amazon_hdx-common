/*
 * event_log.h
 */

#ifndef _LINUX_EVENT_LOG_H
#define _LINUX_EVENT_LOG_H

#define TAG_PM_STATE		6000

#define EVENT_TYPE_STRING	0x3

void log_to_events(int32_t tag, const char *log_msg);

#endif  /* _LINUX_EVENT_LOG_H */
