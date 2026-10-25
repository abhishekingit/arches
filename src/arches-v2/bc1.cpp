#include "bc1.hpp"
#include <algorithm>	

inline static uint16_t pack_rgb565(const rtm::vec3& color) {
	uint r = static_cast<uint>(std::clamp(color.x, 0.0f, 1.0f) * 31.0f + 0.5f);
	uint g = static_cast<uint>(std::clamp(color.y, 0.0f, 1.0f) * 63.0f + 0.5f);
	uint b = static_cast<uint>(std::clamp(color.z, 0.0f, 1.0f) * 31.0f + 0.5f);

	return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

inline static rtm::vec3 unpack_rgb565(uint16_t c) {
	float r = static_cast<float>((c >> 11) & 0x1F) / 31.0f;
	float g = static_cast<float>((c >> 5) & 0x3F) / 63.0f;
	float b = static_cast<float>(c & 0x1F) / 31.0f;
	return rtm::vec3(r, g, b);
}

inline static float rgb_error_squared(const rtm::vec3& a, const rtm::vec3& b) {
	float dr = a.x - b.x;
	float dg = a.y - b.y;
	float db = a.z - b.z;

	return dr * dr + dg * dg + db * db;
}

BC1Block encode_bc1_block(const rtm::vec4 texels[16]) {
	//encode bc1 block

	rtm::vec3 min_color(1.0f, 1.0f, 1.0f);
	rtm::vec3 max_color(0.0f, 0.0f, 0.0f);

	for (uint i = 0; i < 16; i++) {
		const rtm::vec4& texel = texels[i];

		min_color.x = std::min(min_color.x, texel.x);
		min_color.y = std::min(min_color.y, texel.y);
		min_color.z = std::min(min_color.z, texel.z);

		max_color.x = std::max(max_color.x, texel.x);
		max_color.y = std::max(max_color.y, texel.y);
		max_color.z = std::max(max_color.z, texel.z);

	}

	BC1Block block{};
	block.color0 = pack_rgb565(max_color);
	block.color1 = pack_rgb565(min_color);

	if (block.color0 < block.color1) {
		std::swap(block.color0, block.color1);
	}

	if (block.color0 == block.color1) {
		if (block.color0 < 0xFFFF) {
			block.color0++;
		}
		else {
			block.color1--;
		}
	}

	rtm::vec3 palette[4];

	palette[0] = unpack_rgb565(block.color0);
	palette[1] = unpack_rgb565(block.color1);
	palette[2] = (palette[0] * 2.0f + palette[1]) / 3.0f;
	palette[3] = (palette[0] + palette[1] * 2.0f) / 3.0f;

	block.selectors = 0;
	for (uint i = 0; i < 16; i++) {
		rtm::vec3 input(texels[i].x, texels[i].y, texels[i].z);

		uint best_index = 0;
		float best_error = rgb_error_squared(input, palette[0]);

		for (uint p = 1; p < 4; p++) {
			float error = rgb_error_squared(input, palette[p]);

			if (error < best_error) {
				best_error = error;
				best_index = p;
			}

		}
		block.selectors |= best_index << (2 * i);
	}


	return block;
}

rtm::vec4 decode_bc1_texel(const BC1Block& block, uint texel_index) {

	_assert(texel_index < 16);

	rtm::vec3 palette[4];

	palette[0] = unpack_rgb565(block.color0);
	palette[1] = unpack_rgb565(block.color1);

	palette[2] = (palette[0] * 2.0f + palette[1]) / 3.0f;
	palette[3] = (palette[0] + palette[1] * 2.0f) / 3.0f;

	uint selector = (block.selectors >> (2 * texel_index)) & 0x3;

	rtm::vec3 color = palette[selector];

	return rtm::vec4(color.x, color.y, color.z, 1.0f);

}


void test_bc1_solid_color() {
	rtm::vec4 texels[16];
	for (uint i = 0; i < 16; i++) {
		texels[i] = rtm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

	}
	BC1Block block = encode_bc1_block(texels);

	printf("BC1 block: color0=%04x color1=%04x selectors=%08x\n", block.color0, block.color1, block.selectors);


	for (uint i = 0; i < 16; i++) {
		rtm::vec4 decoded = decode_bc1_texel(block, i);

		_assert(decoded.x > 0.99f);
		_assert(decoded.y < 0.01f);
		_assert(decoded.z < 0.01f);

	}

	printf("BC1 solid color test passed.\n");
}