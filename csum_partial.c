// SPDX-License-Identifier: GPL-2.0
/*
 * arch/x86_64/lib/csum-partial.c
 *
 * This file contains network checksum routines that are better done
 * in an architecture-specific manner due to speed.
 */
 

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef uint32_t __wsum;
typedef uint64_t u64;
typedef uint32_t u32;
# define unlikely(x) __builtin_expect(!!(x), 0)

#define LOOPCOUNT 102400
#define PACKETSIZE 40

static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long ret, dummy;

	asm(
		"1:\tmov %2,%0\n"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:\t"
		"lea %2,%1\n\t"
		"and %3,%1\n\t"
		"mov (%1),%0\n\t"
		"leal %2,%%ecx\n\t"
		"andl %4,%%ecx\n\t"
		"shll $3,%%ecx\n\t"
		"shr %%cl,%0\n\t"
		"jmp 2b\n"
		".previous\n"
		:"=&r" (ret),"=&c" (dummy)
		:"m" (*(unsigned long *)addr),
		 "i" (-sizeof(unsigned long)),
		 "i" (sizeof(unsigned long)-1));
	return ret;
}

static inline unsigned add32_with_carry(unsigned a, unsigned b)
{
	asm("addl %2,%0\n\t"
	    "adcl $0,%0"
	    : "=r" (a)
	    : "0" (a), "rm" (b));
	return a;
}

static inline unsigned short from32to16(unsigned a) 
{
	unsigned short b = a >> 16; 
	asm("addw %w2,%w0\n\t"
	    "adcw $0,%w0\n" 
	    : "=r" (b)
	    : "0" (b), "r" (a));
	return b;
}

/*
 * Do a checksum on an arbitrary memory area.
 * Returns a 32bit checksum.
 *
 * This isn't as time critical as it used to be because many NICs
 * do hardware checksumming these days.
 *
 * Still, with CHECKSUM_COMPLETE this is called to compute
 * checksums on IPv6 headers (40 bytes) and other small parts.
 * it's best to have buff aligned on a 64-bit boundary
 */
__wsum csum_partial(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned odd, result;

	odd = 1 & (unsigned long) buff;
	if (unlikely(odd)) {
		if (unlikely(len == 0))
			return sum;
		temp64 += (*(unsigned char *)buff << 8);
		len--;
		buff++;
	}

	while (unlikely(len >= 64)) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq 4*8(%[src]),%[res]\n\t"
		    "adcq 5*8(%[src]),%[res]\n\t"
		    "adcq 6*8(%[src]),%[res]\n\t"
		    "adcq 7*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r" (temp64)
		    : [src] "r" (buff)
		    : "memory");
		buff += 64;
		len -= 64;
	}

	if (len & 32) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 32;
	}
	if (len & 16) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 16;
	}
	if (len & 8) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 8;
	}
	if (len & 7) {
		unsigned int shift = (8 - (len & 7)) * 8;
		unsigned long trail;

		trail = (load_unaligned_zeropad(buff) << shift) >> shift;

		asm("addq %[trail],%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [trail] "r" (trail));
	}
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);
	if (unlikely(odd)) { 
		result = from32to16(result);
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
	}
	return (__wsum)result;
}


__wsum __csum_partial(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned odd, result;

	odd = 1 & (unsigned long) buff;
	if (unlikely(odd)) {
		if (unlikely(len == 0))
			return sum;
		temp64 += (*(unsigned char *)buff << 8);
		len--;
		buff++;
	}

	while (unlikely(len >= 64)) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq 4*8(%[src]),%[res]\n\t"
		    "adcq 5*8(%[src]),%[res]\n\t"
		    "adcq 6*8(%[src]),%[res]\n\t"
		    "adcq 7*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r" (temp64)
		    : [src] "r" (buff)
		    : "memory");
		buff += 64;
		len -= 64;
	}

	if (len & 32) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 32;
	}
	if (len & 16) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 16;
	}
	if (len & 8) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 8;
	}
	if (len & 7) {
		unsigned int shift = (8 - (len & 7)) * 8;
		unsigned long trail;

		trail = (load_unaligned_zeropad(buff) << shift) >> shift;

		asm("addq %[trail],%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [trail] "r" (trail));
	}
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);
	if (unlikely(odd)) { 
		result = from32to16(result);
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
	}
	return (__wsum)result;
}


__wsum csum_partial40(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned result;

	while (unlikely(len >= 64)) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq 4*8(%[src]),%[res]\n\t"
		    "adcq 5*8(%[src]),%[res]\n\t"
		    "adcq 6*8(%[src]),%[res]\n\t"
		    "adcq 7*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
		    : [res] "+r" (temp64)
		    : [src] "r" (buff)
		    : "memory");
		buff += 64;
		len -= 64;
	}

	if (len & 32) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq 2*8(%[src]),%[res]\n\t"
		    "adcq 3*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 32;
	}
	if (len & 16) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq 1*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 16;
	}
	if (len & 8) {
		asm("addq 0*8(%[src]),%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory");
		buff += 8;
	}
	if (len & 7) {
		unsigned int shift = (8 - (len & 7)) * 8;
		unsigned long trail;

		trail = (load_unaligned_zeropad(buff) << shift) >> shift;

		asm("addq %[trail],%[res]\n\t"
		    "adcq $0,%[res]"
			: [res] "+r" (temp64)
			: [trail] "r" (trail));
	}
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);
	return (__wsum)result;
}


__wsum csum_partial41(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned result;

	asm("addq 0*8(%[src]),%[res]\n\t"
	    "adcq 1*8(%[src]),%[res]\n\t"
	    "adcq 2*8(%[src]),%[res]\n\t"
	    "adcq 3*8(%[src]),%[res]\n\t"
	    "adcq 4*8(%[src]),%[res]\n\t"
	    "adcq $0,%[res]"
		: [res] "+r" (temp64)
		: [src] "r" (buff)
		: "memory");
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);

	return (__wsum)result;
}

__wsum csum_partial42(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned result;

	asm("xorq          %%r9, %%r9  \n\t"
	    "movq   0*8(%[src]), %%rcx \n\t"
	    "adcx   1*8(%[src]), %%rcx \n\t"
	    "adcx   2*8(%[src]), %%rcx \n\t"
	    "adcx          %%r9, %%rcx \n\t"
	    "adox   3*8(%[src]), %[res]\n\t"
	    "adox   4*8(%[src]), %[res]\n\t"
	    "adox         %%rcx, %[res]\n\t"
	    "adox          %%r9, %[res]"
		: [res] "+d" (temp64)
		: [src] "r" (buff)
		: "memory", "rcx", "r9");
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);

	return (__wsum)result;
}
__wsum csum_partial43(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned result;

	/* 
	 * the xorq not only zeroes r9, it also clears CF and OF so that 
	 * the first adcx/adox work as expected getting no input carry 
	 * while setting the output carry in the correct flags
	 */
	asm("xorq          %%r9, %%r9  \n\t" 
	    "movq   0*8(%[src]), %%rcx \n\t"
	    "adcx   1*8(%[src]), %%rcx \n\t"
	    "adcx   2*8(%[src]), %%rcx \n\t"
	    "adcx   3*8(%[src]), %%rcx \n\t"
	    "adcx          %%r9, %%rcx \n\t"
	    "adox   4*8(%[src]), %[res]\n\t"
	    "adox         %%rcx, %[res]\n\t"
	    "adox          %%r9, %[res]"
		: [res] "+d" (temp64)
		: [src] "r" (buff)
		: "memory", "rcx", "r9");
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);

	return (__wsum)result;
}
__wsum csum_partial44(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned result;

	asm("movq 0*8(%[src]),%%rcx\n\t"
	    "addq 1*8(%[src]),%%rcx\n\t"
	    "adcq 2*8(%[src]),%%rcx\n\t"
	    "adcq  $0, %%rcx\n\t" 
	    "xorq %%r9, %%r9\n\t"
	    "addq 3*8(%[src]),%[res]\n\t"
	    "adcq 4*8(%[src]),%[res]\n\t"
	    "adcq %%rcx,%[res]\n\t"
	    "adcq $0,%[res]"
		: [res] "+r" (temp64)
		: [src] "r" (buff)
		: "memory", "rcx", "r9");
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);

	return (__wsum)result;
}

__wsum csum_partial45(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned result;

	asm("xorq %%r9, %%r9\n\t"
	    "movq 0*8(%[src]),%%rcx\n\t"
	    "addq 1*8(%[src]),%%rcx\n\t"
	    "adcq 2*8(%[src]),%%rcx\n\t"
	    "adcq 3*8(%[src]),%%rcx\n\t"
	    "adcq %%r9, %%rcx\n\t" 
	    "addq 4*8(%[src]),%[res]\n\t"
	    "adcq %%rcx,%[res]\n\t"
	    "adcq %%r9,%[res]"
		: [res] "+r" (temp64)
		: [src] "r" (buff)
		: "memory", "rcx", "r9");
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);

	return (__wsum)result;
}

__wsum csum_partial46(const void *buff, int len, __wsum sum)
{
	u64 temp64 = (u64)sum;
	unsigned result;

	asm("movq 0*8(%[src]),%%rcx\n\t"
	    "addq 1*8(%[src]),%%rcx\n\t"
	    "adcq 2*8(%[src]),%%rcx\n\t"
	    "adcq $0, %%rcx\n\t" 
	    "movq 3*8(%[src]),%[res]\n\t"
	    "addq 4*8(%[src]),%[res]\n\t"
	    "adcq %%rcx,%[res]\n\t"
	    "adcq $0,%[res]"
		: [res] "=&r" (temp64)
		: [src] "r" (buff)
		: "memory", "rcx");
	result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);

	return (__wsum)result;
}

__wsum csum_partial47(const void *buff, int len, __wsum sum)
{
	__wsum temp64 = sum;
	unsigned result;

	asm("xorq       %%rcx, %%rcx  \n\t"
	    "movl 0*4(%[src]), %%r9d\n\t"
	    "movl 1*4(%[src]), %%r11d\n\t"
	    "movl 2*4(%[src]), %%r10d\n\t"
	    
	    "addl 4*4(%[src]), %%r9d\n\t"
	    "adcl 5*4(%[src]), %%r9d\n\t"
	    "adcl       %%ecx, %%r9d\n\t"
	    
	    "addl 6*4(%[src]), %%r11d\n\t"
	    "adcl 7*4(%[src]), %%r11d\n\t"
	    "adcl	%%ecx, %%r11d\n\t"
	    
	    "addl 8*4(%[src]), %%r10d\n\t"
	    "adcl 3*4(%[src]), %%r10d\n\t"
	    "adcl	%%ecx, %%r10d\n\t"
	    
	    "addl 9*4(%[src]), %%edx\n\t"
	    "adcl       %%r9d, %%edx\n\t"    
	    "adcl      %%r11d, %%edx\n\t"    
	    "adcl      %%r10d, %%edx\n\t"    
	    "adcl      %%ecx,  %%edx\n\t"	
	        : [res] "+d" (temp64)
		: [src] "r" (buff)
		: "memory", "rcx", "r9", "r11", "r10");
	result = temp64;

	return (__wsum)result;
}

static inline __wsum csum_partial2(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return csum_partial40(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}

static inline __wsum csum_specialized(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return __csum_partial(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}

static inline __wsum csum_partial3(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return csum_partial41(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}
static inline __wsum csum_partial4(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return csum_partial42(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}
static inline __wsum csum_partial5(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return csum_partial43(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}
static inline __wsum csum_partial6(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return csum_partial44(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}

static inline __wsum csum_partial7(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return csum_partial45(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}
static inline __wsum csum_partial8(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return csum_partial46(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}
static inline __wsum csum_partial9(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len == 40)  {
		return csum_partial47(buff, len, sum);
	} else {
		return __csum_partial(buff, len, sum);
	}
}

static inline __wsum nulltest(const void *buff, int len, __wsum sum)
{
	return 2;
}


double cycles[64];
int cyclecount[64];
double cycles2[64];
int cyclecount2[64];
__wsum sum[64];
char *names[64];

void reset_data(void) 
{
	memset(cycles, 0, sizeof(cycles));
	memset(cyclecount, 0, sizeof(cyclecount));
	memset(names, 0, sizeof(names));
}

void decay_data(void) 
{
	int i;
	for (i = 0; i < 64; i++) {
	
		if (cyclecount[i] > 1024) {
			cyclecount[i] /= 2;
			cycles[i] /= 2.0;
		}
	}
	for (i = 0; i < 64; i++) {
	
		if (cyclecount2[i] > 1024) {
			cyclecount2[i] /= 2;
			cycles2[i] /= 2.0;
		}
	}
}

#define MEASURE(index, func, name) 					\
	sum[index] = 0;							\
	start = __builtin_ia32_rdtscp(&A);				\
	for (i = 0; i < LOOPCOUNT; i++)					\
		sum[index] = func(buffer + 2 * i, PACKETSIZE, sum[index]);	\
	end =  __builtin_ia32_rdtscp(&A);				\
	cycles[index] += 1.0 * (end - start)/LOOPCOUNT; 		\
	cyclecount[index]++;						\
	names[index] = name;						\
	sum[index+1] = 0;						\
	start = __builtin_ia32_rdtscp(&A);				\
	for (i = 0; i < LOOPCOUNT; i++)					\
		sum[index+1] = func(buffer+1 + 2 * i, PACKETSIZE, sum[index+1]);\
	end =  __builtin_ia32_rdtscp(&A);				\
	cycles[index+1] += 1.0 * (end - start)/LOOPCOUNT; 		\
	cyclecount[index+1]++;						\
	names[index+1] = name;						\
									\
	sum[index] = 0;							\
	start = __builtin_ia32_rdtscp(&A);				\
	for (i = 0; i < LOOPCOUNT; i++)	{				\
		asm volatile ("lfence\n\t.align 64\n\t" : : : "memory", "rcx");	\
		sum[index] = func(buffer + 2 * i, PACKETSIZE, sum[index]);	\
		asm volatile (".align 64\n\tlfence\n\t" : : : "memory", "rcx");	\
	}								\
	end =  __builtin_ia32_rdtscp(&A);				\
	cycles2[index] += 1.0 * (end - start)/LOOPCOUNT; 		\
	cyclecount2[index]++;						\
	names[index] = name;						\
	sum[index+1] = 0;						\
	start = __builtin_ia32_rdtscp(&A);				\
	for (i = 0; i < LOOPCOUNT; i++) {				\
		asm volatile ("lfence\n\t.align 64\n\t" : : : "memory", "rcx");		\
		sum[index+1] = func(buffer+1 + 2 * i, PACKETSIZE, sum[index+1]);\
		asm volatile (".align 64\n\tlfence\n\t" : : : "memory", "rcx");		\
	}								\
	end =  __builtin_ia32_rdtscp(&A);				\
	cycles2[index+1] += 1.0 * (end - start)/LOOPCOUNT; 		\
	cyclecount2[index+1]++;						\
	names[index+1] = name;						\



static void report(void)
{
	static time_t prevtime;
	int i;
	
	if (time(NULL) - prevtime >= 1) {
		printf("\033[H");
		for (i = 2; i < 64; i+=2) {
			if (names[i]) {
				printf("%02i:\t%5.1f / %5.1f cycles\t(%08x)\t%s  \n", i, cycles[i]/cyclecount[i], cycles2[i]/cyclecount2[i] - cycles2[0]/cyclecount2[0], sum[i], names[i]);
			}
		}
		printf("------- odd alignment ----- \n");
		for (i = 3; i < 64; i+=2) {
			if (names[i]) {
				printf("%02i:\t%5.1f / %5.1f cycles\t(%08x)\t%s  \n", i, cycles[i]/cyclecount[i], cycles2[i]/cyclecount2[i] - cycles2[0]/cyclecount2[0], sum[i], names[i]);
			}
		}
		prevtime = time(NULL);
	}

	decay_data();
}


int main(int argc, char **argv)
{
	char buffer[LOOPCOUNT * 4];
	int i;
	unsigned int A;
	uint32_t start, end;

	printf("\033[H\033[J");

	for (i = 0; i < LOOPCOUNT * 4; i++)
		buffer[i] = rand() & 255;

	/* power management warmup */
	for (i = 0; i < 5000; i++) {
		MEASURE(0, csum_partial, "Upcoming linux kernel version");
	}


	reset_data();
	
	while (1) {
	MEASURE(0, nulltest, "NULL test");

	MEASURE(2, csum_partial, "Upcoming linux kernel version");


	MEASURE(4, csum_specialized, "Specialized to size 40");

	MEASURE(22, csum_partial2, "Linux kernel minus alignment");
	MEASURE(24, csum_partial3, "Base minimization           ");
	MEASURE(26, csum_partial4, "ADX based interleave        ");
	MEASURE(28, csum_partial5, "Work in progress ADX interleave ");
	MEASURE(30, csum_partial6, "Work in progress non-ADX interleave ");
	MEASURE(32, csum_partial7, "Work in progress non-ADX interleave ");
	MEASURE(34, csum_partial8, "Assume zero ");
	MEASURE(36, csum_partial9, "32 bit train ");

	report();
	}
}