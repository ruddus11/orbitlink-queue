/*
 * FreeRTOSConfig.h - Kernel configuration for the macOS (Apple Silicon) POSIX simulator (OrbitLink)
 *
 * A minimal configuration based on the official FreeRTOS/Demo/Posix_GCC example, keeping
 * only what this project needs. See the page below for the meaning of each value.
 * https://www.freertos.org/a00110.html
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---------- Scheduler ---------- */
#define configUSE_PREEMPTION                       1   /* Preemptive priority scheduling */
#define configUSE_TIME_SLICING                     1   /* Tasks of equal priority take turns every tick */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    0   /* The POSIX port has no optimised version, so 0 */
#define configMAX_PRIORITIES                       7   /* Usable priorities: 0 (Idle) to 6. Higher number = higher priority */
#define configIDLE_SHOULD_YIELD                    1

/*
 * Tick rate. With 1 tick = 1 ms a tick value can be read directly as milliseconds.
 * In the POSIX port the tick comes from a separate pthread calling usleep(), so it is
 * not exactly real time (measured error is in README.md).
 */
#define configTICK_RATE_HZ                         1000
#define configTICK_TYPE_WIDTH_IN_BITS              TICK_TYPE_WIDTH_64_BITS  /* 64-bit host, so 64-bit ticks */

/* ---------- Memory ---------- */
#define configSUPPORT_DYNAMIC_ALLOCATION           1   /* xTaskCreate, xQueueCreate */
#define configSUPPORT_STATIC_ALLOCATION            0   /* Static allocation APIs are not used */
#define configTOTAL_HEAP_SIZE                      ( ( size_t ) ( 256 * 1024 ) ) /* Heap size used by heap_4.c */

/*
 * Task stack size (unit: word = 8 bytes).
 * In the POSIX port task code runs on the pthread's own stack, and the FreeRTOS stack
 * reserved here only holds the port's internal data (Thread_t). So it can be small, but
 * unlike a real board, stack overflow cannot be observed through this value.
 */
#define configMINIMAL_STACK_SIZE                   256
#define configMAX_TASK_NAME_LEN                    16

/* ---------- Kernel features in use ---------- */
#define configUSE_MUTEXES                          1
#define configUSE_COUNTING_SEMAPHORES              1   /* Counting semaphore that wakes the Receiver */
#define configUSE_TASK_NOTIFICATIONS               1
#define configQUEUE_REGISTRY_SIZE                  0

/* ---------- Features not used ---------- */
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configUSE_MALLOC_FAILED_HOOK               0   /* Failures are checked through return values in main() */
#define configUSE_TIMERS                           0
#define configUSE_CO_ROUTINES                      0
#define configUSE_TRACE_FACILITY                   0
#define configGENERATE_RUN_TIME_STATS              0
#define configCHECK_FOR_STACK_OVERFLOW             0   /* See the stack note above: meaningless in the POSIX port */

/* ---------- APIs to include ---------- */
#define INCLUDE_vTaskDelay                         1
#define INCLUDE_xTaskDelayUntil                    1
#define INCLUDE_vTaskDelete                        1   /* Used to clean up tasks that have finished */
#define INCLUDE_vTaskSuspend                       1   /* Needed to block forever with portMAX_DELAY */
#define INCLUDE_xTaskGetSchedulerState             1
#define INCLUDE_uxTaskPriorityGet                  1

/* ---------- Error detection during development ---------- */
void vAssertCalled( const char * pcFile, unsigned long ulLine ); /* Implemented in each app's main source file */
#define configASSERT( x )    if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )

#endif /* FREERTOS_CONFIG_H */
