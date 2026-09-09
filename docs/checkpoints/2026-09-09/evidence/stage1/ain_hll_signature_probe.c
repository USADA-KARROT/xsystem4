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
    if (argc != 2) return 2;
    int error = 0;
    struct ain *a = ain_open(argv[1], &error);
    if (!a) { fprintf(stderr, "ain_open error=%d\n", error); return 1; }
    printf("AIN version=%d functions=%d libraries=%d\n", a->version, a->nr_functions, a->nr_libraries);
    for (int l = 0; l < a->nr_libraries; l++) {
        struct ain_library *lib = &a->libraries[l];
        if (strcmp(lib->name, "PartsEngine")) continue;
        for (int n = 0; n < lib->nr_functions; n++) {
            struct ain_hll_function *f = &lib->functions[n];
            if (!strstr(f->name, "GetPartsCGName")) continue;
            printf("HLL library=%d function=%d name=%s return_type=%d argc=%d\n", l,n,f->name,f->return_type.data,f->nr_arguments);
            for (int j = 0; j < f->nr_arguments; j++)
                printf("  arg[%d] name=%s type=%d struct=%d rank=%d\n",j,f->arguments[j].name,f->arguments[j].type.data,f->arguments[j].type.struc,f->arguments[j].type.rank);
        }
    }
    initialize_instructions(a->version);
    for (int n = 0; n < a->nr_functions; n++) {
        struct ain_function *f = &a->functions[n];
        if (strcmp(f->name, "parts::detail::CCGParts@CGName::get")) continue;
        printf("FUNCTION fno=%d name=%s address=0x%X return_type=%d args=%d vars=%d\n",n,f->name,f->address,f->return_type.data,f->nr_args,f->nr_vars);
        uint32_t end = a->code_size;
        for (int i = 0; i < a->nr_functions; i++)
            if (a->functions[i].address > f->address && a->functions[i].address < end) end=a->functions[i].address;
        for (uint32_t p=f->address, count=0; p+2<=end && count<40; count++) {
            unsigned op=u16(a->code+p);
            if (op>=NR_OPCODES) { printf("invalid opcode %u at %X\n",op,p);break; }
            const struct instruction *in=&instructions[op];
            printf("  0x%X %s",p,in->name);
            for (int k=0;k<in->nr_args && p+6+4*k<=end;k++) printf(" %d",i32(a->code+p+2+4*k));
            putchar('\n');
            if (in->ip_inc<2) break;
            p+=in->ip_inc;
        }
    }
    ain_free(a);
    return 0;
}
