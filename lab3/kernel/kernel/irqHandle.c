#include "common.h"
#include "x86.h"
#include "device.h"

#define va_to_pa(va) (va + (current + 1) * 0x100000)
#define pa_to_va(pa) (pa - (current + 1) * 0x100000)

extern TSS tss;
extern int displayRow;
extern int displayCol;

extern ProcessTable pcb[MAX_PCB_NUM];
extern int current; // current process


void GProtectFaultHandle(struct StackFrame *sf);

void timerHandle(struct StackFrame *sf);

void syscallHandle(struct StackFrame *sf);

void syscallWrite(struct StackFrame *sf);
void syscallPrint(struct StackFrame *sf);
void syscallFork(struct StackFrame *sf);
void syscallExec(struct StackFrame *sf);
void syscallSleep(struct StackFrame *sf);
void syscallExit(struct StackFrame *sf);


void irqHandle(struct StackFrame *sf) { // pointer sf = esp
	/* Reassign segment register */
	asm volatile("movw %%ax, %%ds"::"a"(KSEL(SEG_KDATA)));
	
	/* Save esp to stackTop */
	//为了中断嵌套
	pcb[current].stackTop=(uint32_t)sf;

	switch(sf->irq) {
		case -1:
			break;
		case 0xd:
			GProtectFaultHandle(sf);
			break;
		case 0x20:
			timerHandle(sf);
			break;
		case 0x80:
			syscallHandle(sf);
			break;
		default:assert(0);
	}
}

void GProtectFaultHandle(struct StackFrame *sf) {
	assert(0);
	return;
}

void timerHandle(struct StackFrame *sf){
	//TODO 完成进程调度，建议使用时间片轮转，按顺序调度
	int runnable = -1;//point to runnable process
	if(pcb[current].state == STATE_RUNNING)
		pcb[current].timeCount++;
	
	for(int i = 0; i < MAX_PCB_NUM; i++){
		if(pcb[i].state == STATE_BLOCKED){
			pcb[i].sleepTime--;
			if(pcb[i].sleepTime <= 0){
				pcb[i].state = STATE_RUNNABLE;
				//pcb[i].timeCount = 0;//maybe can leave out
				runnable = i;
			}
		}
		else if(pcb[i].state == STATE_RUNNABLE)
			runnable = i;
		
	}
	if((pcb[current].timeCount >= MAX_TIME_COUNT)&& runnable != -1){//switch
		pcb[runnable].state = STATE_RUNNING;
		pcb[runnable].timeCount = 0;
		if(pcb[current].state == STATE_RUNNING)
			pcb[current].state = STATE_RUNNABLE; // maybe?
		//pcb[current].timeCount = 0;
		//sleeptime ?? maybe:0

		current = runnable;
		//switch process
		uint32_t tmpStackTop=pcb[current].stackTop;
		pcb[current].stackTop = pcb[current].prevStackTop;
        tss.esp0=(uint32_t)&(pcb[current].stackTop);
        asm volatile("movl %0,%%esp"::"m"(tmpStackTop));
        asm volatile("popl %gs");
        asm volatile("popl %fs");
        asm volatile("popl %es");
        asm volatile("popl %ds");
        asm volatile("popal");
        asm volatile("addl $8,%esp");
        asm volatile("iret");
	}
	else if(pcb[current].timeCount >= MAX_TIME_COUNT && pcb[current].state != STATE_DEAD && pcb[current].state != STATE_BLOCKED)
		pcb[current].timeCount = 0;//no switch
}


void syscallHandle(struct StackFrame *sf) {
	switch(sf->eax) { // syscall number
		case 0:
			syscallWrite(sf);
			break; // for SYS_WRITE
		/* Add Fork,Sleep... */
		case 1:
			syscallFork(sf);
			break;
		case 2:
			syscallExec(sf);
			break;
		case 3:
			syscallSleep(sf);
			break;
		case 4:
			syscallExit(sf);
			break;
		default:break;
	}
}

void syscallWrite(struct StackFrame *sf) {
	switch(sf->ecx) { // file descriptor
		case 0:
			syscallPrint(sf);
			break; // for STD_OUT
		default:break;
	}
}

void syscallPrint(struct StackFrame *sf) {
	int sel = sf->ds; // segment selector for user data, need further modification
	char *str = (char*)sf->edx;
	int size = sf->ebx;
	int i = 0;
	int pos = 0;
	char character = 0;
	uint16_t data = 0;
	asm volatile("movw %0, %%es"::"m"(sel));
	for (i = 0; i < size; i++) {
		asm volatile("movb %%es:(%1), %0":"=r"(character):"r"(str+i));
		if(character == '\n') {
			displayRow++;
			displayCol=0;
			if(displayRow==25){
				displayRow=24;
				displayCol=0;
				scrollScreen();
			}
		}
		else {
			data = character | (0x0c << 8);
			pos = (80*displayRow+displayCol)*2;
			asm volatile("movw %0, (%1)"::"r"(data),"r"(pos+0xb8000));
			displayCol++;
			if(displayCol==80){
				displayRow++;
				displayCol=0;
				if(displayRow==25){
					displayRow=24;
					displayCol=0;
					scrollScreen();
				}
			}
		}

	}
	updateCursor(displayRow, displayCol);
	sf->eax=size;
	return;
}	

void memcpy(void* dst,void* src,size_t size){
	for(uint32_t j=0;j<size;j++){
		*(uint8_t*)(dst+j)=*(uint8_t*)(src+j);
	}
}

void syscallFork(struct StackFrame *sf){
	//TODO 完善它

	//TODO 查找空闲pcb，如果没有就返回-1
	int i=0;
	for(int j = 1; j < MAX_PCB_NUM; j++){
		if(pcb[j].state == STATE_DEAD){
			i = j;
			break;
		}
	}
	if(i == 0){//fail
		sf->eax = -1;
		return;
	}
	
	//TODO 拷贝地址空间
	memcpy(&pcb[i].stack[0], &pcb[current].stack[0],sizeof(pcb[current].stack));
	memcpy((void*)((i + 1) * 0x100000), (void*)((current + 1) * 0x100000), 0x100000);
	//pcb[i].stack = pcb[current].stack;

	// 拷贝pcb，这部分代码给出了，请注意理解
	memcpy(&pcb[i],&pcb[current],sizeof(ProcessTable));
	
	pcb[i].regs.eax = 0;//son process return 0
	pcb[i].regs.cs = USEL(1 + i * 2);
	pcb[i].regs.ds = USEL(2 + i * 2);
	pcb[i].regs.es = USEL(2 + i * 2);
	pcb[i].regs.fs = USEL(2 + i * 2);
	pcb[i].regs.gs = USEL(2 + i * 2);
	pcb[i].regs.ss = USEL(2 + i * 2);
	pcb[i].stackTop = (uint32_t)&(pcb[i].regs);
	pcb[i].state = STATE_RUNNABLE;
	pcb[i].timeCount = 0;
	pcb[i].sleepTime = 0;
	
	pcb[i].pid = i;
	//pcb[i].regs.eip = pcb[current].regs.eip;
	sf->eax = i;//father process return pid of son
	pcb[current].regs.eax = i;
	asm volatile("int $0x20"); 
}	


void syscallExec(struct StackFrame *sf) {
	// TODO 完成exec
	// hint: 用loadelf，已经封装好了
	uint32_t entry = 0;
	uint32_t secstart = 0;
	uint32_t secnum =  0;
	secstart = sf->ecx;
	secnum = sf->edx;
	loadelf(secstart, secnum, (current + 1) * 0x100000, &entry);//what is the third parameter????
	pcb[current].regs.eip = entry;
	asm volatile("int $0x20"); 
}


void syscallSleep(struct StackFrame *sf){
	//TODO:实现它
	uint32_t time = sf->ecx;
	if(time < 0)
		assert(0);
	//if(time  > 0)
	pcb[current].sleepTime = time;
	pcb[current].timeCount = MAX_TIME_COUNT;
	pcb[current].state = STATE_BLOCKED;
	asm volatile("int $0x20"); //?
	//timerHandle(sf);//what if there is no runnable process
}	

void syscallExit(struct StackFrame *sf){
	//TODO 先设置成dead，然后用int 0x20进入调度
	disableInterrupt();
	pcb[current].state = STATE_DEAD;
	pcb[current].timeCount = MAX_TIME_COUNT;
	enableInterrupt();
	asm volatile("int $0x20"); 
}
