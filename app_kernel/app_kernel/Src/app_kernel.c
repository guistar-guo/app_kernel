/**
  ******************************************************************************
  * @file           : app_kernel.c
  * @brief          : app_kernel是构建于freertos操作系统之上的一个应用模块，以
  *                   更优雅，更简洁的方式实现了rtos系统的一些同步工作，并且对
  *                   用户提供了良好的串口终端交互体验！（内含"call", "time",
  *                   "信号与槽"三大机制以及其他的一些小组件（如：专用于状态机
  *                   编程的非阻塞延时函数）
  ******************************************************************************
  * @attention 移植好之后（如何移植下文说明），调用app_kernel_Initialize即可开启
  * 应用内核的全部功能，此时在移植的串口上可以查看内核的打印信息，注意如果有提示
  * 某某失败的信息，大概率是内存不够了，解决办法查看下文最后（其实就是把configT
  * OTAL_HEAP_SIZE设置大一点而已）
  *
  * 整个模块的移植查看《app_kernel简史》中的b站视频介绍
  * 
  * 1.call服务的使用查看app_kernel_regist_user_call_function的注释即可
  * 
  * 2.time服务其实就是一个函数：app_kernel_call_after_times，阅读注释即可掌握
  * 
  * 3.此模块提供了一个时基：app_kernel_systick，它与freertos系统的时基频率保持一致
  *  另外，此模块提供了一个软件定时器函数app_kernel_systimer_callback，周期也是fre
  *  rtos的时基
  *
  * 4.可以指定系统心跳灯，通过APP_KERNEL_HEAT_LED宏指定
  *
  * 5.支持“信号与槽”的强大机制（“信号与槽”伟大无需多言）
  *
  * 6.总结一下app_kernel目前支持的几大业务：call服务，使用户很轻松的在串口助手中调用
  *   某个回调函数；time服务，使用户延迟调用某个函数，还可以和call配合，实现在串口助手
  *   中延迟调用某个函数；信号与槽，非常简单灵活的同步机制，同步任务与任务，任务与
  *   中断的利器！实际使用可以将前两个服务结合达到奇效
  * 
  * 7.除非特殊说明，本模块的任何函数都不应该在中断上下文中运行，但是为了提高灵活性，
  *   笔者专门开发了可以在中断上下文运行的信号与槽机制，提供了可以在中断中运行的
  *   发送信号函数-emit_FromISR.
  *
  *   最后，如何将本模块应用到自己的代码上，很简单，只需要依次实现头文件指定的五个
  *   移植接口即可！经过笔者实测，市面上最常见的f103c8t6可以流畅的运行此模块，不过
  *   在准备移植之前，必须保证freertos可管理的最大内存空间在20000 bytes以上！原因是
  *   本模块对freertos可管理的内存大小有要求，内存太小会导致系统崩溃。需保证config
  *   TOTAL_HEAP_SIZE的大小至少在20000 Bytes以上，且运行的时候，只要碰到卡死的问题，
  *   优先考虑是内存不够导致的，包括两方面：第一，freertos可管理的内存大小不够，这
  *   个由config_TOTAL_HEAP_SIZE决定，前文已经提到过多次了，它将会导致系统刚上电或
  *   者刚复位就卡死，而第二种情况比较复杂，是任务栈溢出了，这个问题导致的现象是：
  *   当某个任务运行的指令程序过多就会卡死，或者调用函数层数过深也会导致卡死
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
		APP_KERNEL_LOG("app_kernel：找不到待删除的链表项%s\r\n", NameToDel);
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
	APP_KERNEL_LOG("app_kernel: 当前系统中注册的call服务有以下几个：\r\n");
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
函数功能：注册串口终端的call命令的回调函数，注册之后，在串口助手中通过
          call name的形式来调用目标回调函数。
入口参数：name 回调函数被终端调用的名字。
          callbackFunc 回调函数指针，其类型必须是kernelFunction_t类型
返回  值：无
**************************************************************************/
void app_kernel_regist_user_call_function(const char* name, kernelFunction_t callbackFunc){
	vTaskSuspendAll(); 
	PFC_t p = &aPendFunctionList->head;
  while(p->next != NULL && strcmp(p->Name, name)){
		p = p->next;
	}
	
	/*为了提前唤醒调度器，空间换时间*/
	char* p_Name = p->Name;
	if( xTaskResumeAll() == pdFALSE ){ 
    portYIELD_WITHIN_API();
	}
	if(!strcmp(p_Name, name)){
		APP_KERNEL_LOG("app_kernel：系统中已经注册了call服务：%s，无需重复中注册\r\n", name);
		return ;
	}
  PFC_t temp = PFCitemInit(callbackFunc, (char*)name);									 
	aPFCAdd(temp, aPendFunctionList);
}

/**************************************************************************
函数功能：注销串口终端的call命令的回调函数
入口参数：name 回调函数被终端调用的名字;
返回  值：无
**************************************************************************/
void app_kernel_del_user_call_function(const char* name){
	aPFCDel((char*)name, aPendFunctionList);
}

void app_kernel_call_user_func(const char*name,\
	char*params[], uint32_t argc, TickType_t xTicksToWait){
	vTaskSuspendAll(); 
	PFC_t p = &aPendFunctionList->head;
	while(p->next!=NULL && strcmp(name, p->Name)) p = p->next;
		
	/*为了提前开启调度器，空间换时间*/
	char* p_name = p->Name;
	if( xTaskResumeAll() == pdFALSE ){ 
      portYIELD_WITHIN_API();
	}
	if(!strcmp(name, p_name)){
		xTimerPendFunctionCall((PendedFunction_t)p->EntryFunction, params, argc, xTicksToWait);
	}else{
		APP_KERNEL_LOG("app_kernel：系统中未注册此函数，请检查函数名字是否正确！");
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
		APP_KERNEL_LOG("app_kernel: 找不到待删除的链表项%s\r\n", time_to_del);
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
	APP_KERNEL_LOG("app_kernel: 当前内核中含有的time服务有以下几个：\
	（按照调用时间的先后顺序排列）\r\n");
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
函数功能：time服务，调用此函数，将在当前时刻之后callTime个系统节拍之后调用
          EntryFunction参数指定的函数，在此期间单片机可以执行其他的任务，也
          就是说此函数是非阻塞的，调用之后立刻返回！不浪费cpu的精力，另外，
          时间到了之后执行函数EntryFunction的上下文是任务上下文，也就是说
          EntryFunction里面是可以执行freertos提供的延时函数的，但是不推荐这
          种做法，因为可能会影响其他time服务的回调函数的调用时机。回调函数
          尽量做到快进快出，为什么？请看注意事项

注意事项：(1)首先明确一点，time是不可以过于频繁的调用的，这个问题无法解决，谁来
          也无法解决！为什么？试想一下，如果非常频繁的调用，当调用频率大于time
          回调函数的处理频率，就会导致系统中现有的time只增不减，而系统的内存
          是有限的！总会有爆掉的那一刻！所以，time的调用频率必须小于time回调
          函数的处理频率！如果真出现大于的情况，当系统中同时存在的time个数大于
          宏MAX_TIME_CNT时，app_kernel内核日志会向串口终端强制持续打印警告信息。

          MAX_TIME_CNT这个宏定义决定了系统中允许同时存在的time个数，但是它并
          不是越大越好，太大了，会导致系统内存不够而崩溃！（可以通过onfig
          TOTAL_HEAP_SIZE宏设置freertos最大可管理内存）
 
          (2)另外注意，此函数禁止在中断上下文调用，但是可以通过在中断上下文发送
				  信号，在信号的槽函数（任务上下文）调用time

入口参数：serviceName： 指定本次服务的名称，可以通过app_kernel_show_times函数
                        查看当前系统中的所有time服务的名称集合；允许用户创建相
                        同名字的time服务，不过不推荐。
          EntryFunction 指定到达指定时刻之后要调用的函数。
          param：       给EntryFunction函数传入的泛型指针参数；允许用户传递空
                        指针NULL，在这种情况下，将无视下一个参数argc。
          argc：        指定param泛型指针指向的内存大小，单位：字节。
          callTime：    当前时刻之后callTime个系统节拍之后调用
                        EntryFunction参数指定的函数。
返回  值：无
**************************************************************************/
void app_kernel_call_after_times(char* serviceName, \
	kernelSignalFunc EntryFunction, void*param, uint32_t argc, uint32_t callTime){
	time_t time = app_kernel_time_item_init(serviceName, EntryFunction, param, argc, callTime);
	app_kernel_timer_add(the_time_head, time);
}

/**************************************************************************
函数功能：打印某个命令的帮助文档   
入口参数：无
返回  值：无
**************************************************************************/
static void user_cmd_help(char* user_command){
	APP_KERNEL_LOG("\r\n");
  if(!strcmp(user_command, "call")){
		APP_KERNEL_LOG("命令call用法：\r\n");
		APP_KERNEL_LOG("首先，用户需要自行定义一个kernelFunction_t类型的函数，可以参考call_testFunction函数的类型\r\n");
		APP_KERNEL_LOG("其次，调用app_kernel_regist_user_call_function函数在内核中注册一个call命令回调函数\r\n");
		APP_KERNEL_LOG("最后，在终端调用call+空格+注册在内核中的回调服务名字，即可调用注册好的函数\r\n");
		APP_KERNEL_LOG("另外，还支持在终端输入参数，例如：call testFunc 123 456，则在回调函数中，param1[0]的值\r\n");
		APP_KERNEL_LOG("是123，param[1]的值是456，param2的值是2，表示用户在终端输入了两个参数，参考call_testFunc\r\n");
		APP_KERNEL_LOG("tion函数了解如何在回调函数内部访问终端传过来的的参数\r\n");
		APP_KERNEL_LOG("输入call list 可以查看系统当前注册的所有回调服务的名字\r\n");
	}
	else if(!strcmp(user_command, "time")){
		APP_KERNEL_LOG("time服务用法：\r\n");
		APP_KERNEL_LOG("首先，用户需要自行定义一个kernelFunction_t类型的函数，可以参考call_testFunction函数的类型\r\n");
		APP_KERNEL_LOG("其次，调用app_kernel_call_after_times函数在内核中注册一个time服务即可。\r\n");
		APP_KERNEL_LOG("服务效果是：在当前时刻之后的第callTime（函数app_kernel_call_after_times形参）个系统节拍\r\n");
		APP_KERNEL_LOG("调用指定的回调函数，即延迟调用功能\r\n");
	}
	else if(!strcmp(user_command, "signal")){
		APP_KERNEL_LOG("signal and slot 信号与槽机制，与Qt的相关接口保持高度一致，主要api包括：\r\n");
		APP_KERNEL_LOG("Signal                                  ：     注册信号\r\n");
		APP_KERNEL_LOG("connect                                 ：     连接信号与槽\r\n");
		APP_KERNEL_LOG("emit_FromISR                            ：     发送信号（中断上下文）\r\n");
		APP_KERNEL_LOG("emit                                    ：     发送信号（任务上下文）\r\n");
		APP_KERNEL_LOG("以上就是信号与槽的相关函数，了解更详细用法，请阅读以上几个函数的注释.\r\n");
	}
	else{
		APP_KERNEL_LOG("app_kernel: 系统未支持此命令！\r\n\r\n");
	}
}

/**************************************************************************
函数功能：打印系统支持的所有的命令帮助信息   
入口参数：无
返回  值：无
**************************************************************************/
static void user_all_cmd_help(void){
	APP_KERNEL_LOG("内核支持以下三大服务：\r\n\r\n");
	APP_KERNEL_LOG("**************************************************************************************\r\n");
	APP_KERNEL_LOG("| call：             |了解详情，请输入'help call'                                    |\r\n");
	APP_KERNEL_LOG("| time:              |time服务其实就是app_kernel_call_after_times，请阅读其注释了解  |\r\n");
  APP_KERNEL_LOG("| signal and slot:   |了解详情，请输入'help signal'                                  |\r\n");
	APP_KERNEL_LOG("**************************************************************************************\r\n");
	APP_KERNEL_LOG("每个命令的详细参数解释可通过“help + 空格 + 命令名字”的形式查看，如: help signal(记得加回车！)\r\n");
	APP_KERNEL_LOG("\r\n另外，还有三个示例程序：app_kernel_demo1，app_kernel_demo2，app_kernel_demo3\r\n\
app_kernel_demo1演示了call服务和time服务的使用；app_kernel_demo2演示了信号与槽的使用\r\n\
；app_kernel_demo3演示了app_kernel提供的非阻塞延时函数如何应用在状态机编程上，提供了一个状态机示例程序\r\n");
	APP_KERNEL_LOG("直接输入“help”可以查看系统当前支持的所有命令及其大致描述\r\n\r\n");
}

/**************************************************************************
函数功能：处理用户命令队列user_cmd_queue，将队列里面的字符串形式的命令翻译
          成内核命令发送给内核处理
入口参数：无
返回  值：无
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
			APP_KERNEL_LOG("app_kernel: 用户发送的原始命令：%s\r\n", user_orig_cmd_buf);
			
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
				APP_KERNEL_LOG("app_kernel: 命令参数过长，超出系统限制!\r\n");
				continue;
			}
//			else{
//				for(int i = 0; i<user_cmd_seg_cnt; i++){
//					APP_KERNEL_LOG("第%d段：%s  ",i ,cmd_user_to_kernel[i]);
//				}
//			}
			APP_KERNEL_LOG("\r\n");
			
			if (!strcmp(cmd_user_to_kernel[0], "help")){
				if(user_cmd_seg_cnt > 2){
					APP_KERNEL_LOG("app_kernel: help命令参数过多，注意：help命令\
					               每次只能传入一个参数\r\n");
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
					APP_KERNEL_LOG("app_kernel: 参数过少!\r\n");
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
				APP_KERNEL_LOG("app_kernel: 命令出错，请检查！\r\n");
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
        sepR->EvenBit, pdFALSE, pdTRUE, portMAX_DELAY);
     if(theSetBits & sepR->EvenBit){
         while(xQueueReceive( sepR->signal_queue, &signal, 0) != errQUEUE_EMPTY){
             signal.slot(signal.param, signal.argc);
             sepR->waitingToDisposeNum = uxQueueMessagesWaiting(sepR->signal_queue);
         }
         xEventGroupClearBits(app_kernel.isSignalThreadBusy, sepR->EvenBit);
     }
     vTaskDelay(1);
  }
}

void app_kernel_timer_daemon(void){
	if(time_cnt >= MAX_TIME_CNT){
		APP_KERNEL_LOG("app_kernel:警告!time请求过于频繁，考虑降低请求频率以提高系统实时性\r\n");
		APP_KERNEL_LOG("app_kernel:也可以选择增大MAX_TIME_CNT宏定义，不过有系统因内存不足崩溃的风险\r\n");
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
函数功能：应用内核管理的软件定时器
入口参数：略
返回  值：无
**************************************************************************/
static void app_kernel_systimer_callback(void* parameter){
	if(!(++app_kernel.app_kernel_systick%200))
	{
		APP_KERNEL_HEAT_LED;
	}
	
	/*每隔500个系统节拍，打印一次当亲系统的剩余内存*/
//	 if(app_kernel.app_kernel_systick%500 == 0){
//		 for(int i = 0; i<MAX_SIGNAL_THREAD; i++){
//			 APP_KERNEL_LOG("队列sep[%d].queue剩余空间个数%d\r\n",\
//			   i, uxQueueSpacesAvailable(sep[0].signal_queue));
//		 }
//	 }
//		size_t remainBytes = xPortGetMinimumEverFreeHeapSize();
//		APP_KERNEL_LOG("app_kernel:系统剩余内存:%d bytes\r\n", remainBytes);
//	}
	app_kernel_timer_daemon();
}

/**************************************************************************
函数功能：初始化本模块，须在app_kernel的移植串口初始化好之后的地方调用此
          函数，调用过此函数是使用本模块的所有功能的前提. 
入口参数：无
返回  值：无
**************************************************************************/
void app_kernel_Initialize(void){
	/*下面是rtos内核对象的创建，rtos内核对象一定要定义在创建任务之前，否则很容易造成系统崩溃
	（造成系统崩溃的原因：指针为NULL的时候，任务试图操作指针）*/
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
		APP_KERNEL_LOG("app_kernel: 创建软件定时器失败!\r\n");
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
			  APP_KERNEL_LOG("app_kernel: 创建%s任务失败!\r\n", sep[i].name);
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
			APP_KERNEL_LOG("app_kernel: 创建user_cmd_queue_Handle任务失败!\r\n");
			while(1){}
		}
	taskEXIT_CRITICAL();
	APP_KERNEL_LOG(" \r\n");
	APP_KERNEL_LOG(" \r\n");
	APP_KERNEL_LOG("      \\|/   \r\n");
	APP_KERNEL_LOG("      -AK-    应用内核：开始运行...\r\n");
	APP_KERNEL_LOG("      /|\\   \r\n");
	APP_KERNEL_LOG("************************************************************************\r\n");
	APP_KERNEL_LOG("欢迎使用##《app_kernel》##，本模块建议将串口助手设置为全屏使用效果更佳\r\n");
	APP_KERNEL_LOG("************************************************************************\r\n");
	APP_KERNEL_LOG("输入每一个命令之后，都必须再追加一个回车建（直接键盘上敲回车）\r\n\r\n");
	APP_KERNEL_LOG("输入help命令可以查看帮助信息\r\n");
	user_all_cmd_help();
}

/**************************************************************************
函数功能：中断回调函数，在中断上下文中不适宜做过多事情，因此使用freertos队
          列机制将用户命令发送到user_cmd_queue，由app_kernel_dispose_user_c
          md处理用户命令
          
入口参数：user_command 用户通过串口发送过来的原始命令，包含命令和参数两部分
返回  值：无
**************************************************************************/
static void send_user_command_to_kernel(const char* user_command){
	BaseType_t xReturn = pdPASS;
	BaseType_t pxHigherPriorityTaskWoken;
  xReturn = xQueueSendFromISR( app_kernel.user_cmd_queue,\
    	user_command,&pxHigherPriorityTaskWoken );
	portYIELD_FROM_ISR(pxHigherPriorityTaskWoken); 
  if(pdPASS != xReturn)
    APP_KERNEL_LOG("app_kernel: 消息send_data1发送失败!\r\n");
}

/**************************************************************************
函数功能：串口终端的串口回调函数，在stm32硬件串口接收单个字节的中断函数
          中调用
入口参数：无
返回  值：无
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
函数功能： 复位ak_nonBlockDelay_t对象，从此刻开始计时，在当前时间ticksToDelay
           个ticks之后，调用app_kernel_non_blocking_delay函数传入testDelay对
           象将返回1，在此之前，调用app_kernel_non_blocking_delay都返回0
          
入口参数：ticks 延时时间
返回  值：无
**************************************************************************/
void app_kernel_non_blocking_delay_reset(ak_nonBlockDelay_t* akDelay, uint32_t ticksToDelay){
	akDelay->endTime = ticksToDelay + app_kernel.app_kernel_systick;
}

/**************************************************************************
函数功能：非阻塞函数，传入ak_nonBlockDelay_t指针，立刻返回，如果
          ak_nonBlockDelay_t对象指定的时刻到达，则立刻返回1，否则立刻返回0
          
入口参数：ticks 延时时间
返回  值：无
**************************************************************************/
int app_kernel_non_blocking_delay(ak_nonBlockDelay_t* akDelay){
	return app_kernel.app_kernel_systick >= akDelay->endTime ? 1 : 0;
}

/**************************************************************************
函数功能：注册一个信号        
入口参数：无
返回  值：signal_t 返回注册好的信号，最常见的应用场景是先定义一个signal_t
          类型的全局变量，然后在某个任务中调用此函数，将返回值赋给这个变量
          为什么要定义成全局变量？因为只有这样才能在当前系统的任意任务，
          中断中调用发送信号函数emit
**************************************************************************/
signal_t Signal(void){
	signal_t signal = (signal_t)pvPortMalloc(sizeof(app_kernel_signal));
	signal->param = (void*)pvPortMalloc(sizeof(SIGNAL_PARAM_MAX_SIZE));
	signal->slot = NULL;
	signal->argc = 0;
	return signal;
}

/**************************************************************************
函数功能：连接信号与槽       
入口参数：signal    要连接的信号，必须被Signal函数初始化过！
          slot      要连接的槽函数指针，是slot_t类型，跳转到app_kernel.h文
                    件查看其定义
返回  值：无
**************************************************************************/
void connect(signal_t signal, slot_t slot){
	if(signal == NULL) return ;
	signal->slot = slot;
}

/**************************************************************************
函数功能：发送信号，发送前必须做两件事情：第一，通过Signal函数注册一个信号
          第二，通过connect函数给注册好的信号连接一个槽。如果做到以上两点，
          则emit将成功发送信号，app_kernel模块会尽力地立刻去调用槽函数。

注意：    此函数禁止在中断函数中使用，不过app_kernel模块提供了此函数的中断
          版本emit_FromISR
入口参数：signal：要发送的信号
          param： 给信号的参数传递的void*类型的指针参数
          argc：  指定param指针指向的内存空间大小，这个参数一定要正确设置！
                  比如param指向一个字符串"hello"，很明显它的大小是6个字节，
                  如果此时指定argc为3，则只能传入'h','e','l'三个字母给param
                  ，并且字符串结束标志'\0'也没有传入，这很有可能导致后续的
                  字符串操作产生非法访问；而如果指定argc为7，则会直接导致
                  param非法访问，系统面临崩溃危险！，因此argc的值一定要慎重！
返回  值：无
**************************************************************************/
void emit(signal_t signal, void* param, uint32_t argc){
	BaseType_t xReturn = pdPASS;
	if(signal == NULL || signal->slot == NULL) return;
	UBaseType_t minNum = QUEUE_LEN+1;
	vTaskSuspendAll();
	signal_emit_param* sepT = NULL;
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
	if( xTaskResumeAll() == pdFALSE )
     portYIELD_WITHIN_API();
	xReturn = xQueueSend(sepT->signal_queue, signal, 0);
	
	if(xReturn == errQUEUE_FULL){
		APP_KERNEL_LOG("app_kernel:警告：信号发送频率过高，将舍弃部分新发送的信号！\r\n");
	  return ;
	}else if(xReturn == pdPASS){
		sepT->waitingToDisposeNum = uxQueueMessagesWaiting(sepT->signal_queue);
		xEventGroupSetBits(app_kernel.isSignalThreadBusy, sepT->EvenBit);
	}
}

/**************************************************************************
函数功能：emit的中断版本，在中断函数中使用
入口参数：略
返回  值：无
**************************************************************************/
void emit_FromISR(signal_t signal, void* param, uint32_t argc){
	BaseType_t pxHigherPriorityTaskWoken;
	BaseType_t xReturn = pdPASS;
	if(signal == NULL || signal->slot == NULL) return;
	UBaseType_t minNum = QUEUE_LEN+1;
	signal_emit_param* sepT = NULL;
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
  xQueueSendFromISR(sepT->signal_queue, signal, 0);
	portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
	
	if(xReturn == errQUEUE_FULL){
	  return ;
	}else if(xReturn == pdPASS){
		sepT->waitingToDisposeNum = uxQueueMessagesWaitingFromISR(sepT->signal_queue);
		xEventGroupSetBitsFromISR(app_kernel.isSignalThreadBusy, sepT->EvenBit,\
      	&pxHigherPriorityTaskWoken);
	  portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
	}
}

/**************************************************************************
函数功能：删除系统中的信号，这个函数基本用不到！因为只要你注册信号，肯定
          是想用它的，用完了之后，除非保证之后永远不会再用了，那样的话，
          调用此函数删除这个信号，否则永远不要调用这个函数，因为你把信号
          删除了，后续一不小心再调用发送信号函数发送这个信号，那系统立刻
          崩溃！（非法访问）
入口参数：signal_t  指定要从系统中删除的信号
返回  值：无
**************************************************************************/
void del_signal(signal_t signalToDel){
	if(signalToDel == NULL) return ;
	vPortFree(signalToDel->param);
	vPortFree(signalToDel);
}

/*以下是三个示例代码，它们依赖系统的“信号与槽”与call机制相配合，达到
  可以在串口终端输入指令来调用的效果，如：输入app_kernel_demo1（加回车
  ）即可调用示例代码1，看懂示例代码就可以直接上手运用app_kernel到自己
  的项目中了*/

/**************************************************************************
函数功能：call和time示例
入口参数：略
返回  值：无
**************************************************************************/
/*time命令回调函数*/
void time_testFunction(void* param,uint32_t argc){
	APP_KERNEL_LOG("%s\r\n", (char*)param);
}

/*call命令回调函数*/
void call_testFunction(char*param[],uint32_t argc){
	APP_KERNEL_LOG("Hello world!\r\n");
	for(int i = 0; i<argc; i++){
		APP_KERNEL_LOG("%7.2lf ",strtod(param[i], NULL));
	}
	APP_KERNEL_LOG("\r\n");
}

void app_kernel_demo1(void* param, uint32_t argc){
	APP_KERNEL_LOG("\r\napp_kernal：run app_kernel_demo1...\r\n");
	APP_KERNEL_LOG("app_kernal：正在运行call和time服务示例程序...\r\n");
	
	/*以下是time服务的应用场景，分别在500，1000，1500各系统节拍之后调用time_testFunction
    函数打印不同的参数*/
	char time1_param[] = "time1 test";
	char time2_param[] = "time2 test";
	char time3_param[] = "time3 test";
	app_kernel_call_after_times("time_test1", time_testFunction, time1_param, strlen(time1_param)+1, 1500);
	app_kernel_call_after_times("time_test2", time_testFunction, time2_param, strlen(time2_param)+1, 1000);
	app_kernel_call_after_times("time_test3", time_testFunction, time3_param, strlen(time3_param)+1, 500);
	app_kernel_show_times();
	
	/*注册一个call服务，名字为call_test，在串口终端输入"call call_test"再输
	  入回车即可调用call_testFunction函数*/
	app_kernel_regist_user_call_function("call_test", call_testFunction);
  
	/*提示一下：有用户可能有疑问为什么没有提供这种接口：在串口终端调用某个指令，然后
	  延迟一段时间再执行指令指定的函数，其实这个需求只要结合call服务和time服务即可轻松实现*/
}

/**************************************************************************
函数功能：信号与槽示例
入口参数：略
返回  值：无
**************************************************************************/
/*定义槽函数*/
void print(void* param, uint32_t argc){
	APP_KERNEL_LOG("param : %s, argc : %d\r\n", (char*)param, argc);
	vTaskDelay(21);
}

/*信号一般被定义成全局变量，这是因为信号主要是用来同步任务与任务，任务与中断的，
 所以他需要被不同的任务，中断访问到，因此要定义成全局变量，对于一些简单的的应用场
 合定义成局部变量当然也是可以的*/
signal_t signalTest;
uint8_t flag = 0;
void app_kernel_demo2(void* param, uint32_t argc){
	/*初始化定义好的全局变量signalTest，但是用户可能多次调用用例2
	  多次调用不应该重复初始化信号，因此这里加入flag判断，只允许初始化一次*/
	if(flag == 0){
		signalTest = Signal();
		flag = 1;
	}
	
	
	/*连接信号与槽*/
	connect(signalTest, print);
	
	/* 发送信号十次，并传入参数Hello world，这里一定要注意emit的
	   第三个参数argc一定要是第二个参数指向的内存大小（单位字节），
	   如果argc过小，则给槽函数传递的param是不完整的，如果argc过大则
	   会因访问非法空间直接导致系统崩溃*/
	for (int i = 0; i<100; i++){
		emit(signalTest, "Hello world!", strlen("Hello world!")+1);
		vTaskDelay(20);
	}
	/*删除信号，这个函数慎用，信号一旦删除，如果还试图发送信号，将导致系统崩溃*/
//	del_signal(signalTest);
}

/**************************************************************************
函数功能：app_kernel_non_blocking_delay示例
入口参数：略
返回  值：无
**************************************************************************/
void app_kernel_demo3(void* param, uint32_t argc){
	uint8_t state = 1;
	vTaskDelay(50);
	
	/*定义非阻塞延时对象(ak_nonBlockDelay_t类型)*/
	ak_nonBlockDelay_t testDelay;
	
	/*复位ak_nonBlockDelay_t对象，重新开始计时，在当前时间1000个ticks之
	  后，调用app_kernel_non_blocking_delay函数传入testDelay对象将返回1*/
	app_kernel_non_blocking_delay_reset(&testDelay, 1000);
	while(1){
		switch (state){
			case 1:
				APP_KERNEL_LOG("state1\r\n");
				vTaskDelay(200);
			
        /*判断testDelay对象是否到达指定时间，如果未到达返回0，到达了返回1，此函数时非阻塞的*/
				if(app_kernel_non_blocking_delay(&testDelay)){
					/*复位testDelay对象，重新开始计时*/
					app_kernel_non_blocking_delay_reset(&testDelay, 1000);
					/*状态转换*/
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
					APP_KERNEL_LOG("用例3测试结束\r\n");
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















