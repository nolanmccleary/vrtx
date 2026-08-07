#include "tlsf.h"


#define LINEAR 7
#define SUB_BIN 5
#define BIN_COUNT 64 - LINEAR
#define SUB_BIN_COUNT 1 << SUB_BIN
#define MIN_ALLOC_SIZE ALIGN4(LINEAR)

