// App-level timers

#ifndef TIMERS_H__
#define TIMERS_H__

#include "app_timer.h"

// NRF parameters that we use for our app's timers
#define APP_TIMER_TICKS_PER_SECOND      32768
#define APP_TIMER_PRESCALER             0                                           /* Value of the RTC1 PRESCALER register. */
#define APP_TIMER_MAX_TIMERS            10                                          /* Maximum number of timers used by the app. */
#define APP_TIMER_OP_QUEUE_SIZE         4                                           /* Size of timer operation queues. */

// Scheduler related.  In comm, we queue only the CMDBUF_TYPE for processing.  But we also
// use the scheduler for timer interrupts.
#define SCHED_MAX_EVENT_DATA_SIZE MAX(APP_TIMER_SCHED_EVT_SIZE, sizeof(uint16_t))
#define SCHED_QUEUE_SIZE                40

// Max NRF_DELAY given that busy buffer and sched queue would otherwise overflow
#include "nrf_delay.h"
#define MAX_NRF_DELAY_MS 500

// Misc
void timer_init(void);
void timer_start();
void timer_start_serial_wakeup(uint32_t sleep_milliseconds);

uint32_t get_seconds_since_boot(void);
void set_timestamp(uint32_t date, uint32_t time);
bool get_current_timestamp(uint32_t *date, uint32_t *time, uint32_t *offset);

#endif // TIMERS_H__
