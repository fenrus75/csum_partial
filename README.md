DRAFT -- WORK IN PROGRESS -- DATA IN TABLES IS NOT REAL YET

## Introduction

In kernel commit
https://git.kernel.org/pub/scm/linux/kernel/git/tip/tip.git/commit/?h=x86/core&id=d31c3c683ee668ba5d87c0730610442fd672525f
Eric Dumazet optimized the csum_partial function for the x86-64
architecture. In his commit message, he noted that the use of this function
has effectively shifted from doing checksums for whole packets (which the
hardware does nowadays) to primarily doing a checksum for the 40 byte IPv6
header. And he then provides an optimization for this function, which shows
that the CPU usage of this function is significant and drops with his
optimization.

In this writeup, I'm going to take a deeper look at this function, and 
see if further optimizations are possible (spoiler: they are).




## What csum_partial does

The function calculates a 32 bit checksum of a block of data.  A checksum is
basically a simple addition function, but where the outgoing carry feeds
back into the checksum.  Because addition is a very gentle mathematical
function where the order of operation is completely unimportant (addition is
transitive, e.g.  A + B equals B + A), this gives a few key freedoms.  The
most important one (and used by the current csum_partial) is that you can
calculte a 64 bit checksum and then "fold" it into a 32 bit checksum by just
adding the upper and lower 32 bits (and then adding again the remaining
carry from this addition).  Likewise, if one wants a 16 bit carry, you can
"fold" the two halves of a 32 bit checkum together.

There really are only two messy parts in csum_partial:

* coping with the "tail" of the buffer for buffers where the size isn't a
  nice multiple of 8 or 4.
* coping with "weird alignment" in case the address of the buffer does not
  start at a multiple of two.



## The optimization logistics and overall strategy

For convenience and speed of development I ported Eric's csum_partial
function to a userspace testbench. This involved providing some the basic
helper functions used by the function and removing a few kernel-isms
that the kernel uses to help static analysis. Neither the functionality
nor the performance of the function is impacted by this port.

I also added the obvious glue logic for measuring and reporting cycles of a large number
of runs,

In terms of strategy, I'm going to focus on the statement that the 40 byte
input is the most common case and will specialize the code for it.


## Performance baseline

For this article, I will only be measuring buffer sizes of 40, even though
the code of course has to support arbitrary buffer sizes.

| Scenario          | Even aligned buffer | Odd aligned buffer |
| ----------------- | ------------------- | ------------------ |
| Baseline          | 11.1 cycles         | 19.2 cycles        |

Nothing much to say about a baseline, other than that the "odd aligned
buffer" case is shockingly more expensive.




## First step: Specializing

As a very first step, we're going to make the size of 40 bytes a special
case.

	static inline __wsum csum_partial(const void *buff, int len, __wsum sum)
	{
    		if (__builtin_constant_p(len) && len == 40)  {
			return __csum_partial(buff, len, sum);
		} else {
			return __csum_partial(buff, len, sum);
		}
	}

In this first step, we still call the original csum_partial() function (now
renamed to __csum_partial) but with an if statement that checks for a
compile-time constant len paraneter of 40 (this will obviously only work in
an inline function in a header).

| Scenario          | Even aligned buffer | Odd aligned buffer |
| ----------------- | ------------------- | ------------------ |
| Baseline          | 11.1 cycles         | 19.2 cycles        |
| Specialized       | 11.1 cycles         | 19.2 cycles        |

As you can see in the results table, nothing has changed yet


## Next step: Getting rid of the "Odd alignment" handling

The data shows that the handling of odd-aligned buffers is very slow. It
also is going to hurt further specialization, since it means sometimes we
process the buffer as 40 bytes, and sometimes as 1 + 39 + 1.
So lets see how bad the performance really is in the unaligned case by just
removing the special case:

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


| Scenario          | Even aligned buffer | Odd aligned buffer |
| ----------------- | ------------------- | ------------------ |
| Baseline          | 11.1 cycles         | 19.2 cycles        |
| Specialized       | 11.1 cycles         | 19.2 cycles        |
| Unaligned removed | 11.1 cycles         | 11.1 cycles        |



## And now: Removing dead code

Now that we only ever have to deal with 40 bytes (and not 39) we can remove
the while loop (for sizes >= 64), as well as the code dealing with a
remainder of 16 and remainders 7 or below. The compiler would have done this
as well, so this by itself is not going to be any win. However, we can now
fold the one extra "adcq" statement to deal with the 8 remaining case into
the code that deals with the first 32, effectively turning the code from 32
+ 8 into just doing 40. This will save one key operation since after each
block the remaining carry has to be folded back into the count, and by doing
this optimization we go from 2 blocks of 32 + 8 to 1 block of 40.

The resulting code now looks like this:

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

As you can see, the code is starting to look much simpler now, and this is
also the first time we gained some performance from the aligned case:

| Scenario          | Even aligned buffer | Odd aligned buffer |
| ----------------- | ------------------- | ------------------ |
| Baseline          | 11.1 cycles         | 19.2 cycles        |
| Specialized       | 11.1 cycles         | 19.2 cycles        |
| Unaligned removed | 11.1 cycles         | 11.1 cycles        |
| Dead Code Removed | 9.1 cycles          | 9.1 cycles         |


## Side track: Critical chain analysis

At this point it's interesting to analyze the code to see what the
fundamental floor of the performance of the code will be.
In the diagram below, I've drawn the essential parts of the compiler generated code (including the
"add32_with_carry") in a diagram where the arrows show the dependency graph
of this code. The Cascade Lake CPU that Eric used can execute upto 4
instructions each clock cycle, but as you can see in the diagram, there is a
chain of "add with carry" instructions that each depend on the previous
instruction to be completed. Or in other words, in reality the CPU will not
execute 4, but only 1 instruction each cycle. The critical chain is
9 instructions long (not counting the mov instruction). The "add" and "adc"
instructions have a latency of 1 clock cycle, while the "shr" instruction has a latency of TBD
cycles. This means that any implementation that uses this chain has an lower
bound of 10 cycles.

Since our measured performance was TBD cycles, it means that there really
isn't any more blood to squeeze out of this stone.

Or is there,..


## Rethinking the problem

We can admire the problem of this chain all day long, or try to polish the
code a bit more, but neither is going to give us any step in performance.

Having reached a dead end, it's time to take a step back. The reason we're
at TBD cycles is that we have one long chain. To break through this barrier
we therefore need to find a way to split the chain in seperate pieces that
can execute in parallel.

Earlier I wrote that addition is a very gentle, transitive mathematical
function. Which means that one could transform a sequential such as

R = A + B + C + D

into

R = (A + B) + (C + D)

where (A + B) and (C + D) can be computed in parallel, turning a dependency
chain of 3 cycles into a chain of 2 cycles.

[TBD 2 pictures of graphs that show this]

Since our problem actually has 5 + 1 (the final carry) additions, we should
be able to use this trick!
The only complication is the final carry that we need to absorb back into
our sum; in the next few sections we'll see what limits this imposes.


## Tangent: ADX instruction set extensions

In certain types of cryptographic code, pairs of such long "add with carry" chains
are common, and having only one carry flag ended up being a huge performance
limiter. Because of this, Intel added 2 instructions (together called ADX)
to the TBD generations of CPUs in [YEAR TBD]. One instruction (ADCX) will use and
set ONLY the carry flag, while the other instruction wiil use and set ONLY
the overflow flag. Because these two instructions use and set a disjoint set
of CPU flags, they can be interleaved in a code stream without having
dependencies between them. 

For more information, Wikipedia has a page: https://en.wikipedia.org/wiki/Intel_ADX


## Using ADX for csum_partial:


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


| Scenario          | Even aligned buffer | Odd aligned buffer |
| ----------------- | ------------------- | ------------------ |
| Baseline          | 11.1 cycles         | 19.2 cycles        |
| Specialized       | 11.1 cycles         | 19.2 cycles        |
| Unaligned removed | 11.1 cycles         | 11.1 cycles        |
| Dead Code Removed | 9.1 cycles          | 9.1 cycles         |
| Using ADX         | 6.1 cycles          | 6.1 cycles         |

Even though this codepath has one extra add (to fold the carry into the sum
for the "adcx" side of the flow), the overall performance is a win!


## 2 streams without ADX

In the ADX example, you might notice that the code doesn't actually
interleave ADCX and ADOX, but that it depends on the out of order engine for
this. This implies it should be possible to also do something similar
with using straight Add-with-carry instructions. Since ADX is somewhat
recent (not even an entire decade) it'll be useful to explore this path
as well and see how close we can get.

	__wsum csum_partial44(const void *buff, int len, __wsum sum)
	{
		u64 temp64 = (u64)sum;
		unsigned result;

		asm("xorq %%r9, %%r9\n\t"
		    "movq 0*8(%[src]),%%rcx\n\t"
		    "addq 1*8(%[src]),%%rcx\n\t"
		    "adcq 2*8(%[src]),%%rcx\n\t"
		    "addq 3*8(%[src]),%%rcx\n\t"
		    "adcq  %%r9, %%rcx\n\t" 
		    "adcq 4*8(%[src]),%[res]\n\t"
		    "adcq %%rcx,%[res]\n\t"
		    "adcq %%r9,%[res]"
			: [res] "+r" (temp64)
			: [src] "r" (buff)
			: "memory", "rcx", "r9");
		result = add32_with_carry(temp64 >> 32, temp64 & 0xffffffff);

		return (__wsum)result;
	}

And the data shows that we don't actually need ADX for this purpose.. we can
get parity without ADX.

| Scenario          | Even aligned buffer | Odd aligned buffer |
| ----------------- | ------------------- | ------------------ |
| Baseline          | 11.1 cycles         | 19.2 cycles        |
| Specialized       | 11.1 cycles         | 19.2 cycles        |
| Unaligned removed | 11.1 cycles         | 11.1 cycles        |
| Dead Code Removed | 9.1 cycles          | 9.1 cycles         |
| Using ADX         | 6.1 cycles          | 6.1 cycles         |
| Two Streams       | 6.1 cycles          | 6.1 cycles         |


# The final potential step

So this was a fun poking session but my flight is starting to decent and my
internal goal of beating Eric's code by 2x has not been achieved yet.

The only thing I can think of right now to push the algorithm over the edge
is another specialization, and that is setting the input checksum to zero.
While this may sound like a useless thing, in reality calculating checksums
of headers and such most likely is the only thing that is checksum'd, which
mean a value of zero is going to be plugged in there. 
By doing this, we can skip one add which also allows to have a more balanced
2 halves of the tree... giving a potential of 2 cycles.

The code now looks like this

	__wsum csum_partial_40len_zerosum(const void *buff, int len, __wsum sum)
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
	

Now this optimization exposed some funky things in the test framework,
where gcc was all too clever by itself and managed to optimize the loop
away until I tweaked the framework code for it not to do that.


| Scenario          | Even aligned buffer | Odd aligned buffer |
| ----------------- | ------------------- | ------------------ |
| Baseline          | 11.1 cycles         | 19.2 cycles        |
| Specialized       | 11.1 cycles         | 19.2 cycles        |
| Unaligned removed | 11.1 cycles         | 11.1 cycles        |
| Dead Code Removed | 9.1 cycles          | 9.1 cycles         |
| Using ADX         | 6.1 cycles          | 6.1 cycles         |
| Two Streams       | 6.1 cycles          | 6.1 cycles         |
| Assume Zero Input | 4.0 cycles          | 4.0 cycles         |

Either way, the final goal is realized where a more-than-2x performance
increase has been achieved.




# Bonus section

Some of my coworkers and others who look at the intersection of low level 
software and CPU microarchitecture realize that the CPUs Out Of Order engine
is hiding latency in the examples and numbers above. One can debate if that
is valid or not for this case. For now, I'm leaning towards it being valid
since in a real world code flow, the Out of Order engine will also hide
latencies as its primary function.

But just for interest, I made a set of measurements where I put an lfence
instruction (which effectively blocks the OOO engine) on either side of the
call to the checksum function to measure a worst-case end-to-end latency.

The data of this experiment is in the table below:

Latency measurement with OOO force killed
| Scenario          | Even aligned buffer | Odd aligned buffer |
| ----------------- | ------------------- | ------------------ |
| Baseline          | 19.1 cycles         | 26.9 cycles        |
| Specialized       | 18.2 cycles         | 26.9 cycles        |
| Unaligned removed | 18.4 cycles         | 18.8 cycles        |
| Dead Code Removed | 14.0 cycles         | 15.2 cycles        |
| Using ADX         | 15.8 cycles         | 17.8 cycles        |
| Two Streams       | 16.3 cycles         | 16.5 cycles        |
| Assume Zero Input | 14.4 cycles         | 14.1 cycles        |

In playing with the code, it's clear that it is often possible to reduce these
"worst case" latencies one or two cycles at the expense of the "with OOO"
performance.