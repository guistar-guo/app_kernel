/**
  ******************************************************************************
  * @file           : app_kernel.c
  * @brief          : app_kernel�ǹ�����freertos����ϵͳ֮�ϵ�һ��Ӧ��ģ�飬��
  *                   �����ţ������ķ�ʽʵ����rtosϵͳ��һЩͬ�����������Ҷ�
  *                   �û��ṩ�����õĴ����ն˽������飡���ں�"call", "time",
  *                   "�ź����"��������Լ�������һЩС������磺ר����״̬��
  *                   ��̵ķ�������ʱ������
  ******************************************************************************
  * @attention ��ֲ��֮�������ֲ����˵����������app_kernel_Initialize���ɿ���
  * Ӧ���ں˵�ȫ�����ܣ���ʱ����ֲ�Ĵ����Ͽ��Բ鿴�ں˵Ĵ�ӡ��Ϣ��ע���������ʾ
  * ĳĳʧ�ܵ���Ϣ����������ڴ治���ˣ�����취�鿴���������ʵ���ǰ�configT
  * OTAL_HEAP_SIZE���ô�һ����ѣ�
  *
  * ����ģ�����ֲ�鿴��app_kernel��ʷ���е�bվ��Ƶ����
  * 
  * 1.call�����ʹ�ò鿴app_kernel_regist_user_call_function��ע�ͼ���
  * 
  * 2.time������ʵ����һ��������app_kernel_call_after_times���Ķ�ע�ͼ�������
  * 
  * 3.��ģ���ṩ��һ��ʱ����app_kernel_systick������freertosϵͳ��ʱ��Ƶ�ʱ���һ��
  *  ���⣬��ģ���ṩ��һ�������ʱ������app_kernel_systimer_callback������Ҳ��fre
  *  rtos��ʱ��
  *
  * 4.����ָ��ϵͳ�����ƣ�ͨ��APP_KERNEL_HEAT_LED��ָ��
  *
  * 5.֧�֡��ź���ۡ���ǿ����ƣ����ź���ۡ�ΰ��������ԣ�
  *
  * 6.�ܽ�һ��app_kernelĿǰ֧�ֵļ���ҵ��call����ʹ�û������ɵ��ڴ��������е���
  *   ĳ���ص�������time����ʹ�û��ӳٵ���ĳ�������������Ժ�call��ϣ�ʵ���ڴ�������
  *   ���ӳٵ���ĳ���������ź���ۣ��ǳ�������ͬ�����ƣ�ͬ������������������
  *   �жϵ�������ʵ��ʹ�ÿ��Խ�ǰ���������ϴﵽ��Ч
  * 
  * 7.��������˵������ģ����κκ�������Ӧ�����ж������������У�����Ϊ���������ԣ�
  *   ����ר�ſ����˿������ж����������е��ź���ۻ��ƣ��ṩ�˿������ж������е�
  *   �����źź���-emit_FromISR.
  *
  *   �����ν���ģ��Ӧ�õ��Լ��Ĵ����ϣ��ܼ򵥣�ֻ��Ҫ����ʵ��ͷ�ļ�ָ�������
  *   ��ֲ�ӿڼ��ɣ���������ʵ�⣬�����������f103c8t6�������������д�ģ�飬����
  *   ��׼����ֲ֮ǰ�����뱣֤freertos�ɹ��������ڴ�ռ���20000 bytes���ϣ�ԭ����
  *   ��ģ���freertos�ɹ�����ڴ��С��Ҫ���ڴ�̫С�ᵼ��ϵͳ�������豣֤config
  *   TOTAL_HEAP_SIZE�Ĵ�С������20000 Bytes���ϣ������е�ʱ��ֻҪ�������������⣬
  *   ���ȿ������ڴ治�����µģ����������棺��һ��freertos�ɹ�����ڴ��С��������
  *   ����config_TOTAL_HEAP_SIZE������ǰ���Ѿ��ᵽ������ˣ������ᵼ��ϵͳ���ϵ��
  *   �߸ո�λ�Ϳ��������ڶ�������Ƚϸ��ӣ�������ջ����ˣ�������⵼�µ������ǣ�
  *   ��ĳ���������е�ָ��������ͻῨ�������ߵ��ú�����������Ҳ�ᵼ�¿���
  *
  ******************************************************************************
  */
#include "app_kernel.h"

typedef struct{
	volatile uint32_t app_kernel_systick;
	QueueHandle_t user_cmd_queue;
	TaskHandle_t user_cmd_queue_Handle;
	volatile char user_uart_buf[USER_CMD_SIZE];
	volatile uint8_t user_uart_buf_cnt;
	volatile EventGroupHandle_t isSignalThreadBusy;
	TimerHandle_t app_kernel_systimer_handle;
}App_Kernel;

static App_Kernel app_kernel = {
	.app_kernel_systick = 0,
	.user_cmd_queue = NULL,
	.user_cmd_queue_Handle = NULL,
	.user_uart_buf_cnt = NULL,
	.isSignalThreadBusy = NULL,
	.app_kernel_systimer_handle = NULL
};

typedef struct{
	QueueHandle_t signal_queue;
	EventBits_t   EvenBit;
	TaskHandle_t  thread_handle;
	char name[20];
	uint8_t waitingToDisposeNum;
}signal_emit_param;
static signal_emit_param sep[MAX_SIGNAL_THREAD];

typedef struct timers{
	char Name[MAX_NAME];
	kernelSignalFunc EntryFunction;
	uint32_t callTime;
	void* param;
	uint32_t argc;
	struct timers* next;
	uint8_t isBusy;
}timercallback_item;
typedef timercallback_item* time_t;
static time_t the_time_head;
static timercallback_item timeNodeTable[MAX_TIME_CNT];
static volatile uint32_t nextCallTime = MAX_TIME;
static volatile uint8_t  time_cnt = 1;

typedef struct PFC_item{
	char Name[MAX_NAME];
	kernelFunction_t EntryFunction;
	struct PFC_item* next;
}axPFCItem, *PFC_t;

typedef struct{
	axPFCItem head;
	uint16_t itemNum;
}app_kernelPFC_l, *PFC_l;
static PFC_l aPendFunctionList=NULL;

static PFC_l app_kernel_PFClist_init(void){
	char head_name[] = "head";
	vTaskSuspendAll(); 
	PFC_l initedPFC_l;
	initedPFC_l = (PFC_l)pvPortMalloc(sizeof(app_kernelPFC_l));
	memcpy(initedPFC_l->head.Name, head_name, strlen(head_name)+1);
	initedPFC_l->head.EntryFunction = NULL;
	initedPFC_l->head.next = NULL;
	initedPFC_l->itemNum = 0;
	if( xTaskResumeAll() == pdFALSE ){ 
     portYIELD_WITHIN_API();
	}
	return initedPFC_l;
}

static PFC_t PFCitemInit(kernelFunction_t EntryFunc, char*Name){
	PFC_t PFCitemToInit;
	PFCitemToInit = (PFC_t)pvPortMalloc(sizeof(axPFCItem));
	PFCitemToInit->EntryFunction = EntryFunc;
	memcpy(PFCitemToInit->Name, Name, strlen(Name)+1);
	PFCitemToInit->next = NULL;
	return PFCitemToInit;
}

static void aPFCAdd(PFC_t PFCitem_to_add, PFC_l PFC_LIST){
	vTaskSuspendAll(); 
	PFC_t p = &PFC_LIST->head;
  while(p->next != NULL){
		p = p->next;
	}
	p->next = PFCitem_to_add;
	PFC_LIST->itemNum++;
	if( xTaskResumeAll() == pdFALSE ){ 
    portYIELD_WITHIN_API();
	}
}

static void aPFCDel(char*NameToDel, PFC_l PFC_LIST){
	vTaskSuspendAll(); 
	PFC_t p = &PFC_LIST->head;
	while(p->next != NULL && strcmp(p->next->Name, NameToDel)){
		p = p->next;
	}
	if(p->next == NULL){
		APP_KERNEL_LOG("app_kernel���Ҳ�����ɾ����������%s\r\n", NameToDel);
		if( xTaskResumeAll() == pdFALSE ){ 
       portYIELD_WITHIN_API();
	  }
		return;
	}
	PFC_t itemtoDel = p->next;
	p->next = itemtoDel->next;
	PFC_LIST->itemNum--;
	vPortFree(itemtoDel);
	if( xTaskResumeAll() == pdFALSE ){ 
     portYIELD_WITHIN_API();
	}
}

static void showPFCList(PFC_l PFC_LIST){
	APP_KERNEL_LOG("app_kernel: ��ǰϵͳ��ע���call���������¼�����\r\n");
	vTaskSuspendAll(); 
	PFC_t p = &PFC_LIST->head;
	if(p->next == NULL){
		if( xTaskResumeAll() == pdFALSE ){ 
       portYIELD_WITHIN_API();
	  }
		return ;
	}
	else p = p->next;
	while(p!=NULL){
		APP_KERNEL_LOG("%s \r\n", p->Name);
		p = p->next;
	}
	if( xTaskResumeAll() == pdFALSE ){ 
       portYIELD_WITHIN_API();
	}
}

/**************************************************************************
�������ܣ�ע�ᴮ���ն˵�call����Ļص�������ע��֮���ڴ���������ͨ��
          call name����ʽ������Ŀ��ص�������
��ڲ�����name �ص��������ն˵��õ����֡�
          callbackFunc �ص�����ָ�룬�����ͱ�����kernelFunction_t����
����  ֵ����
**************************************************************************/
void app_kernel_regist_user_call_function(const char* name, kernelFunction_t callbackFunc){
	vTaskSuspendAll(); 
	PFC_t p = &aPendFunctionList->head;
  while(p->next != NULL && strcmp(p->Name, name)){
		p = p->next;
	}
	
	/*Ϊ����ǰ���ѵ��������ռ任ʱ��*/
	char* p_Name = p->Name;
	if( xTaskResumeAll() == pdFALSE ){ 
    portYIELD_WITHIN_API();
	}
	if(!strcmp(p_Name, name)){
		APP_KERNEL_LOG("app_kernel��ϵͳ���Ѿ�ע����call����%s�������ظ���ע��\r\n", name);
		return ;
	}
  PFC_t temp = PFCitemInit(callbackFunc, (char*)name);									 
	aPFCAdd(temp, aPendFunctionList);
}

/**************************************************************************
�������ܣ�ע�������ն˵�call����Ļص�����
��ڲ�����name �ص��������ն˵��õ�����;
����  ֵ����
**************************************************************************/
void app_kernel_del_user_call_function(const char* name){
	aPFCDel((char*)name, aPendFunctionList);
}

void app_kernel_call_user_func(const char*name,\
	char*params[], uint32_t argc, TickType_t xTicksToWait){
	vTaskSuspendAll(); 
	PFC_t p = &aPendFunctionList->head;
	while(p->next!=NULL && strcmp(name, p->Name)) p = p->next;
		
	/*Ϊ����ǰ�������������ռ任ʱ��*/
	char* p_name = p->Name;
	if( xTaskResumeAll() == pdFALSE ){ 
      portYIELD_WITHIN_API();
	}
	if(!strcmp(name, p_name)){
		xTimerPendFunctionCall((PendedFunction_t)p->EntryFunction, params, argc, xTicksToWait);
	}else{
		APP_KERNEL_LOG("app_kernel��ϵͳ��δע��˺��������麯�������Ƿ���ȷ��");
	}
}

static time_t app_kernel_time_head_init(void){
	vTaskSuspendAll(); 
	time_t time;
	time = &timeNodeTable[0];
	memcpy(time->Name, "head", sizeof("head"));
	time->EntryFunction = NULL;
	time->callTime = MAX_TIME;
	time->next = NULL;
	time->argc = 0;
	time->param = NULL;
	if ( xTaskResumeAll() == pdFALSE ) { 
       portYIELD_WITHIN_API();
	}
	return time;
}

static time_t app_kernel_time_item_init(char*Name, \
	    kernelSignalFunc EntryFunction, void* param, uint32_t argc, uint32_t callTime){
	uint8_t idleTime = 1;
	for(; idleTime<MAX_TIME_CNT; idleTime++){
		if(timeNodeTable[idleTime].isBusy == 0)
			break;
	}
	if(idleTime == MAX_TIME_CNT) return NULL;
	time_t time = &timeNodeTable[idleTime];
	if(param != NULL){
		memcpy(time->param, param, argc);
	  time->argc = argc;
	}
	memcpy(time->Name, Name, strlen(Name)+1);
	time->EntryFunction = EntryFunction;
	time->callTime = callTime + app_kernel.app_kernel_systick;
	time->next = NULL;
	return time;
}
			

static void app_kernel_timer_add(time_t time_head, time_t time_to_add){
	if(time_cnt >= MAX_TIME_CNT){
		return ;
	}
	time_t time = time_to_add;
	 vTaskSuspendAll(); 
	if(time_head->next == NULL){
		time_head->next = time;
		nextCallTime = time->callTime;
		time_cnt++;
		time->isBusy = 1;
		if( xTaskResumeAll() == pdFALSE ){
       portYIELD_WITHIN_API();
	  }
		return ;
	}
	uint32_t time_call_time = time->callTime;
	time_t back = time_head->next;
	time_t forward = time_head;
	while(back!=NULL && back->callTime < time_call_time){
		back = back->next;
		forward = forward->next;
	}
	forward->next = time;
	time->next = back;
	time_cnt++;
	time->isBusy = 1;
	if(nextCallTime>time->callTime){
		nextCallTime = time->callTime;
	}
	 if ( xTaskResumeAll() == pdFALSE ) { 
       portYIELD_WITHIN_API();
	 }
}

static void app_kernel_time_del(time_t time_head, char* time_to_del){
	vTaskSuspendAll(); 
	time_t p = time_head;
	while(p->next != NULL && strcmp(p->next->Name, time_to_del)){
		p = p->next;
	}
	if(p->next == NULL){
		APP_KERNEL_LOG("app_kernel: �Ҳ�����ɾ����������%s\r\n", time_to_del);
		if ( xTaskResumeAll() == pdFALSE ) { 
       portYIELD_WITHIN_API();
	  }
		return;
	}
	time_t itemtoDel = p->next;
	p->next = itemtoDel->next;
	if(nextCallTime == itemtoDel->callTime){
		nextCallTime = the_time_head->next->callTime;
	}
	time_cnt--;
	itemtoDel->isBusy = 0;
	if ( xTaskResumeAll() == pdFALSE ) { 
       portYIELD_WITHIN_API();
	 }
}

void app_kernel_show_times(void){
	APP_KERNEL_LOG("app_kernel: ��ǰ�ں��к��е�time���������¼�����\
	�����յ���ʱ����Ⱥ�˳�����У�\r\n");
	vTaskSuspendAll(); 
	time_t p = the_time_head->next;
	if(p == NULL){
		if( xTaskResumeAll() == pdFALSE ){ 
       portYIELD_WITHIN_API();
	  }
		return;
	}
	while(p->next!=NULL){
		APP_KERNEL_LOG("%s->",p->Name);
		p = p->next;
	}
	if( xTaskResumeAll() == pdFALSE ){ 
       portYIELD_WITHIN_API();
	}
	APP_KERNEL_LOG("%s\r\n",p->Name);
}

void app_kernel_time_init(void){
	the_time_head = app_kernel_time_head_init();
}

/**************************************************************************
�������ܣ�time���񣬵��ô˺��������ڵ�ǰʱ��֮��callTime��ϵͳ����֮�����
          EntryFunction����ָ���ĺ������ڴ��ڼ䵥Ƭ������ִ������������Ҳ
          ����˵�˺����Ƿ������ģ�����֮�����̷��أ����˷�cpu�ľ��������⣬
          ʱ�䵽��֮��ִ�к���EntryFunction�������������������ģ�Ҳ����˵
          EntryFunction�����ǿ���ִ��freertos�ṩ����ʱ�����ģ����ǲ��Ƽ���
          ����������Ϊ���ܻ�Ӱ������time����Ļص������ĵ���ʱ�����ص�����
          ����������������Ϊʲô���뿴ע������

ע�����(1)������ȷһ�㣬time�ǲ����Թ���Ƶ���ĵ��õģ���������޷������˭��
          Ҳ�޷������Ϊʲô������һ�£�����ǳ�Ƶ���ĵ��ã�������Ƶ�ʴ���time
          �ص������Ĵ���Ƶ�ʣ��ͻᵼ��ϵͳ�����е�timeֻ����������ϵͳ���ڴ�
          �����޵ģ��ܻ��б�������һ�̣����ԣ�time�ĵ���Ƶ�ʱ���С��time�ص�
          �����Ĵ���Ƶ�ʣ��������ִ��ڵ��������ϵͳ��ͬʱ���ڵ�time��������
          ��MAX_TIME_CNTʱ��app_kernel�ں���־���򴮿��ն�ǿ�Ƴ�����ӡ������Ϣ��

          MAX_TIME_CNT����궨�������ϵͳ������ͬʱ���ڵ�time��������������
          ����Խ��Խ�ã�̫���ˣ��ᵼ��ϵͳ�ڴ治����������������ͨ��onfig
          TOTAL_HEAP_SIZE������freertos���ɹ����ڴ棩
 
          (2)����ע�⣬�˺�����ֹ���ж������ĵ��ã����ǿ���ͨ�����ж������ķ���
				  �źţ����źŵĲۺ��������������ģ�����time

��ڲ�����serviceName�� ָ�����η�������ƣ�����ͨ��app_kernel_show_times����
                        �鿴��ǰϵͳ�е�����time��������Ƽ��ϣ������û�������
                        ͬ���ֵ�time���񣬲������Ƽ���
          EntryFunction ָ������ָ��ʱ��֮��Ҫ���õĺ�����
          param��       ��EntryFunction��������ķ���ָ������������û����ݿ�
                        ָ��NULL������������£���������һ������argc��
          argc��        ָ��param����ָ��ָ����ڴ��С����λ���ֽڡ�
          callTime��    ��ǰʱ��֮��callTime��ϵͳ����֮�����
                        EntryFunction����ָ���ĺ�����
����  ֵ����
**************************************************************************/
void app_kernel_call_after_times(char* serviceName, \
	kernelSignalFunc EntryFunction, void*param, uint32_t argc, uint32_t callTime){
	time_t time = app_kernel_time_item_init(serviceName, EntryFunction, param, argc, callTime);
	app_kernel_timer_add(the_time_head, time);
}

/**************************************************************************
�������ܣ���ӡĳ������İ����ĵ�   
��ڲ�������
����  ֵ����
**************************************************************************/
static void user_cmd_help(char* user_command){
	APP_KERNEL_LOG("\r\n");
  if(!strcmp(user_command, "call")){
		APP_KERNEL_LOG("����call�÷���\r\n");
		APP_KERNEL_LOG("���ȣ��û���Ҫ���ж���һ��kernelFunction_t���͵ĺ��������Բο�call_testFunction����������\r\n");
		APP_KERNEL_LOG("��Σ�����app_kernel_regist_user_call_function�������ں���ע��һ��call����ص�����\r\n");
		APP_KERNEL_LOG("������ն˵���call+�ո�+ע�����ں��еĻص��������֣����ɵ���ע��õĺ���\r\n");
		APP_KERNEL_LOG("���⣬��֧�����ն�������������磺call testFunc 123 456�����ڻص������У�param1[0]��ֵ\r\n");
		APP_KERNEL_LOG("��123��param[1]��ֵ��456��param2��ֵ��2����ʾ�û����ն������������������ο�call_testFunc\r\n");
		APP_KERNEL_LOG("tion�����˽�����ڻص������ڲ������ն˴������ĵĲ���\r\n");
		APP_KERNEL_LOG("����call list ���Բ鿴ϵͳ��ǰע������лص����������\r\n");
	}
	else if(!strcmp(user_command, "time")){
		APP_KERNEL_LOG("time�����÷���\r\n");
		APP_KERNEL_LOG("���ȣ��û���Ҫ���ж���һ��kernelFunction_t���͵ĺ��������Բο�call_testFunction����������\r\n");
		APP_KERNEL_LOG("��Σ�����app_kernel_call_after_times�������ں���ע��һ��time���񼴿ɡ�\r\n");
		APP_KERNEL_LOG("����Ч���ǣ��ڵ�ǰʱ��֮��ĵ�callTime������app_kernel_call_after_times�βΣ���ϵͳ����\r\n");
		APP_KERNEL_LOG("����ָ���Ļص����������ӳٵ��ù���\r\n");
	}
	else if(!strcmp(user_command, "signal")){
		APP_KERNEL_LOG("signal and slot �ź���ۻ��ƣ���Qt����ؽӿڱ��ָ߶�һ�£���Ҫapi������\r\n");
		APP_KERNEL_LOG("Signal                                  ��     ע���ź�\r\n");
		APP_KERNEL_LOG("connect                                 ��     �����ź����\r\n");
		APP_KERNEL_LOG("emit_FromISR                            ��     �����źţ��ж������ģ�\r\n");
		APP_KERNEL_LOG("emit                                    ��     �����źţ����������ģ�\r\n");
		APP_KERNEL_LOG("���Ͼ����ź���۵���غ������˽����ϸ�÷������Ķ����ϼ���������ע��.\r\n");
	}
	else{
		APP_KERNEL_LOG("app_kernel: ϵͳδ֧�ִ����\r\n\r\n");
	}
}

/**************************************************************************
�������ܣ���ӡϵͳ֧�ֵ����е����������Ϣ   
��ڲ�������
����  ֵ����
**************************************************************************/
static void user_all_cmd_help(void){
	APP_KERNEL_LOG("�ں�֧�������������\r\n\r\n");
	APP_KERNEL_LOG("**************************************************************************************\r\n");
	APP_KERNEL_LOG("| call��             |�˽����飬������'help call'                                    |\r\n");
	APP_KERNEL_LOG("| time:              |time������ʵ����app_kernel_call_after_times�����Ķ���ע���˽�  |\r\n");
  APP_KERNEL_LOG("| signal and slot:   |�˽����飬������'help signal'                                  |\r\n");
	APP_KERNEL_LOG("**************************************************************************************\r\n");
	APP_KERNEL_LOG("ÿ���������ϸ�������Ϳ�ͨ����help + �ո� + �������֡�����ʽ�鿴����: help signal(�ǵüӻس���)\r\n");
	APP_KERNEL_LOG("\r\n���⣬��������ʾ������app_kernel_demo1��app_kernel_demo2��app_kernel_demo3\r\n\
app_kernel_demo1��ʾ��call�����time�����ʹ�ã�app_kernel_demo2��ʾ���ź���۵�ʹ��\r\n\
��app_kernel_demo3��ʾ��app_kernel�ṩ�ķ�������ʱ�������Ӧ����״̬������ϣ��ṩ��һ��״̬��ʾ������\r\n");
	APP_KERNEL_LOG("ֱ�����롰help�����Բ鿴ϵͳ��ǰ֧�ֵ�����������������\r\n\r\n");
}

/**************************************************************************
�������ܣ������û��������user_cmd_queue��������������ַ�����ʽ�������
          ���ں�����͸��ں˴���
��ڲ�������
����  ֵ����
**************************************************************************/
static void app_kernel_dispose_user_cmd(void){
	 BaseType_t xReturn = pdTRUE;
	 char user_orig_cmd_buf[USER_CMD_SIZE];
	 const char delim[2] = " ";
	 char* cmd_user_to_kernel[CMD_SEG_MAX_NUM];
	 char* user_call_param[CMD_SEG_MAX_NUM];
   uint8_t user_cmd_seg_cnt = 0;
	 char* temp;
	 signal_t demo1 = Signal();
	 signal_t demo2 = Signal();
	 signal_t demo3 = Signal();
	 connect(demo1, app_kernel_demo1);
	 connect(demo2, app_kernel_demo2);
	 connect(demo3, app_kernel_demo3);
	 while (1){
    xReturn = xQueueReceive( app_kernel.user_cmd_queue, user_orig_cmd_buf, 0);
		if(pdTRUE == xReturn){
			APP_KERNEL_LOG("//////////////////////////////////////////////////////////////////////////////////////\r\n");
			APP_KERNEL_LOG("app_kernel: �û����͵�ԭʼ���%s\r\n", user_orig_cmd_buf);
			
			cmd_user_to_kernel[0] = strtok((char*)user_orig_cmd_buf, delim);
			user_cmd_seg_cnt = 1;
			temp = strtok(NULL, delim);
			while(user_cmd_seg_cnt <= CMD_SEG_MAX_NUM && \
			temp != NULL){
				user_cmd_seg_cnt++;
				cmd_user_to_kernel[user_cmd_seg_cnt-1] = temp;
				temp = strtok(NULL, delim);
			}
			if(user_cmd_seg_cnt > CMD_SEG_MAX_NUM){
				APP_KERNEL_LOG("app_kernel: �����������������ϵͳ����!\r\n");
				continue;
			}
//			else{
//				for(int i = 0; i<user_cmd_seg_cnt; i++){
//					APP_KERNEL_LOG("��%d�Σ�%s  ",i ,cmd_user_to_kernel[i]);
//				}
//			}
			APP_KERNEL_LOG("\r\n");
			
			if (!strcmp(cmd_user_to_kernel[0], "help")){
				if(user_cmd_seg_cnt > 2){
					APP_KERNEL_LOG("app_kernel: help����������࣬ע�⣺help����\
					               ÿ��ֻ�ܴ���һ������\r\n");
					continue;
				}
				else if(user_cmd_seg_cnt == 1){
					user_all_cmd_help();
					continue;
				}
				else if(user_cmd_seg_cnt == 2){
					user_cmd_help(cmd_user_to_kernel[1]);
					continue;
				}
	    }
			else if (!strcmp(cmd_user_to_kernel[0], "call")){
				if(user_cmd_seg_cnt == 1){
					APP_KERNEL_LOG("app_kernel: ��������!\r\n");
					user_cmd_help("call");
				}else if(user_cmd_seg_cnt == 2){
				   if(!strcmp(cmd_user_to_kernel[1], "list")){
						 showPFCList(aPendFunctionList);
					 }else 
					    app_kernel_call_user_func(cmd_user_to_kernel[1],\
        					 user_call_param, user_cmd_seg_cnt-2, 500);
				 }
				else{
					for(int i = 2; i<user_cmd_seg_cnt; i++){
						user_call_param[i-2] = cmd_user_to_kernel[i];
					}
					app_kernel_call_user_func(cmd_user_to_kernel[1], \
					  user_call_param, user_cmd_seg_cnt-2, 500);
				}
			}
			else if (!strcmp(cmd_user_to_kernel[0], "time")){
				user_cmd_help("time");
			}
			else if (!strcmp(cmd_user_to_kernel[0], "signal")){
				user_cmd_help("signal");
			}
			else if(!strcmp(cmd_user_to_kernel[0], "app_kernel_demo1")){
				emit(demo1, NULL, 0);
			}
			else if(!strcmp(cmd_user_to_kernel[0], "app_kernel_demo2")){
				emit(demo2, NULL, 0);
			}
			else if(!strcmp(cmd_user_to_kernel[0], "app_kernel_demo3")){
				emit(demo3, NULL, 0);
			}
			else{
				APP_KERNEL_LOG("app_kernel: ����������飡\r\n");
			}
		}
		vTaskDelay(1);
	}
}

static void app_kernel_dispose_sigal(void* param){
	app_kernel_signal signal;
	signal_emit_param* sepR = (signal_emit_param* )param;
	while(1){
	  EventBits_t theSetBits = xEventGroupWaitBits(app_kernel.isSignalThreadBusy,\
    sepR->EvenBit, pdFALSE, pdTRUE, 0);
	  if(theSetBits & sepR->EvenBit){
		  while(xQueueReceive( sepR->signal_queue, &signal, 0)){
				signal.slot(signal.param, signal.argc);
				vTaskSuspendAll();
				sepR->waitingToDisposeNum--;
				if( xTaskResumeAll() == pdFALSE )
					 portYIELD_WITHIN_API();
		  }
		  xEventGroupClearBits(app_kernel.isSignalThreadBusy, sepR->EvenBit);
	  }
		vTaskDelay(1);
	}
}

void app_kernel_timer_daemon(void){
	if(time_cnt >= MAX_TIME_CNT){
		APP_KERNEL_LOG("app_kernel:����!time�������Ƶ�������ǽ�������Ƶ�������ϵͳʵʱ��\r\n");
		APP_KERNEL_LOG("app_kernel:Ҳ����ѡ������MAX_TIME_CNT�궨�壬������ϵͳ���ڴ治������ķ���\r\n");
	}
	vTaskSuspendAll();
	time_t p = the_time_head->next;
	while(p!=NULL && app_kernel.app_kernel_systick >= nextCallTime){
		time_t nex = p->next;
		if(nex != NULL){
			nextCallTime = nex->callTime;
		}else{
			nextCallTime = MAX_TIME;
		}
		if( xTaskResumeAll() == pdFALSE ){
       portYIELD_WITHIN_API();
	  }
		p->EntryFunction(p->param, p->argc);
		vTaskSuspendAll();
		app_kernel_time_del(the_time_head, p->Name);
		p = nex;
	}
	if( xTaskResumeAll() == pdFALSE ){
       portYIELD_WITHIN_API();
	}
}

/**************************************************************************
�������ܣ�Ӧ���ں˹���������ʱ��
��ڲ�������
����  ֵ����
**************************************************************************/
static void app_kernel_systimer_callback(void* parameter){
	if(!(++app_kernel.app_kernel_systick%200))
	{
		APP_KERNEL_HEAT_LED;
	}
	
	/*ÿ��500��ϵͳ���ģ���ӡһ�ε���ϵͳ��ʣ���ڴ�*/
//	if(app_kernel_systick%500 == 0){
//		size_t remainBytes = xPortGetMinimumEverFreeHeapSize();
//		APP_KERNEL_LOG("app_kernel:ϵͳʣ���ڴ�:%d bytes\r\n", remainBytes);
//	}
	app_kernel_timer_daemon();
}

/**************************************************************************
�������ܣ���ʼ����ģ�飬����app_kernel����ֲ���ڳ�ʼ����֮��ĵط����ô�
          ���������ù��˺�����ʹ�ñ�ģ������й��ܵ�ǰ��. 
��ڲ�������
����  ֵ����
**************************************************************************/
void app_kernel_Initialize(void){
	/*������rtos�ں˶���Ĵ�����rtos�ں˶���һ��Ҫ�����ڴ�������֮ǰ��������������ϵͳ����
	�����ϵͳ������ԭ��ָ��ΪNULL��ʱ��������ͼ����ָ�룩*/
	BaseType_t xReturn = pdPASS;
	taskENTER_CRITICAL();
	for(int i = 0; i<MAX_TIME_CNT ;i++){
		timeNodeTable[i].isBusy = 0;
		timeNodeTable[i].param = pvPortMalloc(SIGNAL_PARAM_MAX_SIZE);
	}
	app_kernel.user_cmd_queue = xQueueCreate((UBaseType_t ) QUEUE_LEN,\
	    (UBaseType_t ) USER_CMD_SIZE);
	app_kernel.isSignalThreadBusy = xEventGroupCreate();
	aPendFunctionList = app_kernel_PFClist_init();
	app_kernel_time_init();
	app_kernel.app_kernel_systimer_handle=xTimerCreate((const char*		)"app_kernel_systimer",
                            (TickType_t			)1,
                            (UBaseType_t		)pdTRUE,
                            (void*				  )1,
                            (TimerCallbackFunction_t)app_kernel_systimer_callback); 
	if(NULL == app_kernel.app_kernel_systimer_handle){
		APP_KERNEL_LOG("app_kernel: ���������ʱ��ʧ��!\r\n");
		while(1){}
	}
	else{
		xTimerStart(app_kernel.app_kernel_systimer_handle,0);
	}
	 char temp[20];
	 for(int i = 0; i<MAX_SIGNAL_THREAD; i++){
		 sep[i].waitingToDisposeNum = 0;
		 sprintf(temp, "signal_thread%c", (char)(i+'0'));
     memcpy(sep[i].name, temp, 20);
		 sep[i].EvenBit = 0x01<<i;
		 sep[i].signal_queue = xQueueCreate((UBaseType_t ) QUEUE_LEN,\
		     (UBaseType_t ) sizeof(app_kernel_signal));
	   xReturn = xTaskCreate((TaskFunction_t )app_kernel_dispose_sigal,
                        (const char*    )sep[i].name,
                        (uint16_t       )SIGNAL_THREAD_STACK,
                        (void*          )&sep[i],
                        (UBaseType_t    )24,
                        (TaskHandle_t*  )&sep[i].thread_handle);
     if(pdPASS != xReturn){
			  APP_KERNEL_LOG("app_kernel: ����%s����ʧ��!\r\n", sep[i].name);
        while(1){}
		 }
	 }
		xReturn = xTaskCreate((TaskFunction_t )app_kernel_dispose_user_cmd,
                        (const char*    )"user_cmd_to_kernel",
                        (uint16_t       )128,
                        (void*          )NULL,
                        (UBaseType_t    )24,
                        (TaskHandle_t*  )&app_kernel.user_cmd_queue_Handle);
    if(pdPASS != xReturn){
			APP_KERNEL_LOG("app_kernel: ����user_cmd_queue_Handle����ʧ��!\r\n");
			while(1){}
		}
	taskEXIT_CRITICAL();
	APP_KERNEL_LOG(" \r\n");
	APP_KERNEL_LOG(" \r\n");
	APP_KERNEL_LOG("      \\|/   \r\n");
	APP_KERNEL_LOG("      -AK-    Ӧ���ںˣ���ʼ����...\r\n");
	APP_KERNEL_LOG("      /|\\   \r\n");
	APP_KERNEL_LOG("************************************************************************\r\n");
	APP_KERNEL_LOG("��ӭʹ��##��app_kernel��##����ģ�齨�齫������������Ϊȫ��ʹ��Ч������\r\n");
	APP_KERNEL_LOG("************************************************************************\r\n");
	APP_KERNEL_LOG("����ÿһ������֮�󣬶�������׷��һ���س�����ֱ�Ӽ������ûس���\r\n\r\n");
	APP_KERNEL_LOG("����help������Բ鿴������Ϣ\r\n");
	user_all_cmd_help();
}

/**************************************************************************
�������ܣ��жϻص����������ж��������в��������������飬���ʹ��freertos��
          �л��ƽ��û�����͵�user_cmd_queue����app_kernel_dispose_user_c
          md�����û�����
          
��ڲ�����user_command �û�ͨ�����ڷ��͹�����ԭʼ�����������Ͳ���������
����  ֵ����
**************************************************************************/
static void send_user_command_to_kernel(const char* user_command){
	BaseType_t xReturn = pdPASS;
	BaseType_t pxHigherPriorityTaskWoken;
  xReturn = xQueueSendFromISR( app_kernel.user_cmd_queue,\
    	user_command,&pxHigherPriorityTaskWoken );
	portYIELD_FROM_ISR(pxHigherPriorityTaskWoken); 
  if(pdPASS != xReturn)
    APP_KERNEL_LOG("app_kernel: ��Ϣsend_data1����ʧ��!\r\n");
}

/**************************************************************************
�������ܣ������ն˵Ĵ��ڻص���������stm32Ӳ�����ڽ��յ����ֽڵ��жϺ���
          �е���
��ڲ�������
����  ֵ����
**************************************************************************/
void app_kernel_uart_callback(uint8_t recByte){
  /*ulReturn = taskENTER_CRITICAL_FROM_ISR();*/
	while(1){
		app_kernel.user_uart_buf[app_kernel.user_uart_buf_cnt] = recByte;
		app_kernel.user_uart_buf_cnt++;
		if(recByte == '\r' || recByte == '\n'){
			if(app_kernel.user_uart_buf_cnt == 1){
				app_kernel.user_uart_buf_cnt = 0;
				break;
			}
			else if(app_kernel.user_uart_buf_cnt == 2 && (app_kernel.user_uart_buf[0]\
  			== '\r' || app_kernel.user_uart_buf[0] == '\n')){
				app_kernel.user_uart_buf_cnt = 0;
				break;
			}
			else{
				app_kernel.user_uart_buf[app_kernel.user_uart_buf_cnt-1] = '\0';
				send_user_command_to_kernel((const char*)app_kernel.user_uart_buf);
				app_kernel.user_uart_buf_cnt = 0;
				break;
			}
		}
		break;
	}
  /*taskEXIT_CRITICAL_FROM_ISR(ulReturn);*/
}

/**************************************************************************
�������ܣ� ��λak_nonBlockDelay_t���󣬴Ӵ˿̿�ʼ��ʱ���ڵ�ǰʱ��ticksToDelay
           ��ticks֮�󣬵���app_kernel_non_blocking_delay��������testDelay��
           �󽫷���1���ڴ�֮ǰ������app_kernel_non_blocking_delay������0
          
��ڲ�����ticks ��ʱʱ��
����  ֵ����
**************************************************************************/
void app_kernel_non_blocking_delay_reset(ak_nonBlockDelay_t* akDelay, uint32_t ticksToDelay){
	akDelay->endTime = ticksToDelay + app_kernel.app_kernel_systick;
}

/**************************************************************************
�������ܣ�����������������ak_nonBlockDelay_tָ�룬���̷��أ����
          ak_nonBlockDelay_t����ָ����ʱ�̵�������̷���1���������̷���0
          
��ڲ�����ticks ��ʱʱ��
����  ֵ����
**************************************************************************/
int app_kernel_non_blocking_delay(ak_nonBlockDelay_t* akDelay){
	return app_kernel.app_kernel_systick >= akDelay->endTime ? 1 : 0;
}

/**************************************************************************
�������ܣ�ע��һ���ź�        
��ڲ�������
����  ֵ��signal_t ����ע��õ��źţ������Ӧ�ó������ȶ���һ��signal_t
          ���͵�ȫ�ֱ�����Ȼ����ĳ�������е��ô˺�����������ֵ�����������
          ΪʲôҪ�����ȫ�ֱ�������Ϊֻ�����������ڵ�ǰϵͳ����������
          �ж��е��÷����źź���emit
**************************************************************************/
signal_t Signal(void){
	signal_t signal = (signal_t)pvPortMalloc(sizeof(app_kernel_signal));
	signal->param = (void*)pvPortMalloc(sizeof(SIGNAL_PARAM_MAX_SIZE));
	signal->slot = NULL;
	signal->argc = 0;
	return signal;
}

/**************************************************************************
�������ܣ������ź����       
��ڲ�����signal    Ҫ���ӵ��źţ����뱻Signal������ʼ������
          slot      Ҫ���ӵĲۺ���ָ�룬��slot_t���ͣ���ת��app_kernel.h��
                    ���鿴�䶨��
����  ֵ����
**************************************************************************/
void connect(signal_t signal, slot_t slot){
	if(signal == NULL) return ;
	signal->slot = slot;
}

/**************************************************************************
�������ܣ������źţ�����ǰ�������������飺��һ��ͨ��Signal����ע��һ���ź�
          �ڶ���ͨ��connect������ע��õ��ź�����һ���ۡ���������������㣬
          ��emit���ɹ������źţ�app_kernelģ��ᾡ��������ȥ���òۺ�����

ע�⣺    �˺�����ֹ���жϺ�����ʹ�ã�����app_kernelģ���ṩ�˴˺������ж�
          �汾emit_FromISR
��ڲ�����signal��Ҫ���͵��ź�
          param�� ���źŵĲ������ݵ�void*���͵�ָ�����
          argc��  ָ��paramָ��ָ����ڴ�ռ��С���������һ��Ҫ��ȷ���ã�
                  ����paramָ��һ���ַ���"hello"�����������Ĵ�С��6���ֽڣ�
                  �����ʱָ��argcΪ3����ֻ�ܴ���'h','e','l'������ĸ��param
                  �������ַ���������־'\0'Ҳû�д��룬����п��ܵ��º�����
                  �ַ������������Ƿ����ʣ������ָ��argcΪ7�����ֱ�ӵ���
                  param�Ƿ����ʣ�ϵͳ���ٱ���Σ�գ������argc��ֵһ��Ҫ���أ�
����  ֵ����
**************************************************************************/
void emit(signal_t signal, void* param, uint32_t argc){
	BaseType_t xReturn = pdPASS;
	if(signal == NULL || signal->slot == NULL) return;
	UBaseType_t minNum = QUEUE_LEN+1;
	vTaskSuspendAll();
	
	/*�����ط�(����������������һ��bug)������(2025/1/27)��Ϊ
	  ��sepTָ���ʼ��ΪNULL�����º������ʿ�ָ����ʹϵͳ����*/
	signal_emit_param* sepT = &sep[0];
	if(param != NULL){
		memcpy(signal->param, param, argc);
	}
	signal->argc = argc;
	for(int i = 0 ;i<MAX_SIGNAL_THREAD; i++){
		if(minNum > sep[i].waitingToDisposeNum){
			sepT = &sep[i];
			minNum = sepT->waitingToDisposeNum;
		}
	}
	sepT->waitingToDisposeNum = (sepT->waitingToDisposeNum + 1) > QUEUE_LEN ?\
     QUEUE_LEN : (sepT->waitingToDisposeNum + 1);
	if( xTaskResumeAll() == pdFALSE )
     portYIELD_WITHIN_API();
	xReturn = xQueueSend(sepT->signal_queue, signal, 0);
	if(xReturn == errQUEUE_FULL)
		APP_KERNEL_LOG("app_kernel:���棺�źŷ���Ƶ�ʹ��ߣ����ܶ�ʧ�����źţ�\r\n");
	xEventGroupSetBits(app_kernel.isSignalThreadBusy, sepT->EvenBit);
}

/**************************************************************************
�������ܣ�emit���жϰ汾�����жϺ�����ʹ��
��ڲ�������
����  ֵ����
**************************************************************************/
void emit_FromISR(signal_t signal, void* param, uint32_t argc){
	BaseType_t pxHigherPriorityTaskWoken;
	if(signal == NULL || signal->slot == NULL) return;
	UBaseType_t minNum = QUEUE_LEN+1;	
	signal_emit_param* sepT = &sep[0];
	if(param != NULL){
		memcpy(signal->param, param, argc);
	}
	signal->argc = argc;
	for(int i = 0 ;i<MAX_SIGNAL_THREAD; i++){
		if(minNum > sep[i].waitingToDisposeNum){
			sepT = &sep[i];
			minNum = sepT->waitingToDisposeNum;
		}
	}
	sepT->waitingToDisposeNum = (sepT->waitingToDisposeNum + 1) > QUEUE_LEN ?\
                           	QUEUE_LEN : (sepT->waitingToDisposeNum + 1);
	xQueueSendFromISR(sepT->signal_queue, signal, 0);
	portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
	xEventGroupSetBitsFromISR(app_kernel.isSignalThreadBusy, sepT->EvenBit,\
      	&pxHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
}

/**************************************************************************
�������ܣ�ɾ��ϵͳ�е��źţ�������������ò�������ΪֻҪ��ע���źţ��϶�
          ���������ģ�������֮�󣬳��Ǳ�֤֮����Զ���������ˣ������Ļ���
          ���ô˺���ɾ������źţ�������Զ��Ҫ���������������Ϊ����ź�
          ɾ���ˣ�����һ��С���ٵ��÷����źź�����������źţ���ϵͳ����
          ���������Ƿ����ʣ�
��ڲ�����signal_t  ָ��Ҫ��ϵͳ��ɾ�����ź�
����  ֵ����
**************************************************************************/
void del_signal(signal_t signalToDel){
	if(signalToDel == NULL) return ;
	vPortFree(signalToDel->param);
	vPortFree(signalToDel);
}

/*����������ʾ�����룬��������ϵͳ�ġ��ź���ۡ���call��������ϣ��ﵽ
  �����ڴ����ն�����ָ�������õ�Ч�����磺����app_kernel_demo1���ӻس�
  �����ɵ���ʾ������1������ʾ������Ϳ���ֱ����������app_kernel���Լ�
  ����Ŀ����*/

/**************************************************************************
�������ܣ�call��timeʾ��
��ڲ�������
����  ֵ����
**************************************************************************/
/*time����ص�����*/
void time_testFunction(void* param,uint32_t argc){
	APP_KERNEL_LOG("%s\r\n", (char*)param);
	vTaskDelay(200);
}

/*call����ص�����*/
void call_testFunction(char*param[],uint32_t argc){
	APP_KERNEL_LOG("Hello world!\r\n");
	for(int i = 0; i<argc; i++){
		APP_KERNEL_LOG("%7.2lf ",strtod(param[i], NULL));
	}
	APP_KERNEL_LOG("\r\n");
}

void app_kernel_demo1(void* param, uint32_t argc){
	APP_KERNEL_LOG("\r\napp_kernal��run app_kernel_demo1...\r\n");
	APP_KERNEL_LOG("app_kernal����������call��time����ʾ������...\r\n");
	
	/*������time�����Ӧ�ó������ֱ���500��1000��1500��ϵͳ����֮�����time_testFunction
    ������ӡ��ͬ�Ĳ���*/
	char time1_param[] = "time1 test";
	char time2_param[] = "time2 test";
	char time3_param[] = "time3 test";
	app_kernel_call_after_times("time_test1", time_testFunction, time1_param, strlen(time1_param)+1, 1500);
	app_kernel_call_after_times("time_test2", time_testFunction, time2_param, strlen(time2_param)+1, 1000);
	app_kernel_call_after_times("time_test3", time_testFunction, time3_param, strlen(time3_param)+1, 500);
	app_kernel_show_times();
	
	/*ע��һ��call��������Ϊcall_test���ڴ����ն�����"call call_test"����
	  ��س����ɵ���call_testFunction����*/
	app_kernel_regist_user_call_function("call_test", call_testFunction);
  
	/*��ʾһ�£����û�����������Ϊʲôû���ṩ���ֽӿڣ��ڴ����ն˵���ĳ��ָ�Ȼ��
	  �ӳ�һ��ʱ����ִ��ָ��ָ���ĺ�������ʵ�������ֻҪ���call�����time���񼴿�����ʵ��*/
}

/**************************************************************************
�������ܣ��ź����ʾ��
��ڲ�������
����  ֵ����
**************************************************************************/
/*����ۺ���*/
void print(void* param, uint32_t argc){
	APP_KERNEL_LOG("param : %s, argc : %d\r\n", (char*)param, argc);
	vTaskDelay(21);
}

/*�ź�һ�㱻�����ȫ�ֱ�����������Ϊ�ź���Ҫ������ͬ�������������������жϵģ�
 ��������Ҫ����ͬ�������жϷ��ʵ������Ҫ�����ȫ�ֱ���������һЩ�򵥵ĵ�Ӧ�ó�
 �϶���ɾֲ�������ȻҲ�ǿ��Ե�*/
signal_t signalTest;
uint8_t flag = 0;
void app_kernel_demo2(void* param, uint32_t argc){
	/*��ʼ������õ�ȫ�ֱ���signalTest�������û����ܶ�ε�������2
	  ��ε��ò�Ӧ���ظ���ʼ���źţ�����������flag�жϣ�ֻ�����ʼ��һ��*/
	if(flag == 0){
		signalTest = Signal();
		flag = 1;
	}
	
	
	/*�����ź����*/
	connect(signalTest, print);
	
	/* �����ź�ʮ�Σ����������Hello world������һ��Ҫע��emit��
	   ����������argcһ��Ҫ�ǵڶ�������ָ����ڴ��С����λ�ֽڣ���
	   ���argc��С������ۺ������ݵ�param�ǲ������ģ����argc������
	   ������ʷǷ��ռ�ֱ�ӵ���ϵͳ����*/
	for (int i = 0; i<100; i++){
		emit(signalTest, "Hello world!", strlen("Hello world!")+1);
		vTaskDelay(20);
	}
	/*ɾ���źţ�����������ã��ź�һ��ɾ�����������ͼ�����źţ�������ϵͳ����*/
//	del_signal(signalTest);
}

/**************************************************************************
�������ܣ�app_kernel_non_blocking_delayʾ��
��ڲ�������
����  ֵ����
**************************************************************************/
void app_kernel_demo3(void* param, uint32_t argc){
	uint8_t state = 1;
	vTaskDelay(50);
	
	/*�����������ʱ����(ak_nonBlockDelay_t����)*/
	ak_nonBlockDelay_t testDelay;
	
	/*��λak_nonBlockDelay_t�������¿�ʼ��ʱ���ڵ�ǰʱ��1000��ticks֮
	  �󣬵���app_kernel_non_blocking_delay��������testDelay���󽫷���1*/
	app_kernel_non_blocking_delay_reset(&testDelay, 1000);
	while(1){
		switch (state){
			case 1:
				APP_KERNEL_LOG("state1\r\n");
				vTaskDelay(200);
			
        /*�ж�testDelay�����Ƿ񵽴�ָ��ʱ�䣬���δ���ﷵ��0�������˷���1���˺���ʱ��������*/
				if(app_kernel_non_blocking_delay(&testDelay)){
					/*��λtestDelay�������¿�ʼ��ʱ*/
					app_kernel_non_blocking_delay_reset(&testDelay, 1000);
					/*״̬ת��*/
					state = 2;
				}
				break;
			case 2:
				APP_KERNEL_LOG("state2\r\n");
				vTaskDelay(200);
				if(app_kernel_non_blocking_delay(&testDelay)){
					app_kernel_non_blocking_delay_reset(&testDelay, 1000);
					state = 3;
				}
			  break;
			case 3:
				APP_KERNEL_LOG("state3\r\n");
				vTaskDelay(200);
				if(app_kernel_non_blocking_delay(&testDelay)){
					app_kernel_non_blocking_delay_reset(&testDelay, 1000);
					state = 4;
				}
			  break;
			case 4:
				APP_KERNEL_LOG("state4\r\n");
				vTaskDelay(200);
				if(app_kernel_non_blocking_delay(&testDelay)){
					APP_KERNEL_LOG("����3���Խ���\r\n");
					return ;
				}
			  break;
		}
		vTaskDelay(1);
	}
}

void app_kernel_test(void){
	while(1){
		vTaskDelay(20);
	}
}















