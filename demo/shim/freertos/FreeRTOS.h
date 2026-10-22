// Demo: FreeRTOS stand-in. One thread: the simulation (demo/sim.cpp) runs the tasks' steps in turn;
// locks always succeed, queues are plain rings, a wait on an empty semaphore runs the control task.
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void* TaskHandle_t;
struct DemoSem;
struct DemoQueue;
typedef DemoSem* SemaphoreHandle_t;
typedef DemoQueue* QueueHandle_t;
typedef void (*TaskFunction_t)(void*);
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFFu
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define configTICK_RATE_HZ 1000
#define tskNO_AFFINITY 0x7FFFFFFF
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_ISR(m) ((void)(m))
#define portEXIT_CRITICAL_ISR(m) ((void)(m))
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
#define xSemaphoreTakeRecursive xSemaphoreTake
#define xSemaphoreGiveRecursive xSemaphoreGive
QueueHandle_t xQueueCreate(UBaseType_t n, UBaseType_t size);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
#define xQueueSendToBack xQueueSend
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char* name, uint32_t stack, void* arg, UBaseType_t prio,
                                   TaskHandle_t* h, BaseType_t core);
BaseType_t xTaskCreate(TaskFunction_t f, const char* name, uint32_t stack, void* arg, UBaseType_t prio, TaskHandle_t* h);
void vTaskDelay(TickType_t t);
void vTaskDelayUntil(TickType_t* wake, TickType_t t);
TickType_t xTaskGetTickCount();
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t h);
void vTaskDelete(TaskHandle_t h);
