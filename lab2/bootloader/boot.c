#include "boot.h"

#define SECTSIZE 512

#include<string.h>



void bootMain(void) {
	int i = 0;
	//int phoff = 0x34;
	//int offset = 0x1000;
	unsigned int elf = 0x100000; //refer to kernel 
	void (*kMainEntry)(void);
	kMainEntry = (void(*)(void))0x100000;

	for (i = 0; i < 200; i++) {
		readSect((void*)(elf + i*512), 1+i);
	}

	// TODO: 填写kMainEntry、phoff、offset...... 然后加载Kernel（可以参考NEMU的某次lab）
	ELFHeader* p_elf = (void*) elf;
	kMainEntry = (void(*)(void))(p_elf->entry);
	//phoff = p_elf->phoff;
	ProgramHeader* ph = (void*)p_elf + p_elf->phoff;
	//ProgramHeader* eph = ph + p_elf->phnum;//num of ph
	int phnum = p_elf->phnum;
	//for(; ph < eph; ph++){
	for(int i = 0; i < phnum; i++, ph++){
		if(ph->type == 0x1){//load
			//offset = ph->off;
			/*int indx = 0;
			for(; indx < ph->filesz; indx++)//copy one by one
				*(unsigned char*)(ph->paddr + indx) = *(unsigned char*)(elf + ph->off + indx);
			for(; indx < ph->memsz; indx++)//memset
				*(unsigned char*)(ph->paddr + indx) = 0;*/
			memcpy((void*)ph->paddr, (void*)elf + ph->off , ph->filesz);
		}
	}

	//outByte(0x3F8, '@');
  /*  ELFHeader* eh=(ELFHeader*)elf;
    kMainEntry=(void(*)(void))(eh->entry);
    phoff=eh->phoff;
    ProgramHeader* ph=(ProgramHeader*)(elf+phoff);
    offset=ph->off;

    for (i = 0; i < 200 * 512; i++) {
        *(unsigned char *)(elf + i) = *(unsigned char *)(elf + i + offset);
    }

*/	kMainEntry();
}

void waitDisk(void) { // waiting for disk
	while((inByte(0x1F7) & 0xC0) != 0x40);
}

void readSect(void *dst, int offset) { // reading a sector of disk
	int i;
	waitDisk();
	outByte(0x1F2, 1);
	outByte(0x1F3, offset);
	outByte(0x1F4, offset >> 8);
	outByte(0x1F5, offset >> 16);
	outByte(0x1F6, (offset >> 24) | 0xE0);
	outByte(0x1F7, 0x20);

	waitDisk();
	for (i = 0; i < SECTSIZE / 4; i ++) {
		((int *)dst)[i] = inLong(0x1F0);
	}
}
