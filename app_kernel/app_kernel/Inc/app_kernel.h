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

/*��ֲ�ӿ�1��ָ���ں����ն˵Ĵ�ӡ��ʽ��������*/
#define APP_KERNEL_LOG_PORT               printf

/*��ֲ�ӿ�2�����ڵ��ֽڻص���������������ֲ��ʱ��
  �豣֤pp_kernel_uart_callback���ڵĴ����ж�����
  �����Ա�freertos���������磺stm32��freertos��
  ������ֲ��ʱ���õ���app_kernel_uart_callback����
  ��Ҫ�ŵ����ڵ��жϺ����У��������õ���freertos��
  api�������豣֤��Ӧ�Ĵ����ж����ȼ���freertos��
  �ɹ���Χ�ڣ������ȼ�����ֵҪ���ڵ��ں궨�壺
  configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY*/
void app_kernel_uart_callback(uint8_t recByte);

/*��ֲ�ӿ�3��ָ���ں˹���Ŀɱ��led�ƣ���ѡ������������ϵͳ�������У�led��˸����ϵͳ������ledֹͣ��˸��*/
#define APP_KERNEL_HEAT_LED                     //{HAL_GPIO_TogglePin(LED_G_GPIO_Port, LED_G_Pin);}   

/*ϵͳ����*/
#define  QUEUE_LEN                 10            /*���������õ����е��ں˶���������*/
#define  QUEUE_SIZE                 4            /*����app_kernel_send_command�Ķ��г��ȣ�������������ã�����������ɾ��*/
#define  USER_CMD_SIZE             50            /*�û������ÿһ��ָ������ո����ڵ������*/
#define  CMD_SEG_MAX_NUM           13            /*�����û�����������*/
#define  MAX_TIME          0XFFFFFFFF            /*�������޳���ʱ��*/
#define  MAX_NAME                  30            /*����ak�������ֵ������*/
#define  SIGNAL_PARAM_MAX_SIZE     20            /*����time��signal���ں˶���paramָ���ָ�������ڴ�(��λ���ֽ�)*/
#define  MAX_TIME_CNT              15            /*����ϵͳ������ͬʱ���ڵ�time����ڵ����*/          

/*app_kernel�ں���־��ӡ�������÷�ͬprintf*/
#define APP_KERNEL_LOG(format, ...)     {vTaskSuspendAll();\
	                                       APP_KERNEL_LOG_PORT(format, ##__VA_ARGS__);\
                                         if( xTaskResumeAll() == pdFALSE )\
                                            portYIELD_WITHIN_API();}

/*app_kernel��ʼ��*/
void app_kernel_Initialize(void);

#define akOK      1
#define akERR     0
typedef uint8_t AppKernel_t;
typedef void* AppKernel_px;

typedef void (*kernelFunction_t)( char**, uint32_t );       /*call�ص��������Ͷ���*/
typedef void (*kernelSignalFunc)(void*, uint32_t);          /*time�ص��������Ͷ���*/
typedef void (*slot_t)(void*, uint32_t);                    /*�ۺ������Ͷ���*/

typedef struct{
	slot_t     slot;
	void*      param;
	uint32_t   argc;
}app_kernel_signal;
typedef app_kernel_signal* signal_t;

typedef struct{
	uint32_t endTime;
}ak_nonBlockDelay_t;

/*�������û��ӿں�����ת�����壬�Ķ�����ע�ͼ���ʹ�ñ�ģ��*/
signal_t Signal(void);
void connect(signal_t signal, slot_t slot);
void emit(signal_t signal, void* param, uint32_t argc);
void emit_FromISR(signal_t signal, void* param, uint32_t argc);

/*call������غ���*/
void app_kernel_regist_user_call_function(const char* name, kernelFunction_t callbackFunc);

/*time������غ���*/
void app_kernel_call_after_times(char* serviceName, \
	kernelSignalFunc EntryFunction, void*param, uint32_t argc, uint32_t callTime);

/*��������ʱ*/
void app_kernel_non_blocking_delay_reset(ak_nonBlockDelay_t* akDelay, uint32_t ticksToDelay);
int  app_kernel_non_blocking_delay(ak_nonBlockDelay_t* akDelay);

/*�������������е��û�������ʹ��ʾ��������ָ���û����ʹ�������û��ӿں���
 ע�⣬�������������ʾ��������ֻ���ڴ����ն������뺯������Ȼ��س�������
 �����ն�����鿴����Ч��*/
void app_kernel_demo1(void* param, uint32_t);    /*call��timeʹ��ʾ��*/
void app_kernel_demo2(void* param, uint32_t);    /*�ź����ʹ��ʾ��*/
void app_kernel_demo3(void* param, uint32_t);    /*app_kernel_non_blocking_delay��д״̬��ʾ��*/

/*�պ����������ע����������ڿ����׶�ʹ�õ�*/
void app_kernel_test(void);

#endif
















