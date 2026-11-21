/* Program entry point and the few ARM EABI runtime helpers that libgcc would normally provide.
   We link without a sysroot, so these live here. */
#include <stdint.h>

int main(int, char **);
extern int __libc_start_main(int (*)(int, char **), int, char **, void *, void *, void *, void *);

/* Same register/stack setup as glibc's sysdeps/arm/start.S:
   __libc_start_main(main, argc, argv, init=0, fini=0, rtld_fini (r0 from ld.so), stack_end) */
__attribute__((naked, noreturn, used)) void _start(void) {
    __asm__ volatile(
        "mov fp, #0\n"
        "mov lr, #0\n"
        "pop {r1}\n"          /* argc */
        "mov r2, sp\n"        /* argv */
        "push {r2}\n"         /* stack_end */
        "push {r0}\n"         /* rtld_fini */
        "mov ip, #0\n"
        "push {ip}\n"         /* fini */
        "ldr r0, =main\n"
        "mov r3, #0\n"        /* init */
        "bl __libc_start_main\n"
        "1: b 1b\n"
        ".ltorg\n");
}

#define AEABI __attribute__((pcs("aapcs"), used))

AEABI double __aeabi_ul2d(uint64_t u) {
    return (double)(uint32_t)(u >> 32) * 4294967296.0 + (double)(uint32_t)u;
}
AEABI double __aeabi_l2d(int64_t v) {
    uint64_t u = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    double d = (double)(uint32_t)(u >> 32) * 4294967296.0 + (double)(uint32_t)u;
    return v < 0 ? -d : d;
}
AEABI float __aeabi_l2f(int64_t v) { return (float)__aeabi_l2d(v); }
AEABI uint64_t __aeabi_d2ulz(double d) {
    if (!(d > 0)) return 0;
    if (d >= 18446744073709551615.0) return UINT64_MAX;
    uint32_t hi = (uint32_t)(d / 4294967296.0);
    double rem = d - (double)hi * 4294967296.0;
    if (rem < 0) rem = 0;
    return ((uint64_t)hi << 32) | (uint32_t)rem;
}
AEABI int64_t __aeabi_d2lz(double d) {
    int neg = d < 0;
    if (neg) d = -d;
    if (d >= 9223372036854775807.0) return neg ? INT64_MIN : INT64_MAX;
    uint64_t u = __aeabi_d2ulz(d);
    return neg ? -(int64_t)u : (int64_t)u;
}

/* 64-bit division: quotient in r0:r1, remainder in r2:r3 */
__attribute__((used)) uint64_t atv_udivmod64(uint64_t n, uint64_t d, uint64_t *rem) {
    if (d == 0) { *rem = 0; return 0; }
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    *rem = r;
    return q;
}
__attribute__((used)) int64_t atv_sdivmod64(int64_t n, int64_t d, int64_t *rem) {
    int nn = n < 0, nd = d < 0;
    uint64_t un = nn ? (uint64_t)0 - (uint64_t)n : (uint64_t)n, ud = nd ? (uint64_t)0 - (uint64_t)d : (uint64_t)d, ur;
    uint64_t q = atv_udivmod64(un, ud, &ur);
    *rem = nn ? -(int64_t)ur : (int64_t)ur;
    return (nn != nd) ? -(int64_t)q : (int64_t)q;
}
__attribute__((naked, used)) void __aeabi_uldivmod(void) {
    __asm__ volatile(
        "push {r4, lr}\n"
        "sub sp, sp, #16\n"
        "add r4, sp, #8\n"
        "str r4, [sp]\n"
        "bl atv_udivmod64\n"
        "ldrd r2, r3, [sp, #8]\n"
        "add sp, sp, #16\n"
        "pop {r4, pc}\n");
}
__attribute__((naked, used)) void __aeabi_ldivmod(void) {
    __asm__ volatile(
        "push {r4, lr}\n"
        "sub sp, sp, #16\n"
        "add r4, sp, #8\n"
        "str r4, [sp]\n"
        "bl atv_sdivmod64\n"
        "ldrd r2, r3, [sp, #8]\n"
        "add sp, sp, #16\n"
        "pop {r4, pc}\n");
}
