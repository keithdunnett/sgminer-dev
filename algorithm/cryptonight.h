#ifndef __CRYPTONIGHT_H
#define __CRYPTONIGHT_H

typedef struct _CryptonightCtx
{
	uint64_t State[25];
	uint64_t Scratchpad[1 << 18];
} CryptonightCtx;

void cryptonight_regenhash(struct work *work);

#endif
