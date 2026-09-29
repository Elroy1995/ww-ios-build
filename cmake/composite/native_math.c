#include "native_math.h"
#include <math.h>

// GZLE01 SDK leaves. Keep the original register results, stack stores, paired
// rounding, reservation invalidation and guest cycle accounting. The ordinary
// translation handles exceptional values, quantization and device accesses.
static unsigned long long s_hits[3], s_fallbacks[3];
void bluewake_native_math_report(void) {
    fprintf(stderr, "[native-math] copy=%llu/%llu concat=%llu/%llu vec=%llu/%llu (native/fallback)\n",
        s_hits[0],s_fallbacks[0],s_hits[1],s_fallbacks[1],s_hits[2],s_fallbacks[2]);
}
typedef struct Pair { float x, y; } Pair;
static Pair mul(Pair a, float b) { return (Pair){a.x*b, a.y*b}; }
static Pair madd(Pair a, float b, Pair c) {
    return (Pair){fmaf(a.x,b,c.x), fmaf(a.y,b,c.y)};
}
static void reg(CPUState* c, unsigned r, Pair p) {
    c->fpr[r]=p.x; c->ps1[r]=p.y;
}
static void store(CPUState* c, u32 p, float f) {
    u32 b; memcpy(&b,&f,4);
    // The entry guard has already resolved the complete output range as
    // unaliased RAM, and excluded write observers. No per-word lookup remains.
    clear_matching_reservation(c,p);
    write_be32(c->ram+(p-GC_RAM_BASE),b);
}
static void store_pair(CPUState* c, u32 p, Pair f) {
    store(c,p,f.x); store(c,p+4,f.y);
}
static Pair pair(const float* a) { return (Pair){a[0],a[1]}; }
static int ram(CPUState* c,u32 p,u32 n) {
    return ppc_dispatch_poll_read_stable(c,p,n) && (p & 3u)==0;
}
static int overlap(u32 a,u32 n,u32 b,u32 m) {
    return (u64)a < (u64)b+m && (u64)b < (u64)a+n;
}
static int load(CPUState* c,u32 p,float* a,unsigned n) {
    if (!ram(c,p,n*4)) return 0;
    const u8* data=c->ram+(p-GC_RAM_BASE);
    unsigned invalid=0;
    for (unsigned i=0;i<n;++i) {
        u32 bits=read_be32(data+4*i);
        memcpy(a+i,&bits,4);
        // All products and sums remain finite and outside the denormal range.
        // This also excludes NaNs whose payload/exception behavior must stay
        // on the instruction implementation, not the host's FP convention.
        u32 magnitude=bits & 0x7FFFFFFFu;
        invalid |= magnitude!=0 &&
            magnitude-0x2EDBE6FFu > 0x501502F9u-0x2EDBE6FFu;
    }
    return invalid==0;
}
static void fprf(CPUState* c,float f) {
    unsigned cls = f==0 ? (signbit(f)?0x12:0x02) : (signbit(f)?8:4);
    c->fpscr=(c->fpscr & ~0x1F000u) | (cls<<12);
}
static int ready(CPUState* c,unsigned cycles) {
    return c && !c->exception && (c->msr & PPC_MSR_FP) &&
        (c->hid2 & PPC_HID2_LSQE) && c->gqr[0]==0 &&
        (c->fpscr & 3u)==0 && c->cycle_budget>0 &&
        c->downcount> -c->cycle_budget &&
        (c->cycle_deadline_budget<=0 ||
         c->cycle_deadline_budget+c->downcount >= cycles) &&
        // A write observer can change state between instructions. Preserve its
        // original observation points by using the translated path instead.
        !g_mem_write_journal;
}
static int finish(CPUState* c,unsigned cycles,unsigned suffix) {
    c->downcount-=cycles; c->cycle_observation_suffix=suffix;
    c->pc=c->lr & ~3u; return 1;
}
static int copy_matrix(CPUState* c) {
    float a[12]; u32 src=c->gpr[3],out=c->gpr[4];
    if (!load(c,src,a,12) || !ram(c,out,48) ||
        (src!=out && overlap(src,48,out,48))) return 0;
    for (unsigned i=0;i<6;++i) {
        Pair p=pair(a+2*i); reg(c,i,p); store_pair(c,out+8*i,p);
    }
    return finish(c,13,1);
}
static int concat_matrix(CPUState* c) {
    float a[12],b[12]; u32 out=c->gpr[5],sp=c->gpr[1]-64;
    if (!load(c,c->gpr[3],a,12) || !load(c,c->gpr[4],b,12) ||
        !ram(c,out,48) || !ram(c,sp,64) ||
        overlap(sp,64,c->gpr[3],48) || overlap(sp,64,c->gpr[4],48) ||
        overlap(sp,64,out,48) || overlap(sp,64,0x803F66F0,8) ||
        !ram(c,0x803F66F0,8) ||
        mem_read32(c,0x803F66F0)!=0 || mem_read32(c,0x803F66F4)!=0x3F800000)
        return 0;
    mem_write32(c,sp,c->gpr[1]);
    mem_write64(c,sp+8,f64_bits(c->fpr[14]));
    mem_write64(c,sp+16,f64_bits(c->fpr[15]));
    mem_write64(c,sp+40,f64_bits(c->fpr[31]));
    Pair r[6];
    for (unsigned row=0;row<3;++row) {
        Pair x=mul(pair(b),a[4*row]);
        Pair y=mul(pair(b+2),a[4*row]);
        x=madd(pair(b+4),a[4*row+1],x);
        y=madd(pair(b+6),a[4*row+1],y);
        x=madd(pair(b+8),a[4*row+2],x);
        y=madd(pair(b+10),a[4*row+2],y);
        r[2*row]=x;
        r[2*row+1]=madd((Pair){0,1},a[4*row+3],y);
    }
    // All input loads precede any output store in this SDK leaf, including
    // the supported in-place products. Retain its write order too.
    const unsigned order[]={0,2,1,3,4,5};
    for (unsigned i=0;i<6;++i) store_pair(c,out+8*order[i],r[order[i]]);
    reg(c,0,r[5]); reg(c,1,pair(a+2)); reg(c,2,r[4]);
    reg(c,3,pair(a+6)); reg(c,4,pair(a+8)); reg(c,5,pair(a+10));
    for (unsigned i=0;i<6;++i) reg(c,6+i,pair(b+2*i));
    reg(c,12,r[0]); reg(c,13,r[1]);
    c->ps1[14]=r[2].y; c->ps1[15]=r[3].y; c->ps1[31]=1;
    c->gpr[6]=0x803F66F0;
    fprf(c,r[5].x);
    return finish(c,51,2);
}
static int mult_vec(CPUState* c) {
    float a[12],v[3]; u32 out=c->gpr[5];
    if (!load(c,c->gpr[3],a,12) || !load(c,c->gpr[4],v,3) ||
        !ram(c,out,12) || overlap(out,12,c->gpr[3],48)) return 0;
    Pair xy=pair(v),z={v[2],1}; reg(c,0,xy); reg(c,1,z);
    for (unsigned row=0;row<3;++row) {
        unsigned base=row==1?8:2;
        Pair m=pair(a+row*4), n=pair(a+row*4+2);
        Pair p={m.x*xy.x,m.y*xy.y};
        Pair q={fmaf(n.x,z.x,p.x),fmaf(n.y,z.y,p.y)};
        reg(c,base,m); reg(c,base+1,n); reg(c,base+2,p); reg(c,base+3,q);
        // ps_sum0 preserves and rounds the destination's second lane. This
        // lane is not a matrix result and can contain any prior FP value.
        unsigned dest=row==1?12:6;
        ppc_ps_sum0(c,dest,base+3,dest,base+3);
        store(c,out+4*row,(float)c->fpr[dest]);
    }
    return finish(c,21,1);
}
int bluewake_native_math(CPUState* c,u32 address) {
    unsigned index, cycles;
    switch (address) {
    case 0x8030D0C8: index=0; cycles=13; break;
    case 0x8030D0FC: index=1; cycles=51; break;
    case 0x8030DA44: index=2; cycles=21; break;
    default: return 0;
    }
    int handled=0;
    if (ready(c,cycles)) {
        if (index==0) handled=copy_matrix(c);
        else if (index==1) handled=concat_matrix(c);
        else handled=mult_vec(c);
    }
    if (handled) ++s_hits[index]; else ++s_fallbacks[index];
    return handled;
}
