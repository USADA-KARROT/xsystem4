/* Read-only AIN static surface inventory. Uses the existing pinned libsys4 parser.
 * Does not execute game code. Call counts are static sites, not runtime coverage.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"

static void js(const char *s) {
    putchar('"');
    for (const unsigned char *p=(const unsigned char *)s; *p; p++) {
        if (*p=='"' || *p=='\\') printf("\\%c", *p);
        else if (*p<32 || *p>=127) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}
static void typ(const struct ain_type *t) {
    printf("{\"data\":%d,\"struct\":%d,\"rank\":%d",t->data,t->struc,t->rank);
    if(t->array_type) {printf(",\"subtype\":");typ(t->array_type);}
    putchar('}');
}
int main(int argc,char **argv) {
    if(argc!=2) {fprintf(stderr,"usage: ain_surface game.ain\n");return 2;}
    int err=0;
    struct ain *a=ain_open(argv[1],&err);
    if(!a){fprintf(stderr,"ain_open failed %d\n",err);return 3;}
    initialize_instructions(a->version);
    size_t *ops=calloc(NR_OPCODES,sizeof(size_t));
    size_t **hll=calloc(a->nr_libraries,sizeof(size_t *));
    for(int i=0;i<a->nr_libraries;i++)hll[i]=calloc(a->libraries[i].nr_functions,sizeof(size_t));
    unsigned char *boundaries=calloc(a->code_size+1,1);
    size_t ip=0,total=0,invalid_hll=0;
    while(ip<a->code_size) {
        if(ip+2>a->code_size){fprintf(stderr,"truncated opcode at %zu\n",ip);return 4;}
        unsigned op=(uint16_t)LittleEndian_getW(a->code,ip);
        if(op>=NR_OPCODES || !instructions[op].name){fprintf(stderr,"unknown opcode %u at %zu\n",op,ip);return 5;}
        unsigned w=instruction_width(op);
        if(ip+w>a->code_size){fprintf(stderr,"truncated instruction at %zu\n",ip);return 6;}
        boundaries[ip]=1; ops[op]++;total++;
        if(op==CALLHLL) {
            int l=LittleEndian_getDW(a->code,ip+2),f=LittleEndian_getDW(a->code,ip+6);
            if(l<0 || l>=a->nr_libraries || f<0 || f>=a->libraries[l].nr_functions)invalid_hll++;
            else hll[l][f]++;
        }
        ip+=w;
    }
    printf("{\"ain_version\":%d,\"code_bytes\":%zu,\"decoded_bytes\":%zu,\"instruction_sites\":%zu,\"functions\":%d,\"structures\":%d,\"globals\":%d,\"delegates\":%d,\"main\":%d,\"alloc\":%d,\"msgf\":%d,\"invalid_hll_sites\":%zu,\"opcodes\":[",a->version,a->code_size,ip,total,a->nr_functions,a->nr_structures,a->nr_globals,a->nr_delegates,a->main,a->alloc,a->msgf,invalid_hll);
    int comma=0;
    for(int op=0;op<NR_OPCODES;op++)if(ops[op]) {
        if(comma++)putchar(',');printf("{\"code\":%d,\"name\":",op);js(instructions[op].name);printf(",\"sites\":%zu}",ops[op]);
    }
    printf("],\"libraries\":[");
    for(int l=0;l<a->nr_libraries;l++) {
        if(l)putchar(',');struct ain_library *lib=&a->libraries[l];
        printf("{\"index\":%d,\"name\":",l);js(lib->name);printf(",\"declared_functions\":%d,\"functions\":[",lib->nr_functions);
        for(int f=0;f<lib->nr_functions;f++) {
            if(f)putchar(',');struct ain_hll_function *fn=&lib->functions[f];
            printf("{\"index\":%d,\"name\":",f);js(fn->name);printf(",\"static_call_sites\":%zu,\"return_type\":",hll[l][f]);typ(&fn->return_type);printf(",\"arguments\":[");
            for(int j=0;j<fn->nr_arguments;j++){if(j)putchar(',');typ(&fn->arguments[j].type);}printf("]}");
        }
        printf("]}");
    }
    int invalid_function_addresses=0;
    int sentinel_addresses=0;
    for(int n=0;n<a->nr_functions;n++)if(a->functions[n].address>=a->code_size || !boundaries[a->functions[n].address]) {
        invalid_function_addresses++;
        if(a->functions[n].address==0xffffffffu)sentinel_addresses++;
        if(invalid_function_addresses<=12)fprintf(stderr,"non-boundary f%d address=0x%x name=%s\n",n,a->functions[n].address,a->functions[n].name);
    }
    printf("],\"invalid_function_addresses\":%d,\"sentinel_function_addresses\":%d,\"selected_functions\":[",invalid_function_addresses,sentinel_addresses);comma=0;
    const int selected[]={6371,20752,20761,27031,27034,36081};
    for(unsigned i=0;i<sizeof(selected)/sizeof(*selected);i++) {
        int n=selected[i];if(n>=a->nr_functions)continue;struct ain_function *f=&a->functions[n];
        if(comma++)putchar(',');printf("{\"fno\":%d,\"address\":%u,\"name\":",n,f->address);js(f->name);printf(",\"nr_args_slots\":%d,\"return_type\":",f->nr_args);typ(&f->return_type);printf(",\"variables\":[");
        for(int j=0;j<f->nr_vars;j++){if(j)putchar(',');typ(&f->vars[j].type);}printf("]}");
    }
    printf("]}\n");
    for(int l=0;l<a->nr_libraries;l++)free(hll[l]);free(hll);free(ops);free(boundaries);ain_free(a);return 0;
}
