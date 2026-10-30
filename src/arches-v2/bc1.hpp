#pragma once

#include "stdafx.hpp"
#include "rtm/rtm.hpp"


struct BC1Block {
	uint16_t color0;
	uint16_t color1;
	uint32_t selectors;
};

static_assert(sizeof(BC1Block) == 8, "BC1Block size must be 8 bytes");

struct BC1Texture {
	uint32_t width;
	uint32_t height;
	uint32_t blocks_x;
	uint32_t blocks_y;

	std::vector<BC1Block> blocks;
};

BC1Block encode_bc1_block(const rtm::vec4 texels[16]);
rtm::vec4 decode_bc1_texel(const BC1Block& block, uint texel_index);

BC1Texture encode_bc1_texture(const Texture2D& texture);
rtm::vec4 decode_bc1_texture_texel(const BC1Texture& bc1_texture, uint32_t x, uint32_t y);


void test_bc1_solid_color();
void test_bc1_texture(const Texture2D& texture);