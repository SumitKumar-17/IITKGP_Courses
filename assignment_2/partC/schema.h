// schema.h: the two row types the workload generator uses, repeated here so
// the join code can read fields. workload_gen.cpp keeps its own copies.
#pragma once
#include <cstddef>

struct Trade      { long trade_id; char symbol[8]; double price; long ts; };
struct Instrument { char symbol[8]; char venue[8]; int tick_size; };

static_assert(sizeof(Trade) == 32, "layout must match workload_gen.cpp");
static_assert(sizeof(Instrument) == 20, "layout must match workload_gen.cpp");
