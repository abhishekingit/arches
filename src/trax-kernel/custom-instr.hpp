#pragma once
#include "stdafx.hpp"

#ifndef __riscv
static std::atomic_uint _next_thread;
#endif

// uint32_t inline fchthrd()
// {
// #ifdef __riscv
// 	uint32_t value = 0;
// 	asm volatile("fchthrd %0\n\t" : "=r" (value));
// 	return value;
// #else
// 	return _next_thread++;
// #endif
// }

uint32_t inline fchthrd()
{
#ifdef __riscv
	uint32_t value = 0;
	asm volatile(".insn i 0x000b, 0, %0, x0, 0\n\t" : "=r" (value));
	return value;
#else
	return _next_thread++;
#endif
}

#ifndef __riscv
void reset_fchthrd()
{
 	_next_thread = 0;
}
#endif

inline void ebreak()
{
	#ifdef __riscv
	asm volatile
	(
		"ebreak\n\t"
	);
	#endif
}

rtm::vec4 inline sample2d(Texture2DMetadata* texture, rtm::vec2 uv)
{
#ifdef __riscv
	uint32_t addr = (uint64_t)texture;
	register float src0 asm("f0") = *(float*)&addr;
	register float src1 asm("f1") = uv.x;
	register float src2 asm("f2") = uv.y;

	register float dst0 asm("f28");
	register float dst1 asm("f29");
	register float dst2 asm("f30");
	register float dst3 asm("f31");

	asm volatile(".insn i 0xb, 0x6, %0, %4, 0\n\t" : "=f" (dst0), "=f" (dst1), "=f" (dst2), "=f" (dst3) : "f" (src0), "f" (src1), "f" (src2));
	return rtm::vec4(dst0, dst1, dst2, dst3);
#else
	return Texture2D(*texture).sample(uv);
#endif
}

rtm::vec4 inline sample2d(Texture2D* texture, rtm::vec2 uv)
{
	return sample2d(static_cast<Texture2DMetadata*>(texture), uv);
}
