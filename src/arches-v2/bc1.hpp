#pragma once

#include "stdafx.hpp"
#include "rtm/rtm.hpp"


struct BC1Block {
	uint16_t color0;
	uint16_t color1;
	uint32_t selectors;
};

static_assert(sizeof(BC1Block) == 8, "BC1Block size must be 8 bytes");

BC1Block encode_bc1_block(const rtm::vec4 texels[16]);
rtm::vec4 decode_bc1_texel(const BC1Block& block, uint texel_index);


void test_bc1_solid_color();