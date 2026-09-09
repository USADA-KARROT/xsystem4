
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
static bool system_IsDebugMode(void)
{
	const char *value = getenv("XSYS4_GAME_DEBUG");
	return value && !strcmp(value, "1");
}
int main(int argc,char**argv){
 assert(argc==2);unsetenv("XSYS4_GAME_DEBUG");assert(!system_IsDebugMode());
 const char*disabled[]={"","0","false","true","yes","01"," 1"};
 for(unsigned i=0;i<sizeof(disabled)/sizeof(disabled[0]);i++){setenv("XSYS4_GAME_DEBUG",disabled[i],1);assert(!system_IsDebugMode());}
 setenv("XSYS4_GAME_DEBUG","1",1);assert(system_IsDebugMode());unsetenv("XSYS4_GAME_DEBUG");
 setenv("ASAN_OPTIONS","detect_leaks=0",1);assert(!system_IsDebugMode());
 puts("Production IsDebugMode: default/empty/0/other strings=false, explicit 1=true, ASan env does not enable game debug: PASS");
 int err=0;struct ain*a=ain_open(argv[1],&err);assert(a);initialize_instructions(a->version);
 int system=ain_get_library(a,"system"),dbg=-1;assert(system>=0);
 for(int i=0;i<a->libraries[system].nr_functions;i++)if(!strcmp(a->libraries[system].functions[i].name,"IsDebugMode"))dbg=i;assert(dbg>=0);
 unsigned p=a->functions[4219].address;assert(LittleEndian_getW(a->code,p)==CALLHLL);assert(LittleEndian_getDW(a->code,p+2)==system && LittleEndian_getDW(a->code,p+6)==dbg);
 p+=instruction_width(CALLHLL);assert(LittleEndian_getW(a->code,p)==IFNZ);unsigned enabled=LittleEndian_getDW(a->code,p+2);p+=instruction_width(IFNZ);assert(LittleEndian_getW(a->code,p)==RETURN && enabled>p);
 printf("Actual AIN4219 gate: default returns before dump work; debug=1 reaches body at 0x%x: PASS\n",enabled);
 int add=-1;for(int i=0;i<a->nr_functions;i++)if(!strcmp(a->functions[i].name,"activity::detail::AddUserComponent"))add=i;assert(add>=0);
 unsigned main_start=a->functions[20590].address,add_pos=0,debug_pos=0;
 for(unsigned q=main_start;q<main_start+4096;q+=instruction_width(LittleEndian_getW(a->code,q))){
  int op=LittleEndian_getW(a->code,q);
  if(op==CALLFUNC && LittleEndian_getDW(a->code,q+2)==add)add_pos=q;
  if(op==CALLHLL && LittleEndian_getDW(a->code,q+2)==system && LittleEndian_getDW(a->code,q+6)==dbg){debug_pos=q;break;}
 }
 assert(add_pos && debug_pos && add_pos<debug_pos);p=debug_pos+instruction_width(CALLHLL);assert(LittleEndian_getW(a->code,p)==IFNZ);enabled=LittleEndian_getDW(a->code,p+2);p+=instruction_width(IFNZ);assert(LittleEndian_getW(a->code,p)==JUMP);unsigned join=LittleEndian_getDW(a->code,p+2);assert(enabled<join);
 p=enabled;assert(LittleEndian_getW(a->code,p)==PUSH);p+=instruction_width(PUSH);assert(LittleEndian_getW(a->code,p)==PUSH && LittleEndian_getDW(a->code,p+2)==4219);
 printf("Actual main: AddUserComponent at 0x%x precedes debug check 0x%x; conditional adds only debug dump callback4219 before common join0x%x: PASS\n",add_pos,debug_pos,join);
 ain_free(a);puts("Game debug production fixture: ALL PASS");
}
