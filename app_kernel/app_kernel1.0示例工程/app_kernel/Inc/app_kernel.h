#ifndef APP_KERNEL__H__
#define APP_KERNEL__H__

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "event_groups.h"

/*移植接口1：指定内核向终端的打印方式（必做）*/
#define APP_KERNEL_LOG_PORT               printf

/*移植接口2：串口单字节回调函数（必做）移植的时候，
  需保证pp_kernel_uart_callback所在的串口中断优先
  级可以被freertos管理到。例如：stm32用freertos，
  由于移植的时候用到的app_kernel_uart_callback函数
  需要放到串口的中断函数中，并且他用到了freertos的
  api，所以需保证对应的串口中断优先级在freertos的
  可管理范围内：即优先级的数值要大于等于宏定义：
  configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY*/
void app_kernel_uart_callback(uint8_t recByte);

/*移植接口3：指定内核管理的可编程led灯（可选，建议做，当系统正常运行，led闪烁；当系统卡死，led停止闪烁）*/
#define APP_KERNEL_HEAT_LED                     //{HAL_GPIO_TogglePin(LED_G_GPIO_Port, LED_G_Pin);}   

/*系统配置*/
#define  QUEUE_LEN                 10            /*定义所有用到队列的内核对象的最长队列*/
#define  QUEUE_SIZE                 4            /*定义app_kernel_send_command的队列长度（这个功能已弃用，但是我懒得删了*/
#define  USER_CMD_SIZE             50            /*用户输入的每一条指令包括空格在内的最长长度*/
#define  CMD_SEG_MAX_NUM           13            /*定义用户命令的最长段数*/
#define  MAX_TIME          0XFFFFFFFF            /*定义无限长的时间*/
#define  MAX_NAME                  30            /*定义ak对象名字的最长长度*/
#define  SIGNAL_PARAM_MAX_SIZE     20            /*定义time，signal等内核对象param指针可指向的最大内存(单位：字节)*/
#define  MAX_TIME_CNT              15            /*定义系统中允许同时存在的time链表节点个数*/          

/*app_kernel内核日志打印函数，用法同printf*/
#define APP_KERNEL_LOG(format, ...)     {vTaskSuspendAll();\
	                                       APP_KERNEL_LOG_PORT(format, ##__VA_ARGS__);\
                                         if( xTaskResumeAll() == pdFALSE )\
                                            portYIELD_WITHIN_API();}

/*app_kernel初始化*/
void app_kernel_Initialize(void);

#define akOK      1
#define akERR     0
typedef uint8_t AppKernel_t;
typedef void* AppKernel_px;

typedef void (*kernelFunction_t)( char**, uint32_t );       /*call回调函数类型定义*/
typedef void (*kernelSignalFunc)(void*, uint32_t);          /*time回调函数类型定义*/
typedef void (*slot_t)(void*, uint32_t);                    /*槽函数类型定义*/

typedef struct{
	slot_t     slot;
	void*      param;
	uint32_t   argc;
}app_kernel_signal;
typedef app_kernel_signal* signal_t;

typedef struct{
	uint32_t endTime;
}ak_nonBlockDelay_t;

/*以下是用户接口函数，转到定义，阅读函数注释即可使用本模块*/
signal_t Signal(void);
void connect(signal_t signal, slot_t slot);
void emit(signal_t signal, void* param, uint32_t argc);
void emit_FromISR(signal_t signal, void* param, uint32_t argc);

/*call服务相关函数*/
void app_kernel_regist_user_call_function(const char* name, kernelFunction_t callbackFunc);

/*time服务相关函数*/
void app_kernel_call_after_times(char* serviceName, \
	kernelSignalFunc EntryFunction, void*param, uint32_t argc, uint32_t callTime);

/*非阻塞延时*/
void app_kernel_non_blocking_delay_reset(ak_nonBlockDelay_t* akDelay, uint32_t ticksToDelay);
int  app_kernel_non_blocking_delay(ak_nonBlockDelay_t* akDelay);

/*以下是上述所有的用户函数的使用示例，用于指导用户如何使用上述用户接口函数
 注意，运行下面的三个示例函数，只需在串口终端中输入函数名，然后回车即可在
 串口终端里面查看运行效果*/
void app_kernel_demo1(void* param, uint32_t);    /*call与time使用示例*/
void app_kernel_demo2(void* param, uint32_t);    /*信号与槽使用示例*/
void app_kernel_demo3(void* param, uint32_t);    /*app_kernel_non_blocking_delay编写状态机示例*/

/*空函数，无需关注，这个是我在开发阶段使用的*/
void app_kernel_test(void);

#endif
















