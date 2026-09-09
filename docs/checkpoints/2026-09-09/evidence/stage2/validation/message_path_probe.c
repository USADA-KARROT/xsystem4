#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/instructions.h"

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static int32_t i32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    int error = 0;
    struct ain *a = ain_open(argv[1], &error);
    if (!a) { fprintf(stderr, "ain_open error=%d\n", error); return 1; }
    printf("AIN version=%d functions=%d libraries=%d\n", a->version, a->nr_functions, a->nr_libraries);
    for (int l = 0; l < a->nr_libraries; l++) {
        struct ain_library *lib = &a->libraries[l];
        if (strcmp(lib->name, "PartsEngine")) continue;
        for (int n = 0; n < lib->nr_functions; n++) {
            struct ain_hll_function *f = &lib->functions[n];
            if (!strstr(f->name,"MessageWindow") && strcmp(f->name,"GetMessageUniqueID")
                && strcmp(f->name,"GetMessageVariableString") && strcmp(f->name,"Parts_SetText")
                && strcmp(f->name,"SetText") && strcmp(f->name,"Parts_AddPartsText")
                && strcmp(f->name,"Parts_SetFont") && strcmp(f->name,"SetKeyWaitShow")) continue;
            printf("HLL %d:%d %s return=%d argc=%d\n",l,n,f->name,f->return_type.data,f->nr_arguments);
            for(int j=0;j<f->nr_arguments;j++)
                printf("  arg%d %s type=%d\n",j,f->arguments[j].name,f->arguments[j].type.data);
        }
    }
    initialize_instructions(a->version);
    for (int n=0;n<a->nr_functions;n++) {
        struct ain_function *f=&a->functions[n];
        if(argc==3 ? !strstr(f->name,argv[2]) :
            (!strstr(f->name,"MessageTextModel") && !strstr(f->name,"MessageTextView")
             && !strstr(f->name,"Composer") && strcmp(f->name,"message")
             && !strstr(f->name,"GetMessageVariable"))) continue;
        printf("FUNCTION %d %s address=0x%X return=%d args=%d vars=%d\n",n,f->name,f->address,f->return_type.data,f->nr_args,f->nr_vars);
        if(f->address>=a->code_size) { puts("  no bytecode (sentinel declaration)"); continue; }
        uint32_t end=a->code_size;
        for(int i=0;i<a->nr_functions;i++)
            if(a->functions[i].address>f->address && a->functions[i].address<end) end=a->functions[i].address;
        for(uint32_t p=f->address;p+2<=end;) {
            unsigned op=u16(a->code+p);
            if(op>=NR_OPCODES){printf("invalid opcode %u at %X\n",op,p);break;}
            const struct instruction *in=&instructions[op];
            if(!strcmp(in->name,"CALLHLL")) {
                int l=i32(a->code+p+2),h=i32(a->code+p+6);
                if(l>=0 && l<a->nr_libraries && h>=0 && h<a->libraries[l].nr_functions)
                    printf("  0x%X CALLHLL %d:%d %s.%s\n",p,l,h,a->libraries[l].name,a->libraries[l].functions[h].name);
            } else if(!strcmp(in->name,"CALLFUNC")) {
                int fn=i32(a->code+p+2);
                if(fn>=0 && fn<a->nr_functions)
                    printf("  0x%X %s %d %s\n",p,in->name,fn,a->functions[fn].name);
            } else if(!strcmp(in->name,"CALLMETHOD")) {
                printf("  0x%X CALLMETHOD nargs=%d (target on VM stack)\n",p,i32(a->code+p+2));
            }
            unsigned width=2+in->nr_args*4; /* ip_inc is zero for VM branches. */
            if(p+width>end)break;
            p+=width;
        }
    }
    ain_free(a);
    return 0;
}
