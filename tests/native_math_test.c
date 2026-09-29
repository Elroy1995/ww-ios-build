#include "native_math.h"
#include "StaticRecompABI.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stddef.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

static u32 seed=0x36E21A57;
static u32 random_u32(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static float random_float(void) {
    u32 b=random_u32();
    // Include cancellation, both signed zeros and a broad ordinary exponent
    // range, without spending most of the test on rejected NaN/denorm inputs.
    b=(b&0x807FFFFFu) | ((100+(b%50))<<23);
    if ((b&31)==0) b&=0x80000000;
    float f;memcpy(&f,&b,4);return f;
}
static void write_float(CPUState* c,u32 p,float f) {
    u32 b;memcpy(&b,&f,4);mem_write32(c,p,b);
}
static CPUState initial(u8* ram,u32 pc) {
    CPUState c={0};c.ram=ram;c.ram_size=GC_MAIN_RAM_SIZE;
    c.pc=pc;c.lr=0xFFFFFFFC;c.msr=PPC_MSR_FP;c.hid2=PPC_HID2_LSQE;
    c.cycle_budget=16384;c.cycle_deadline_budget=1000;c.downcount=-17;
    c.fpscr=(random_u32()&0xFFFFF000u); // sticky flags must be retained
    for(unsigned i=0;i<32;++i) {
        c.gpr[i]=random_u32();c.fpr[i]=(double)random_float();c.ps1[i]=(double)random_float();
    }
    c.gpr[1]=0x80004100;c.gpr[3]=0x80001000;c.gpr[4]=0x80002000;c.gpr[5]=0x80003000;
    c.reserve_valid=true;c.reserve_addr=c.gpr[5];
    mem_write32(&c,0x803F66F0,0);mem_write32(&c,0x803F66F4,0x3F800000);
    for(unsigned i=0;i<12;++i) {
        write_float(&c,c.gpr[3]+4*i,random_float());
        write_float(&c,c.gpr[4]+4*i,random_float());
    }
    return c;
}
static void unchanged(CPUState* c,u32 pc) {
    CPUState saved=*c;
    assert(!bluewake_native_math(c,pc));assert(memcmp(c,&saved,sizeof saved)==0);
}
int main(int argc,char**argv) {
    u8* a=calloc(1,GC_MAIN_RAM_SIZE);u8* b=calloc(1,GC_MAIN_RAM_SIZE);assert(a&&b);
    CPUState c=initial(a,0x8030D0FC);
    c.fpscr|=1;unchanged(&c,c.pc);c.fpscr&=~3u;
    c.cycle_deadline_budget=67;unchanged(&c,c.pc);c.cycle_deadline_budget=1000;
    c.gqr[0]=4;unchanged(&c,c.pc);c.gqr[0]=0;
    c.msr=0;unchanged(&c,c.pc);c.msr=PPC_MSR_FP;
    write_float(&c,c.gpr[3],NAN);unchanged(&c,c.pc);
    write_float(&c,c.gpr[3],0x1p-120f);unchanged(&c,c.pc);
    write_float(&c,c.gpr[3],1);c.gpr[5]=0xCC000000;unchanged(&c,c.pc);
    c.gpr[5]=c.gpr[1]-48;unchanged(&c,c.pc);
    c.gpr[5]=0x80003000;c.gpr[1]=0x803F6720;unchanged(&c,c.pc);
    puts("native math: fallback leaves state unchanged");
    if(argc<2) {free(a);free(b);return 0;}
    void* lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib){fprintf(stderr,"%s\n",dlerror());return 1;}
    StaticRecompGetModuleFn get=(StaticRecompGetModuleFn)dlsym(lib,STATICRECOMP_GET_MODULE_SYMBOL);
    assert(get);const StaticRecompModuleDesc* mod=get();
    assert(mod->cpu_state_size==sizeof c && strcmp(mod->game_id,"GZLE01")==0);
    const u32 entries[]={0x8030D0C8,0x8030D0FC,0x8030DA44};
    for(unsigned fn=0;fn<3;++fn) for(unsigned i=0;i<4000;++i) {
        memset(a,0,0x10000);c=initial(a,entries[fn]);
        if(fn==0 && i%2) c.gpr[4]=c.gpr[3];
        if(fn==1 && i%3) c.gpr[5]=c.gpr[3+i%3-1];
        if(fn==2 && i%2) c.gpr[5]=c.gpr[4];
        if(i%4==0) c.cycle_deadline_budget=0;
        if(i%4==1) c.cycle_deadline_budget=17+(fn==0?13:fn==1?51:21);
        if(fn==2 && i%7==0) c.ps1[6]=c.ps1[12]=NAN;
        memcpy(b,a,0x10000);memcpy(b+0x3F66F0,a+0x3F66F0,8);
        CPUState reference=c;reference.ram=b;
        mod->on_state_loaded(&reference);
        assert(mod->dispatch(&reference,reference.pc));
        ppc_fpscr_updated(&c);
        assert(bluewake_native_math(&c,c.pc));
        reference.ram=a;
        if(memcmp(&c,&reference,sizeof c) || memcmp(a,b,0x10000)) {
            fprintf(stderr,"mismatch function %08x case %u seed %08x\n",entries[fn],i,seed);
            for(unsigned j=0;j<sizeof c;++j) if(((u8*)&c)[j]!=((u8*)&reference)[j])
                fprintf(stderr,"CPU byte %u got %02x want %02x\n",j,((u8*)&c)[j],((u8*)&reference)[j]);
            for(unsigned j=0;j<0x10000;++j) if(a[j]!=b[j])
                fprintf(stderr,"RAM %08x got %02x want %02x\n",0x80000000+j,a[j],b[j]);
            return 1;
        }
    }
    puts("native math: 12000 full CPU and memory comparisons against personal module passed");
    if (argc>2 && strcmp(argv[2],"--bench")==0) {
        const unsigned iterations=2000000;
        for (unsigned fn=0;fn<3;++fn) {
            CPUState start=initial(a,entries[fn]);
            for (unsigned native=0;native<2;++native) {
                c=start;ppc_fpscr_updated(&c);
                struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);
                for(unsigned i=0;i<iterations;++i) {
                    c.pc=entries[fn];c.downcount=0;
                    if(native) assert(bluewake_native_math(&c,c.pc));
                    else assert(mod->dispatch(&c,c.pc));
                }
                clock_gettime(CLOCK_MONOTONIC,&t1);
                double ns=(t1.tv_sec-t0.tv_sec)*1e9+t1.tv_nsec-t0.tv_nsec;
                printf("%08x %s %.1f ns/call\n",entries[fn],native?"native":"translated",ns/iterations);
            }
        }
    }
    dlclose(lib);free(a);free(b);return 0;
}
