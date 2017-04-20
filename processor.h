#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include "draw.h"

#define MEMSIZE 16000000 
#define INSTSIZE 16000
#define STALLOFFSET 4 // 4 for start

int instMem[INSTSIZE][32]; // instruction memory
char *image_file;
char *output_file;

int memidx= 0; // index for instruction memory
int numofinstructions;
int numOfCycles=0;
int numOfDataStalls = 0;
int numOfControlStalls = 0;
int memAccess=0;
int excessCount= 0; // for if stage fetching instructions beyond numofinstructions

int opglobal[32];//global variable to store output of multiplexer in write back 
bool stallIfId = false;
	
int regMem[32][32];
int dataMem[MEMSIZE][32];
int Hi[32]; int Lo[32]; // 2 special registers for mul and mla

// forward signal
int fwdA, fwdB,fwdC; // 1 for memory //2 for aluresult
pthread_t callThd[6];

// hazard signals
int ifidwrite, pcwrite, controlmultiplex;

int dataMemoryIndex(int offset){
	int val= 67125248;
	int idx = offset - val;
	if (idx < 0 || idx >= MEMSIZE ){
		printf("Invalid Memory Address.\n");
		exit(0);
	} 
	return idx; 
}

// we wil call thread only if instruction is valid
typedef struct{
	int aluop[2];
	int alusrc;
	int regdst;
	//int link=0;	
} alu;

typedef struct {
	int branchmem;
	int memwr;
	int memre;
} mem;

typedef struct{
	int mem2r;
	int regwr;
} wb;

// 1-> Current ; 2-> Pending
struct ifidst{
	int instruction1[32];
	int instruction2[32];
	int pc1; // index of instMem
	int pc2;
	bool nop1;
	bool nop2;
	// ifflush
};
typedef struct ifidst ifid;
ifid regifid;

struct idexst{
	int instruction1[32];
	int instruction2[32];
	bool nop1;
	bool nop2;

	wb wb1 ; 
	wb wb2;
	mem m1;
	mem m2;
	alu ex1;
	alu ex2;
    
	int pc1;
	int pc2;

	int operanda1[32];
	int operanda2[32];
	int operandb1[32];
	int operandb2[32];

	int constantext1[32];
	int constantext2[32];
	int rt1[5];
	int rt2[5];
	int rd1[5];
	int rd2[5];
};
typedef struct idexst idex;

idex regidex;
struct exmemst{
	int instruction1[32];
	int instruction2[32];
	bool nop1;
	bool nop2;

	wb wb1 ; 
	wb wb2;
	mem m1;
	mem m2;

	int pcadd1;
	int pcadd2;
	
	int zero1;
	int zero2;
	int aluresult1[32];
	int aluresult2[32];
	int memwritedata1[32];
	int memwritedata2[32];
	int regwriteaddr1[5];
	int regwriteaddr2[5];

	int branch; //m1[0] && zero2 ? order imp calculated in PENDING WRITES NOT IN THE THREAD!!
};
typedef struct exmemst exmem;
exmem regexmem;

struct memwbst{
	int instruction1[32];
	int instruction2[32];
	bool nop1;
	bool nop2;

	wb wb1 ; 
	wb wb2;
	
	int readdata1[32];
	int readdata2[32];
	int aluresult1[32];
	int aluresult2[32];
	int regwriteaddr1[5];
	int regwriteaddr2[5];
};
typedef struct memwbst memwb;
memwb regmemwb;

void instruction_to_register(int a[31],int b[5],int start);
bool arr5_equal(int a[5],int b[5]){ //a==b //working
	int i;
	for( i=0;i<5;i++){
		if(a[i] != b[i]){
			return false;
		}
	}
	return true;
}

int alphatonum(char c){
	if ( '0' <= c && c <= '9') return (c - '0');
	if ('a' <= c && c <= 'f') return (c- 'a' + 10);
	else return (c- 'A' + 10);
}

void to_hex(int a[8],int b[32]) //converts from hexadecimal to binary
{	int i;
	for( i=0;i<8;i++)
	{
		int k=0;
		int s = a[i];
		while(s>0)
		{
			b[(4*i)+k]=s%2;
			s=s/2;
			k++;
		}
		while(k<3)
		{
			b[(4*i)+k]=0;
			k++;
		}
	}
}

void storeInMem(char* hexStr, int idx){//takes data from lex and store in instruction memory 
	int i;
	int temp[8];
	for (i=0; i<8; i++){
		temp[7-i]= alphatonum(hexStr[i]);
	}
	to_hex(temp,instMem[idx]);
}


void datahazardcalculate(){// sets Stall for data hazards
	if (regidex.nop1 == true || regifid.nop1 == true) {
		stallIfId = false;
		return;
	}
	// calculated before pending write is done in pipeline registers.
	int op1[5];
	instruction_to_register(regifid.instruction1,op1,21); // puts 21 to 25 in op1

	int op2[5];
	instruction_to_register(regifid.instruction1,op2,16);

	if (regidex.m1.memre ==1 && (arr5_equal(regidex.rt1,op1) || arr5_equal(regidex.rt1,op2)) ){
		stallIfId = true;
		numOfDataStalls++;
	}
	else
		stallIfId = false;

	// update 3 hazard signals
}

void controlhazardcalculate(){//sets nop for branch
	if (regexmem.branch ==1){
		regidex.nop1 = true;
		regifid.nop1 = true;
		numOfControlStalls+=2;
	}
}

void forwardingcalculate(){//sets fwdA,fwdB,fwdC
	// calculated before the pending write.
	// we have specified not equal to zero thing
	fwdA=0;
	fwdB=0;
	fwdC=0;
	
	int op1[5];
	instruction_to_register(regidex.instruction2,op1,21); // puts 21 to 25 in op1

	int op2[5];
	instruction_to_register(regidex.instruction2,op2,16);

	int zero5[] = {0,0,0,0,0};
	if (regexmem.wb2.regwr==1 && !(arr5_equal(regexmem.regwriteaddr2,zero5)) && arr5_equal(regexmem.regwriteaddr2,op1) )
		fwdA = 2;
	else if (regmemwb.wb2.regwr==1 && !(arr5_equal(regmemwb.regwriteaddr2,zero5)) && arr5_equal(regmemwb.regwriteaddr2,op1)) 
		fwdA = 1;
	
	if (regexmem.wb2.regwr==1 && !(arr5_equal(regexmem.regwriteaddr2,zero5)) && arr5_equal(regexmem.regwriteaddr2,op2))
		fwdB = 2;
	else if (regmemwb.wb2.regwr==1 && !(arr5_equal(regmemwb.regwriteaddr2,zero5)) && arr5_equal(regmemwb.regwriteaddr2,op2)) 
		fwdB = 1;	 

	if (regmemwb.wb2.regwr==1 && (regexmem.m2.memwr == 1) && !(arr5_equal(regmemwb.regwriteaddr2,zero5)) && arr5_equal(regmemwb.regwriteaddr2,regexmem.regwriteaddr2))
	    fwdC=1;
}

void* ifstage(void *x ){ //thread calls this function for if
	int pcx;
	int i;
	if (regexmem.branch == 1)  {
		pcx = regexmem.pcadd1; 
	}
	else pcx = regifid.pc1;
	if (pcx >= numofinstructions) excessCount++;
	for(i=0; i<32; i++) regifid.instruction2[i] = instMem[pcx][i];
	regifid.pc2= pcx + 1;
	if (pcx >= numofinstructions) regifid.nop2= true;
	else regifid.nop2= false;
}

int bin6_to_int(int a[6])//convert binary 6 bit to int
{
	int sum=0;
	int p=1;
	int i;
	for(i=0;i<6;i++)
	{
		sum+=(p*a[i]);
		p=p*2;
	}
	return sum;	
}

int bin32_to_int_signed(int a[32]){//converts binary 32 bit to signed integer
	if(a[31]==0){
		int s;
		int sum=0;
		int p=1;
		for(s=0;s<31;s++){
			sum+=(a[s]*p);
			p=p*2;
		}
		return sum;
	}
	else{
		int s;
		int k[31];
		for(s=0;s<31;s++){
			if(a[s]==0) k[s]=1;
			else k[s]=0;
		}
	    s=0;
	    while(k[s]==1) {
		k[s]=0;
		s++;
		}
	    k[s]=1;
		int sum=0;
		int p=1;
		for(s=0;s<31;s++){
			sum+=(k[s]*p);
			p=p*2;
		}
		return (-1*sum); 
	}
}
void first6(int a[32],int b[6]);
int binary_to_int2(int a[6]);

void controller(int ins1[32],int ins[6],mem* memo,wb* wbo,alu* ex)
{
	// add & mul & mov: 01 ; mla:11 ; ldr, str :00 ; branch equal: 10 ori:33 slti:12 bge,blt:20 bgt:30 bleq=40 lui:50
	int k= bin6_to_int(ins);
	if(k==0){ // for add,sub, (including mul) etc.
		int fun[6]; 
		first6(ins1,fun);
		if(binary_to_int2(fun)==9 || binary_to_int2(fun)==8){
			memo->branchmem=1;
			//ex->link=1;
		}
		else{
			memo->branchmem=0;
		}	
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		if(binary_to_int2(fun)==24 || binary_to_int2(fun)==8){ // mul
			wbo->regwr=0;
		}
		else{ // not mult
			wbo->regwr=1;
		}
		ex->alusrc=0;
		ex->regdst=1;
		ex->aluop[0]=0;
		ex->aluop[1]=1;
	}

	else if(k==15){
		memo->branchmem=0; //for lui
		memo->memwr=0;
		memo->memre=0;
		wbo->mem2r=0;
		wbo->regwr=1;
		ex->alusrc=1;
		ex->regdst=0;
		ex->aluop[0]=5;
		ex->aluop[1]=0;
	}

	else if(k==28){ //for madd 
		memo->branchmem=0;
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=0;
		ex->regdst=1;
		ex->aluop[0]=1;
		ex->aluop[1]=1;
	}
	
	else if(k==35)
	{
		memo->branchmem=0; //for load
		memo->memwr=0;
		memo->memre=1;
		wbo->mem2r=1;
		wbo->regwr=1;
		ex->alusrc=1;
		ex->regdst=0;
		ex->aluop[0]=0;
		ex->aluop[1]=0;
		memAccess++;
	}
	else if(k==32){
		memo->branchmem=0; //for load byte
		memo->memwr=0;
		memo->memre=1;
		wbo->mem2r=1;
		wbo->regwr=1;
		ex->alusrc=1;
		ex->regdst=0;
		ex->aluop[0]=0;
		ex->aluop[1]=0;	
		memAccess++;
	}

	else if(k==40)
	{
		memo->branchmem=0; // for store byte
		memo->memre=0;
		memo->memwr=1;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=1;
		ex->regdst=0;
		ex->aluop[0]=0;
		ex->aluop[1]=0;
		memAccess++;

	}

	else if(k==43)
	{
		memo->branchmem=0; // for store
		memo->memre=0;
		memo->memwr=1;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=1;
		ex->regdst=0;
		ex->aluop[0]=0;
		ex->aluop[1]=0;
		memAccess++;
	}	

	else if(k==1){
		memo->branchmem=1; // for branch greater or equal to 0 or less than 0
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=0;
		ex->regdst=0;
		ex->aluop[0]=2;
		ex->aluop[1]=0;
	}

	else if(k==7){
		memo->branchmem=1; // for branch greater than 0
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=0;
		ex->regdst=0;
		ex->aluop[0]=3;
		ex->aluop[1]=0;
	}

	else if(k==6){
		memo->branchmem=1; // for branch less or equal 0
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=0;
		ex->regdst=0;
		ex->aluop[0]=4;
		ex->aluop[1]=0;
	}
	
	else if(k==4){
		memo->branchmem=1; // for branch equal
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=0;
		ex->regdst=0;
		ex->aluop[0]=1;
		ex->aluop[1]=0;
	}
    else if(k==2){ //for jump
    	memo->branchmem=1;
    	memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=0;
		ex->regdst=0;
		ex->aluop[0]=9;
		ex->aluop[1]=7;
	}
	else if(k==3){//for jump and link
		memo->branchmem=1;
    	memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=0;
		ex->alusrc=0;
		ex->regdst=0;
		ex->aluop[0]=9;
		ex->aluop[1]=8;	
	}
	else if(k==8){    //add immediate
		memo->branchmem=0;
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=1;
		ex->alusrc=1;
		ex->regdst=0;
		ex->aluop[0]=0;
		ex->aluop[1]=0;	
	}

	else if(k==10){ //slti
		memo->branchmem=0;
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=1;
		ex->alusrc=1;
		ex->regdst=0;
		ex->aluop[0]=1;
		ex->aluop[1]=2;		
	}

	else if(k==13){
		memo->branchmem=0; //ori
		memo->memre=0;
		memo->memwr=0;
		wbo->mem2r=0;
		wbo->regwr=1;
		ex->alusrc=1;
		ex->regdst=0;
		ex->aluop[0]=3;
		ex->aluop[1]=3;
	}

	else {
		printf("Error:Invalid Instruction.\n");
		exit(0);
	}
}

int bin5_to_int(int a[5]){ // convert 5 bit no to int
	int sum=0;
	int p=1;
	int i;
	for(i=0;i<5;i++)
	{
		sum+=(p*a[i]);
		p=p*2;
	}
	return sum;
}

void instruction_to_register(int a[31],int b[5],int start)//derives 5 bit register with lowest index start bit from instruction
{
	int k;
	for(k=0;k<5;k++)
	{
		b[k]=a[start+k];
	}	
}

void copy_register_value(int op[32],int s)//stores value of reg s in op
{
	int i;
	for( i=0;i<32;i++)
	op[i]=regMem[s][i];	

}

void extend_sign(int a[16],int b[32]) //stores a with extended sign in b
{
	int k=a[15];
	int i;
	for( i=0;i<16;i++)
	{
		b[i]=a[i];
	}
	for(i=16;i<32;i++)
	{
		b[i]=k;
	}
}
int copy_reg(int a[5],int copy[5])//copies a into copy
{
	int q;
	for(q=0;q<5;q++)
	copy[q]=a[q];
}

int copy_oper(int a[32],int copy[32])//copies a operand into copy
{
	int q;
	for(q=0;q<32;q++)
	copy[q]=a[q];	
}

void* idstage(void *x ){	//thread calls this function in idstage
	regidex.pc2= regifid.pc1;
	regidex.nop2= regifid.nop1;
	int opp[6];
	int k;
	copy_oper(regifid.instruction1,regidex.instruction2);
	
	for(k=0;k<6;k++)
	{
		opp[k]=regifid.instruction1[26+k];
	}	
	
	if (regifid.nop1== true) { // if no operation then make all control signals to be zero.
		regidex.m2.branchmem = 0;
		regidex.m2.memre = 0;
		regidex.m2.memwr = 0;
		regidex.wb2.mem2r = 0;
		regidex.wb2.regwr= 0;
		regidex.ex2.aluop[0] = 0;
		regidex.ex2.aluop[1] = 0;
		regidex.ex2.alusrc = 0;
		regidex.ex2.regdst = 0;
	}
	else  {
		controller(regifid.instruction1,opp,&(regidex.m2),&(regidex.wb2),&(regidex.ex2));//calculates all controllers
	}

	int op1[5];
	instruction_to_register(regifid.instruction1,op1,21); // puts 21 to 25 in op1
	copy_register_value(regidex.operanda2,bin5_to_int(op1)); // fetches values from reg file
	
	int op2[5];
	instruction_to_register(regifid.instruction1,op2,16);//puts 16 to 20 in op2
	copy_register_value(regidex.operandb2,bin5_to_int(op2));//fetches value from reg file
	
	int op3[16];
	int sk;
	for(sk=0;sk<16;sk++)
	{
		op3[sk]=regifid.instruction1[sk];	
	}	
	extend_sign(op3,regidex.constantext2);
	copy_reg(op2 ,regidex.rt2);
	
	int op4[5];
	instruction_to_register(regifid.instruction1,op4,11);
	copy_reg(op4,regidex.rd2);
}

void memory_equal(mem* s1,mem* s2)//make mem1 =mem2
{
	s1->branchmem=s2->branchmem;
	s1->memre=s2->memre;
	s1->memwr=s2->memwr;
}

void writeback_equal(wb* w1,wb* w2)//make wb1=wb2
{
	w1->mem2r=w2->mem2r;
	w1->regwr=w2->regwr;
}

void alu_equal(alu* copy,alu* orig)//make copy=orig
{
    copy->aluop[0]=orig->aluop[0];
    copy->aluop[1]=orig->aluop[1];
    copy->alusrc=orig->alusrc;
    copy->regdst=orig->regdst;
}

void shift_left_2(int a[32],int after_shift[32])//store 'a' left shift by 2 bits in after_shift
{
	after_shift[0]=0;
	after_shift[1]=0;
	int i;
	for(i=0;i<30;i++){
		after_shift[i+2]=a[i];
	}	
}

int binary_to_int2(int a[6]){//convert 6 bit binary to int
	int sum=0;
	int p=1;
	int i;
	for(i=0;i<6;i++)
	{
		sum+=(p*a[i]);
		p=p*2;
	}
	return sum;	
}

void Alu_control(int func[6],int aluop[2],int alucontrol[4])//check aluopbits and func bits to generate control signal for ALU
{
	if(aluop[1]==1 && aluop[0]==0){
		int i=0;
		if(binary_to_int2(func)==32)
		{
			alucontrol[3]=0;alucontrol[2]=0;alucontrol[1]=1;alucontrol[0]=0;          //0010 for add
		}
		else if(binary_to_int2(func)==36){
			alucontrol[3]=0;alucontrol[2]=0;alucontrol[1]=0;alucontrol[0]=0;          //0000 for and
		}
		else if(binary_to_int2(func)==34){
	        alucontrol[3]=0;alucontrol[2]=1;alucontrol[1]=1;alucontrol[0]=0;         //0110  	for  sub	
		}
		else if(binary_to_int2(func)==37){
			alucontrol[3]=0;alucontrol[2]=0;alucontrol[1]=0;alucontrol[0]=1;          // 0001    for OR
		}
		else if(binary_to_int2(func)==42){
	        alucontrol[3]=0;alucontrol[2]=1;alucontrol[1]=1;alucontrol[0]=2;          //0112     for SLT		
		}
		else if(binary_to_int2(func)==43){
	        alucontrol[3]=0;alucontrol[2]=1;alucontrol[1]=1;alucontrol[0]=1;          //0111     for SLTU		
		}
		else if(binary_to_int2(func)==24){
			alucontrol[3]=0;alucontrol[2]=0;alucontrol[1]=1;alucontrol[0]=1;        //0011 for mul
		}
		else if(binary_to_int2(func)==39)
		{
			alucontrol[3]=1;alucontrol[2]=1;alucontrol[1]=1;alucontrol[0]=0;          //1110 for nor
		}
		else if(binary_to_int2(func)==0)
		{
			alucontrol[3]=0;alucontrol[2]=0;alucontrol[1]=0;alucontrol[0]=2;         //0002 for sll
		}
		else if(binary_to_int2(func)==4)
		{
			alucontrol[3]=0;alucontrol[2]=0;alucontrol[1]=0;alucontrol[0]=3;         //0003 for sllv
		}
		else if(binary_to_int2(func)==18)
		{
			alucontrol[3]=1;alucontrol[2]=5;alucontrol[1]=9;alucontrol[0]=3;       //1593 for mflo
		}
		else if(binary_to_int2(func)==9){
			alucontrol[3]=7;alucontrol[2]=7;alucontrol[1]=7;alucontrol[0]=7;       //7777 for jalr
		}
		else if(binary_to_int2(func)==8){
			alucontrol[3]=7;alucontrol[2]=7;alucontrol[1]=7;alucontrol[0]=7;       //7777 for jump register
		}
		else {
			printf("Error:Invalid Instruction.\n");
			exit(0);
		}
	}
	
	else if(aluop[1]==1 && aluop[0]==1){
	    if(binary_to_int2(func)==0)
		{
			alucontrol[3]=1;alucontrol[2]=0;alucontrol[1]=0;alucontrol[0]=0;          //1000 for madd
		}	
	}
		
	else if(aluop[0]==0 && aluop[1]==0){ 		// for load and str and addi
		alucontrol[3]=0;alucontrol[2]=0;alucontrol[1]=1;alucontrol[0]=0;
	}
	
	else if(aluop[1]==0 && aluop[0]==1){	// branch
		alucontrol[3]=0;alucontrol[2]=1;alucontrol[1]=1;alucontrol[0]=0;
	}
	else if(aluop[1]==3 && aluop[0]==3){
		alucontrol[3]=0;alucontrol[2]=0;alucontrol[1]=0;alucontrol[0]=1;          // 0001    for OR
	}
	else if(aluop[0]==1 && aluop[1]==2){//SLTI
		alucontrol[3]=0;alucontrol[2]=1;alucontrol[1]=1;alucontrol[0]=2;
	}
	else if(aluop[0]==2 && aluop[1]==0){ //2222 for BGE or BLT
		alucontrol[3]=2;alucontrol[2]=2;alucontrol[1]=2;alucontrol[0]=2;
	}
	else if(aluop[0]==3 && aluop[1]==0){
		alucontrol[3]=3;alucontrol[2]=3;alucontrol[1]=3;alucontrol[0]=3; //3333 for BGT
	}
	else if(	aluop[0]==4 && aluop[1]==0){
	    alucontrol[3]=4;alucontrol[2]=4;alucontrol[1]=4;alucontrol[0]=4; //4444 for BLEQ	
	}
	else if(aluop[0]==5 && aluop[1]==0){
	    alucontrol[3]=5;alucontrol[2]=5;alucontrol[1]=5;alucontrol[0]=5; //5555 for LUI	
	}
    else if(aluop[0]==9 && aluop[1]==7){
    	alucontrol[3]=6;alucontrol[2]=6;alucontrol[1]=6;alucontrol[0]=6; //6666 for jump
	}
	else if(aluop[0]==9 && aluop[1]==8){
    	alucontrol[3]=6;alucontrol[2]=6;alucontrol[1]=6;alucontrol[0]=7; //6667 for jump and link
	}
		
}

void first6(int a[32],int b[6])//stores first 6 bit of a in b
{
	int i;
	for(i=0;i< 6;i++)
	b[i]=a[i];
}

int binary_to_int(int a[32])//converts 32 bit binary to int
{
	int sum=0;
	int p=1;
	int i;
	for(i=0 ; i<32;i++)
	{
		sum += (p*a[i]);
		p=p*2;
	}
	return sum;
}

void int_to_bin32(int a,int result[32])//converts int to 32 bit binary
{
	int i=0;
	for (i=0; i <32; i++){
		result[i]=0;
	}
	
	int s=0;
	while (a){
		result[s]= a%2;
		a= a/2;
		s++;
	}
}

void int_to_bin64(int a,int result[64])//converts int to 64 bit binary
{
	int i=0;
	for (i=0; i <64; i++){
		result[i]=0;
	}
	
	int s=0;
	while (a){
		result[s]= a%2;
		a= a/2;
		s++;
	}
}

void int_signed_to_bit32(int a,int result[32]){//converts signed int to 32 bit binary
	int i=0;
	if(a >= 0){
		int_to_bin32(a, result);
	}

	if(a<0){
		int k[32];
		int_to_bin32((-1*a),k);
		int s;
		
		for(s=0; s<32;s++){
			if(k[s]==0) result[s]=1;
			else result[s]=0;
		}

	    s=0;
	    while(result[s]==1) {
			result[s]=0;
			s++;
		}
	    result[s]=1;
	}
}

void int_signed_to_bit64(int a,int result[64]){// converts signed int to 64 bit binary
	int i=0;
	if(a>0){
	int_to_bin64(a, result);
	}
	if(a<0){
		int k[31];
		int_to_bin64((-1*a),k);
		int s;
		
		for(s=0;s<64;s++){
			if(k[s]==0) result[s]=1;
			else result[s]=0;
		}
	    s=0;
	    while(result[s]==1) {
		result[s]=0;
		s++;
		}
	    result[s]=1;
	}
}

bool result_equals_zero(int result[32])//checks if 32 bit result equals 0
{
	int i;
	for(i=0;i<32;i++)
	{
		if(result[i]!=0){
			return false;
		}
	}
	return true;
}

void shift(int a[32],int result[32],int shift){ // left shift a by shift amount and store in result
	int k;
	for(k=0;k<shift;k++)
	{ 
		result[k]=0;
	}

	for(k = shift;k<32;k++)
	{
		result[k] = a[k-shift];
	}
}

void ALU(int next_ins[32],int ins[32],int op1[32],int op2[32],int ctr[4],int result[32],int* zero ){
	*zero = 0;
	if(ctr[3]==0 && ctr[2]==0 && ctr[1]==1 && ctr[0]==0)//add signed a and b
	{
		int k1=bin32_to_int_signed(op1);
		int k2=bin32_to_int_signed(op2);
		int_signed_to_bit32(k1+k2,result);
		}	
	else if(ctr[3]==0 && ctr[2]==0 && ctr[1]==0 && ctr[0]==0)//and op1 and op2
	{
		int q;
		for(q=0;q<32;q++){
			result[q]=op1[q] & op2[q];
		}
	}

	else if(ctr[3]==0 && ctr[2]==1 && ctr[1]==1 && ctr[0]==0) // subtract
	{
		int k1= bin32_to_int_signed(op1);
		int k2=bin32_to_int_signed(op2);
		int_signed_to_bit32(k1-k2,result);
		int i;
		bool x =result_equals_zero(result);
				
		if(result_equals_zero(result)) (*zero) =1;
		else (*zero) = 0;
	}
	else if(ctr[3]==0 && ctr[2]==0 && ctr[1]==0 && ctr[0]==1)// or
	{
		int q;
		for(q=0;q<32;q++){
			result[q]=op1[q] | op2[q];
		}
	}

	else if(ctr[3]==0 && ctr[2]==1 && ctr[1]==1 && ctr[0]==1)//SLTU
	{
		int k1=binary_to_int(op1);
		int k2=binary_to_int(op2);
		int s;
		if(k1<k2)s=1;
		else{
			s=0;
		}
		int_to_bin32(s,result);
	}

	else if(ctr[3]==0 && ctr[2]==1 && ctr[1]==1 && ctr[0]==2){          //0112     for SLT)
		int k1=bin32_to_int_signed(op1);
		int k2=bin32_to_int_signed(op2);
		int s;
		if(k1<k2)s=1;
		else{s=0;
		}
		int_to_bin32(s,result);
	}

	else if(ctr[3]==0 && ctr[2]==0 && ctr[1]==1 && ctr[0]==1){        //0011 for mul)
		int k1=bin32_to_int_signed(op1);
		int k2=bin32_to_int_signed(op2);
		int result1[64];
		int_signed_to_bit32(k1*k2,result1);
		int s;
		for(s=0;s<32;s++) Lo[s]=result1[s];
		for(s=32;s<64;s++) Hi[s-32]=result1[s];
	}

	else if(ctr[3]==1 && ctr[2]==1 && ctr[1]==1 && ctr[0]==0){          //1110 for nor)
		int q;
		for(q=0;q<32;q++){
			result[q]=op1[q] | op2[q];
		}
        for(q=0;q<32;q++){
        	if(result[q]==0) result[q]=1;
        	else result[q]=0;
		}
	}
	else if(ctr[3]==0 && ctr[2]==0 && ctr[1]==0 && ctr[0]==2){     //0002 for sll
		int shamt[5];
		instruction_to_register(ins,shamt,6);
		int k2= bin5_to_int(shamt);
		shift(op2,result,k2);

	}
	
	else if(ctr[3]==0 && ctr[2]==0 && ctr[1]==0 && ctr[0]==3){      //0003 for sllv
		shift(op2,result,binary_to_int(op1));	
	}
	
	else if(ctr[3]==1 && ctr[2]==0 && ctr[1]==0 && ctr[0]==0){    //1000 for madd
		int k1=bin32_to_int_signed(op1);
		int k2=bin32_to_int_signed(op2);
		int result1[64];
		int_signed_to_bit64(k1*k2,result1);
		int s;
		int Lo1[32];
		int Hi1[32];
		int i;
		for(s=0;s<32;s++) Lo1[s]=result1[s];
		for(s=32;s<64;s++) Hi1[s-32]=result1[s];
		k1=bin32_to_int_signed(Lo1);
		k2=bin32_to_int_signed(Lo);
		int_signed_to_bit32(k1+k2,Lo);
		k1=bin32_to_int_signed(Hi1);
		k2=bin32_to_int_signed(Hi);
		int_signed_to_bit32(k1+k2,Hi);
	}
	else if(ctr[3]==2 && ctr[2]==2 && ctr[1]==2 && ctr[0]==2){ // for BGE and BLT
		int i;
		int op22[5];
			instruction_to_register(ins,op22,16);
		if(bin5_to_int(op22)==1){// BGE
			int k1 = bin32_to_int_signed(op1);
			if(k1>=0){ 
				*zero = 1;
			}
			else *zero=0;
		}
		else if(bin5_to_int(op22)==0){ //BLT
			int k1=bin32_to_int_signed(op1);
			if(k1<0){ *zero=1;
			}
			else *zero=0;
		}	
	}
	else if(ctr[3]==3 && ctr[2]==3 && ctr[1]==3 && ctr[0]==3){ //BGT
	    int k1=bin32_to_int_signed(op1);
		if(k1>0) *zero=1;
		else *zero=0;	
	}

	else if(ctr[3]==4 && ctr[2]==4 && ctr[1]==4 && ctr[0]==4){ //BLEQ
	    int k1=bin32_to_int_signed(op1);
		if(k1<=0) *zero=1;
		else *zero=0;	
	}

	else if(ctr[3]==5 && ctr[2]==5 && ctr[1]==5 && ctr[0]==5){ //LUI
	    int i;
		for( i=0;i<16;i++){
			result[i]=0;
		}	
		for(i=16;i<32;i++){
			result[i]=op2[i-16];
		}
	}
	else if(ctr[3]==1 && ctr[2]==5 && ctr[1]==9 && ctr[0]==3){//mflo
		copy_oper(Lo,result);
	}
	else if(ctr[3]==6 && ctr[2]==6 && ctr[1]==6 && ctr[0]==6){//jump
		int i;
		for( i=0;i<26;i++){
			result[i]=ins[i];
		}
		int j;
		for( j=26;j<32;j++){
			result[j]=0;
		}
		(*zero)=1;		
	}
	else if(ctr[3]==6 && ctr[2]==6 && ctr[1]==6 && ctr[0]==7){//jump and link
		int i;
		for( i=0;i<26;i++){
			result[i]=ins[i];
		}
		int j;
		for(j=26;j<32;j++){
			result[j]=0;
		}
		*zero=1;
		copy_oper(next_ins,regMem[31]);
	}
	else if(ctr[3]==7 && ctr[2]==7 && ctr[1]==7 && ctr[0]==7){//jalr && jr
		copy_oper(op1,result);
		*zero=1;		
	}
	
}
 
void* exstage(void *x ){

	regexmem.nop2= regidex.nop1;
	if (regidex.nop1==true){
		regexmem.m2.branchmem = 0;
		regexmem.m2.memre = 0;
		regexmem.m2.memwr = 0;
		regexmem.wb2.mem2r = 0;
		regexmem.wb2.regwr= 0;
	}
	else {
		memory_equal(&(regexmem.m2) ,&(regidex.m1));
		writeback_equal(&(regexmem.wb2) ,&(regidex.wb1));
	}
	
	copy_oper(regidex.instruction1,regexmem.instruction2);	
	

	int aluctr[4]; // tells the type of instruction in Siddhant's Rules of Nomenclature
	int fun[6]; // in instruction set style.
	first6(regidex.instruction1,fun);

	Alu_control(fun,regidex.ex1.aluop,aluctr); // gives control signals for alu

	int op2[32];
	if(fwdB==0){//forwarding
		copy_oper(regidex.operandb1,op2);	
	}
	else if(fwdB==1){		
		copy_oper(opglobal,op2);
	}
	else if(fwdB==2){
		copy_oper(regexmem.aluresult1,op2); 
	}
	
	int final_op2[32];

	if(regidex.ex1.alusrc==1){ // constant to be copied or normal operand
		copy_oper(regidex.constantext1,final_op2);//alusrc
	}
	else{
		copy_oper(op2,final_op2);
	}
	
	int final_op1[32];
	if(fwdA==0){
		copy_oper(regidex.operanda1,final_op1);	
	}
	else if(fwdA==1){
		copy_oper(opglobal,final_op1);
	}
	else if(fwdA==2){
		copy_oper(regexmem.aluresult1,final_op1); 
	}

	int i;
	ALU(regifid.instruction1,regidex.instruction1,final_op1,final_op2,aluctr,regexmem.aluresult2, &(regexmem.zero2)) ;
	if((aluctr[3]==6 && aluctr[2]==6 && aluctr[1]==6 && aluctr[0]==6)|| (aluctr[3]==6 && aluctr[2]==6 && aluctr[1]==6 && aluctr[0]==7)|| (aluctr[3]==7 && aluctr[2]==7 && aluctr[1]==7 && aluctr[0]==7)){
		regexmem.pcadd2 = binary_to_int(regexmem.aluresult2); //jump variations
	}
	else{ regexmem.pcadd2= regidex.pc1 + binary_to_int(regidex.constantext1);} //take care of offset
	copy_oper(op2,regexmem.memwritedata2); // for str
	
	
	if(regidex.ex1.regdst==0){ // if you want to store in register rt1 (for ldr , str) or rd1 (for add, sub)
		copy_reg(regidex.rt1,regexmem.regwriteaddr2);
	}
	
	else if(regidex.ex1.regdst==1){
		copy_reg(regidex.rd1,regexmem.regwriteaddr2);
	}
}

void data_read(int x,int a[32])
{
	int i;
	for(i=0;i<32;i++)
	{
		a[i]=dataMem[dataMemoryIndex(x)][i];
	}
}

void data_write(int x,int a[32]){
	int i;
	for(i=0;i<32;i++)
	{
		dataMem[dataMemoryIndex(x)][i]=a[i];
	}
}

void load_byte(int a,int offset,int result[32]){
	int s;
	for(s=0;s<8;s++){
		result[s]=dataMem[dataMemoryIndex(a)][(offset*8)+s];
	}
	int k=dataMem[dataMemoryIndex(a)][(offset*8)+7];
	for(s=8;s<32;s++){
		result[s]=k;
	}
}

void store_byte(int a,int offset,int result[32]){
	int s;
	
	for(s=0;s<8;s++){
		dataMem[dataMemoryIndex(a)][(offset*8)+s]=result[s];
	}

}

void* memstage(void *x ){
	regmemwb.nop2= regexmem.nop1;
	copy_oper(regexmem.instruction1,regmemwb.instruction2);
	writeback_equal(&(regmemwb.wb2) , &(regexmem.wb1)) ;
	int opp[6];
	int k1;
	for(k1=0;k1<6;k1++)
	{
		opp[k1]=regexmem.instruction1[26+k1];
	}	
	int k=binary_to_int2(opp);
	if(regexmem.m1.memre ==1){
		if(k==32)
		{
			int s=binary_to_int(regexmem.aluresult1);
			load_byte((s/4),(s%4),regmemwb.readdata2);	
		}
		else{
		data_read((binary_to_int(regexmem.aluresult1)/4),regmemwb.readdata2);}
	}
	copy_reg(regexmem.regwriteaddr1,regmemwb.regwriteaddr2);
	copy_oper(regexmem.aluresult1,regmemwb.aluresult2);
	int mwr[32];
	if(fwdC==1){
		copy_oper(opglobal,mwr);
	}
	else{
		copy_oper(regexmem.memwritedata1,mwr);
		int l=0;
		}
	if(regexmem.m1.memwr==1 ){
		if(k==40){ // store byte
			int s=binary_to_int(regexmem.aluresult1);
			store_byte((s/4),(s%4),mwr);	
		}
		else{
			data_write((binary_to_int(regexmem.aluresult1)/4),mwr);
		}	
	}
}

void register_write(int x,int a[32]){// writes x into 32 bit
	int i;
	for(i=0;i<32;i++)
	{
		regMem[x][i]=a[i];
	}
}

int binary_to_int1(int a[5]){//converts 5 bit binary to int
	int sum=0;
	int p=1;
	int i;
	for(i=0;i<5;i++)
	{
		sum+=(p*a[i]);
		p=p*2;
	}
	return sum;
}

void* wbstage(void *x ){//thread calls for write back
	if(regmemwb.wb1.mem2r==0){
		copy_oper(regmemwb.aluresult1,opglobal);
	}
	else if(regmemwb.wb1.mem2r==1){
		copy_oper(regmemwb.readdata1,opglobal);
	}
	
	if(regmemwb.wb1.regwr == 1){
		int k;int opp[6];
		for(k=0;k<6;k++)
		{
			opp[k]=regmemwb.instruction1[26+k];
		}	
		int fun[6]; 
		first6(regmemwb.instruction1,fun);
		if(bin6_to_int(opp)==0 && bin6_to_int(fun)==9){
			register_write(bin5_to_int(regmemwb.regwriteaddr1),regexmem.instruction1);
		}
		else{
			register_write(bin5_to_int(regmemwb.regwriteaddr1),opglobal);
		}
	}
}

void pendingWrites(){
	if (!stallIfId){
		copy_oper(regifid.instruction2,regifid.instruction1);
		regifid.pc1 = regifid.pc2;
		regifid.nop1 =regifid.nop2;
	}
	
	if (stallIfId){
		regidex.nop1= true; // adding nop for data hazard instructions
		regidex.m1.branchmem = 0;
		regidex.m1.memre = 0;
		regidex.m1.memwr = 0;
		regidex.wb1.mem2r = 0;
		regidex.wb1.regwr= 0;
		regidex.ex1.aluop[0] = 0;
		regidex.ex1.aluop[1] = 0;
		regidex.ex1.alusrc = 0;
		regidex.ex1.regdst = 0;
	}

	else {
		memory_equal(&(regidex.m1),&(regidex.m2));
		writeback_equal(&(regidex.wb1),&(regidex.wb2));
		alu_equal(&(regidex.ex1),&(regidex.ex2));
		regidex.nop1=regidex.nop2;
	}

	copy_oper(regidex.instruction2,regidex.instruction1);
	copy_oper(regidex.constantext2,regidex.constantext1);
	copy_oper(regidex.operanda2,regidex.operanda1);
	copy_oper(regidex.operandb2,regidex.operandb1);
	copy_reg(regidex.rd2,regidex.rd1);
	copy_reg(regidex.rt2,regidex.rt1);
	regidex.pc1=regidex.pc2;

	copy_oper(regexmem.instruction2,regexmem.instruction1);
	regexmem.zero1=regexmem.zero2;
	copy_oper(regexmem.aluresult2,regexmem.aluresult1);
	copy_oper(regexmem.memwritedata2,regexmem.memwritedata1);
	memory_equal(&(regexmem.m1),&(regexmem.m2));
	writeback_equal(&(regexmem.wb1) ,&(regexmem.wb2));
	copy_reg(regexmem.regwriteaddr2 ,regexmem.regwriteaddr1);
	regexmem.pcadd1 = regexmem.pcadd2;
	regexmem.nop1 = regexmem.nop2;
	
	if(regexmem.nop1 == false && regexmem.zero1 == 1 && regexmem.m1.branchmem==1){
		regexmem.branch=1;
	}
	else{ 
		regexmem.branch=0;
	}

	copy_oper(regmemwb.instruction2,regmemwb.instruction1);
	copy_oper(regmemwb.aluresult2,regmemwb.aluresult1);
	copy_oper(regmemwb.readdata2,regmemwb.readdata1);
	copy_reg(regmemwb.regwriteaddr2,regmemwb.regwriteaddr1);
	writeback_equal(&(regmemwb.wb1) ,&(regmemwb.wb2));
	regmemwb.nop1= regmemwb.nop2;
}

void initialize_oper(int a[32])
{
	int j;
	for(j=0;j<32;j++)
	a[j]=0;
}

void initialize_reg(int a[5])
{
	int j;
	for(j=0;j<5;j++)
	a[j]=0;
}

void initialize_mem(mem* m1)
{
	m1->branchmem=0;
	m1->memre=0;
	m1->memwr=0;
}

void initialize_alu(alu* a1){
	a1->aluop[0]=0;
	a1->aluop[1]=0;
	a1->alusrc=0;
	a1->regdst=0;
}

void initialize_wb(wb* write)
{
	write->mem2r=0;
	write->regwr=0;
}

void init_mem(){
	int i,j;
	for (i= numofinstructions; i< INSTSIZE; i++){
		for (j=0; j< 32; j++){
			instMem[i][j]= 0;
		}
	}	
	for(i=0;i<32;i++){
		for(j=0;j<32;j++){
			regMem[i][j]=0;
		}
	}
	
	for(i=0;i<MEMSIZE;i++){
		for(j=0;j<32;j++){
			dataMem[i][j]=0;
		}
	}	
}

void initialize(){
	init_mem();
	
	initialize_oper(regifid.instruction1);
	initialize_oper(regifid.instruction2);
	regifid.pc1=0;
	regifid.pc2=0;
	regifid.nop1= true;
	regifid.nop2= true;
	
	initialize_oper(regidex.instruction1);
	initialize_oper(regidex.instruction2);
	initialize_oper(regidex.constantext1);
	initialize_oper(regidex.constantext2);
	initialize_alu(&(regidex.ex1));
	initialize_alu(&(regidex.ex2));
	initialize_mem(&(regidex.m1));
	initialize_mem(&(regidex.m2));
	initialize_wb(&(regidex.wb1));
	initialize_wb(&(regidex.wb2));
	initialize_oper(regidex.operanda1);
	initialize_oper(regidex.operanda2);
	initialize_oper(regidex.operandb1);
	initialize_oper(regidex.operandb2);
	initialize_reg(regidex.rd1);
	initialize_reg(regidex.rd2);
	initialize_reg(regidex.rt1);
	initialize_reg(regidex.rt2);
	regidex.pc1=0;
	regidex.pc2=0;
	regidex.nop1= regidex.nop2= true;

	initialize_oper(regexmem.instruction1);
	initialize_oper(regexmem.instruction2);
	regexmem.zero1=0;
	regexmem.zero2=0;
	regexmem.pcadd1=0;
	regexmem.pcadd2=0;
	initialize_oper(regexmem.aluresult1);
	initialize_oper(regexmem.aluresult2);
	regexmem.branch=0;
	initialize_mem(&(regexmem.m1));
	initialize_mem(&(regexmem.m2));
	initialize_oper(regexmem.memwritedata1);
	initialize_oper(regexmem.memwritedata2);
	initialize_reg(regexmem.regwriteaddr1);
	initialize_reg(regexmem.regwriteaddr2);
	initialize_wb(&(regexmem.wb1));
	initialize_wb(&(regexmem.wb2));
	regexmem.nop1 = regexmem.nop2= true;

	initialize_oper(regmemwb.instruction1);
	initialize_oper(regmemwb.instruction2);
	initialize_oper(regmemwb.aluresult1);
	initialize_oper(regmemwb.aluresult2);
	initialize_oper(regmemwb.readdata1);
	initialize_oper(regmemwb.readdata2);
	initialize_reg(regmemwb.regwriteaddr1);
	initialize_reg(regmemwb.regwriteaddr2);
	initialize_wb(&(regmemwb.wb1));
	initialize_wb(&(regmemwb.wb2));
	regmemwb.nop1= regmemwb.nop2= true;

	stallIfId= false;
}

void* updateSVG(void* ptr){
	FILE* svgfp = fopen(image_file, "w");
	draw(svgfp,!stallIfId && (regifid.pc2 < numofinstructions),!regifid.nop1,!regidex.nop1,!regexmem.nop1,!regmemwb.nop1, regexmem.m1.memre, regexmem.m1.memwr,binary_to_int(regifid.instruction2),binary_to_int(regifid.instruction1),binary_to_int(regidex.instruction1),binary_to_int(regexmem.instruction1),binary_to_int(regmemwb.instruction1));
	fclose(svgfp);
}

int processor(){
	//pending writes in pipeline registers
	
	datahazardcalculate();
	forwardingcalculate();
	pendingWrites();
	controlhazardcalculate();

	 // wbstage(NULL);
	 // ifstage(NULL);
	 // idstage(NULL);
	 // exstage(NULL);
	 // memstage(NULL);

	pthread_attr_t attr;
	void *status;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_create(&callThd[0], &attr, wbstage, NULL);
	pthread_attr_destroy(&attr);
	int i;
	for(i=0;i<1;i++) {
  		pthread_join(callThd[i], &status);
  	}
  	pthread_attr_t attr1;
	pthread_attr_init(&attr1);
	pthread_attr_setdetachstate(&attr1, PTHREAD_CREATE_JOINABLE);
	pthread_create(&callThd[1], &attr1, memstage, NULL);
	pthread_create(&callThd[2], &attr1, ifstage, NULL);
	pthread_create(&callThd[3], &attr1, idstage, NULL);
	pthread_create(&callThd[4], &attr1, exstage, NULL);
	pthread_attr_destroy(&attr);

	for(i=1;i<5;i++) {
  		pthread_join(callThd[i], &status);
  	}
	pthread_create(&callThd[5], &attr1, updateSVG, NULL);
	pthread_join(callThd[5], &status);
  	
}

void print_alu(alu a){
	int i;
	printf("alu op");
	for (i=0; i<2; i++) printf("%d", a.aluop[i]);
	printf("\n");
	
	printf("alu dst:%d alu src: %d \n", a.regdst, a.alusrc);
	printf("\n");
}

void print_mem(mem a){
	int i;
	printf("mem op \n");
	printf("branchmem: %d mem read: %d mem write: %d \n",a.branchmem,a.memre,a.memwr);
}

void print_wb(wb a){
	int i; 
	printf("wb op \n");
	printf("m2r: %d regwrite: %d \n",a.mem2r,a.regwr);	
}

void printIns(){
	int i,j;
	for (i=0; i< numofinstructions; i++){
		for (j=0; j< 32; j++){
			printf("%d", instMem[i][j]);
		}
		printf("\n");
	}
}

printDebug(){
	int i;
	printf("\nregifid:\n");
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regifid.instruction1[i]);	
	}
	printf("\n");
	
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regifid.instruction2[i]);	
	}
	printf("\n");
	
	printf("%d\n",regifid.pc1);
	printf("%d\n",regifid.pc2);
	
	printf("\n***regidiex:\n");
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regidex.instruction1[i]);	
	}
	printf("\n");
	
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regidex.instruction2[i]);	
	}
	printf("\n");
	
	
	print_alu(regidex.ex1);
	print_alu(regidex.ex2);
	print_mem(regidex.m1);
	print_mem(regidex.m2);
	print_wb(regidex.wb1);
	print_wb(regidex.wb2);
	
	printf("pc1: %d\n",regidex.pc1);
	printf("pc2: %d\n",regidex.pc2);
	
	printf("Operand A\n");
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regidex.operanda1[i]);	
	}
	printf("\n");
	
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regidex.operanda2[i]);	
	}
	printf("\n");
	
	printf("Operand B\n");
	for (i=0; i < 32; i++){
		if (i%4==0) printf(" ");
		printf("%d",regidex.operandb1[i]);	
	}
	printf("\n");
	
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regidex.operandb2[i]);	
	}
	printf("\n");
	
	printf("ConstEx:\n");
	for (i=0; i < 32; i++){
		if (i%4==0) printf(" ");
		printf("%d",regidex.constantext1[i]);	
	}
	printf("\n");
	
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regidex.constantext2[i]);	
	}
	printf("\n");
	
	printf("Rt\n");
	for (i=0; i < 5; i++){
		printf("%d",regidex.rt1[i]);	
	}
	printf("\n");
	
	for (i=0; i < 5; i++){
		printf("%d",regidex.rt2[i]);	
	}
	printf("\n");
	
	printf("Rd\n");
	for (i=0; i < 5; i++){
		printf("%d",regidex.rd1[i]);	
	}
	printf("\n");
	
	for (i=0; i < 5; i++){
		printf("%d",regidex.rd2[i]);	
	}
	printf("\n");
	
	printf("\n\n****regexmem:\n");
	printf("Instruction\n");
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regexmem.instruction1[i]);	
	}
	printf("\n");
	
	
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regexmem.instruction2[i]);	
	}
	printf("\n");
	
	print_mem(regexmem.m1);
	print_mem(regexmem.m2);
	print_wb(regexmem.wb1);
	print_wb(regexmem.wb2);
	
	printf("pcadd1: %d\n",regexmem.pcadd1);
	printf("pcadd2: %d\n",regexmem.pcadd2);
	printf("zero1: %d\n",regexmem.zero1);
	printf("zero2: %d\n",regexmem.zero2);
	
	printf("ALU result1\n");
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regexmem.aluresult1[i]);	
	}
	printf("\n");

	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regexmem.aluresult2[i]);	
	}
	printf("\n");
	
	printf("Mem Write Data\n");
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regexmem.memwritedata1[i]);	
	}
	printf("\n");

	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regexmem.memwritedata2[i]);	
	}
	printf("\n");
	
	printf("Reg Write Address\n");
	for (i=0; i < 5; i++){
		//if (i%4==0)printf(" ");
		printf("%d",regexmem.regwriteaddr1[i]);	
	}
	printf("\n");

	for (i=0; i < 5; i++){
		//if (i%4==0)printf(" ");
		printf("%d",regexmem.regwriteaddr2[i]);	
	}
	printf("\n");
	
	printf("Branch : %d\n", regexmem.branch);
	
	
	printf("\n\n****regmemwb :\n");
	printf("Instruction\n");
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regmemwb.instruction1[i]);	
	}
	printf("\n");
	
	
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regmemwb.instruction2[i]);	
	}
	printf("\n");
	printf("Wb\n");
	//printf("%d\n%d\n", regmemwb.wb1, regmemwb.wb2);
	print_wb(regmemwb.wb1);
	print_wb(regmemwb.wb2);
	
	printf("Read Data\n");
	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regmemwb.readdata1[i]);	
	}
	printf("\n");

	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regmemwb.readdata2[i]);	
	}
	printf("\n");
	
	printf("ALU result\n");
	for (i=0; i < 32; i++){
		if (i%4 == 0)printf(" ");
		printf("%d",regmemwb.aluresult1[i]);	
	}
	printf("\n");

	for (i=0; i < 32; i++){
		if (i%4==0)printf(" ");
		printf("%d",regmemwb.aluresult2[i]);	
	}
	printf("\n");
	
	printf("reg Write Addr\n");
	for (i=0; i < 5; i++){
		//if (i%4==0)printf(" ");
		printf("%d",regmemwb.regwriteaddr1[i]);	
	}
	printf("\n");

	for (i=0; i < 5; i++){
		//if (i%4==0)printf(" ");
		printf("%d",regmemwb.regwriteaddr2[i]);	
	}
	printf("\n");
}

int instMemAnswer(int index){
	int x = (index*4 + 419304); return x;
}

void printResults(){
	numOfCycles--;
	excessCount-=5; // removing the last instruction excess
	int idleCycles = numOfDataStalls + numOfControlStalls + STALLOFFSET;
	int instAccess = numOfCycles - numOfDataStalls - STALLOFFSET; // 4-> for ending stalls
	int numofinstructions1= instAccess - numOfControlStalls;
	FILE* resfp = fopen(output_file, "w");
	fprintf(resfp,"Instructions,%d\n",numofinstructions1);
	fprintf(resfp,"Cycles,%d\n",numOfCycles);
	fprintf(resfp,"IPC,%.4f\n", ((float)numofinstructions1)/numOfCycles);
	if (numOfCycles%2==0) fprintf(resfp,"Time (ns),%d\n",numOfCycles/2);
	else fprintf(resfp,"Time (ns),%.4f\n",0.5*numOfCycles);
	if (idleCycles%2==0) fprintf(resfp,"Idle time (ns),%d\n",idleCycles/2);
	else fprintf(resfp,"Idle time (ns),%.4f\n",0.5*idleCycles);
	fprintf(resfp,"Idle time (%%),%.4f%%\n", (idleCycles*100.0) /numOfCycles);
	fprintf(resfp,"Cache Summary\nCache L1-I\nnum cache accesses,%d\nCache L1-D\nnum cache accesses,%d\n",instAccess- excessCount, memAccess);
	fclose(resfp);
}

void goProcessor(){
	initialize();
/*	int_to_bin32(12,regMem[12]);
	int_to_bin32(5,regMem[5]);
	int_to_bin32(4,regMem[4]);
	int_to_bin32(0,dataMem[6]);
	int_to_bin32(-17,regMem[11]);*/
	char str[50];
	int *brkaddr;
	char* strStep= "step";
	char* strMem= "memdump";
	char* strReg= "regdump";
	char* strBrk= "break";
	char* strDel= "delete";  
	char* strCon= "continue"; 
	int i,j;
	while(true){
		scanf (" %s", str);
		if (strcmp(str, strStep) == 0){
			if (regifid.pc2 >= numofinstructions + 5) {
				printf("Instructions Finished!\n");
				printResults();
				exit(0);
			}
			processor();
			numOfCycles++;
		}
		// else if(strcmp(str, strBrk)==0){
		// 	scanf("0x%x",&brkaddr);
		// }
		else if(strcmp(str, strDel)==0){
			int brdel;
			scanf("0x%x",&brdel);	
		}
		else if (strcmp(str, strMem) == 0){
			printf("MemDump:\n");
			int start, num;
			scanf(" 0x%x %d", &start, &num);
			if (start%4){
				printf("Invalid Address: Enter Multiples of 4.\n");
				exit(0);
			}
			int i=0;
			for (i=0; i< num; i++){
				printf("0x%08X: 0x%X\n",i*4 + start, binary_to_int(dataMem[dataMemoryIndex(i + start/4)]));
			}
		}

		else if (strcmp(str, strReg) == 0){
			printf("RegDump\n");
			for (i=0; i< 32; i++){
				printf("$%02d: 0x%08X\n",i, binary_to_int(regMem[i]));
			}
			printf("hi: 0x%08X\n", binary_to_int(Hi));
			printf("lo: 0x%08X\n", binary_to_int(Lo));
			printf("pc: 0x%08X\n", instMemAnswer(regifid.pc1));	
		}
		else {
			printf("Incorrect Command.\n");
			return;
		}
	}
}
