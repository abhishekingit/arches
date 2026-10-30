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


BC1Texture encode_bc1_texture(const Texture2D& texture) {
	BC1Texture output;
	output.width = texture.width;
	output.height = texture.height;
	output.blocks_x = (texture.width + 3) / 4;
	output.blocks_y = (texture.height + 3) / 4;

	uint tex_width = static_cast<uint>(texture.width);
	uint tex_height = static_cast<uint>(texture.height);

	_assert(tex_width > 0);
	_assert(tex_height > 0);


	output.blocks.resize(output.blocks_x * output.blocks_y);

	for (uint block_y = 0; block_y < output.blocks_y; block_y++) {
		for (uint block_x = 0; block_x < output.blocks_x; block_x++) {
			rtm::vec4 texels[16];

			for (uint local_y = 0; local_y < 4; local_y++) {
				for (uint local_x = 0; local_x < 4; local_x++) {
					uint x = std::min(block_x * 4u + local_x, tex_width - 1u);
					uint y = std::min(block_y * 4u + local_y, tex_height - 1u);

					rtm::uvec2 iuv(x, y);
					uint block_index = local_y * 4u + local_x;

					texels[block_index] = Texture2D::decode_texel(*texture.get_texel_addr(iuv));
				}
			}
			output.blocks[block_y * output.blocks_x + block_x] = encode_bc1_block(texels);
		}
	}

	return output;
}

rtm::vec4 decode_bc1_texture_texel(const BC1Texture& bc1_texture, uint32_t x, uint32_t y) {
	_assert(x < bc1_texture.width);
	_assert(y < bc1_texture.height);

	uint32_t block_x = x / 4;
	uint32_t block_y = y / 4;

	uint32_t block_index = block_y * bc1_texture.blocks_x + block_x;

	uint32_t local_x = x % 4;
	uint32_t local_y = y % 4;

	uint32_t local_texel_index = local_y * 4 + local_x;

	return decode_bc1_texel(bc1_texture.blocks[block_index], local_texel_index);
}

void test_bc1_texture(const Texture2D& texture) {
	BC1Texture bc1_texture = encode_bc1_texture(texture);

	uint32_t width = static_cast<uint32_t>(texture.width);
	uint32_t height = static_cast<uint32_t>(texture.height);

	size_t raw_bytes = static_cast<size_t>(width) * height * sizeof(Texture2D::Texel);

	size_t bc1_bytes = bc1_texture.blocks.size() * sizeof(BC1Block);

	double total_squared_error = 0.0;

	std::vector<uint8_t> reference_pixels(texture.width * texture.height * 4);
	std::vector<uint8_t> bc1_preview_pixels(texture.width * texture.height * 4);
	//preview.resize(static_cast<size_t>(width) * height * 4);

	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			uint32_t index = y * width + x;
			uint32_t flipped_y = texture.height - 1 - y;
			uint32_t output_index = flipped_y * width + x;
			rtm::uvec2 iuv(x, y);

			rtm::vec4 original = Texture2D::decode_texel(*texture.get_texel_addr(iuv));

			rtm::vec4 decoded = decode_bc1_texture_texel(bc1_texture, x, y);

			float dr = original.x - decoded.x;
			float dg = original.y - decoded.y;
			float db = original.z - decoded.z;

			total_squared_error += dr * dr + dg * dg + db * db;

			auto to_byte = [](float value) -> uint8_t {
				return static_cast<uint8_t>(
					std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
				};

			reference_pixels[output_index * 4 + 0] = to_byte(original.x);
			reference_pixels[output_index * 4 + 1] = to_byte(original.y);
			reference_pixels[output_index * 4 + 2] = to_byte(original.z);
			reference_pixels[output_index * 4 + 3] = to_byte(original.w);

			bc1_preview_pixels[output_index * 4 + 0] = to_byte(decoded.x);
			bc1_preview_pixels[output_index * 4 + 1] = to_byte(decoded.y);
			bc1_preview_pixels[output_index * 4 + 2] = to_byte(decoded.z);
			bc1_preview_pixels[output_index * 4 + 3] = 255; // expected for now

			

		}
	}

	double mse = total_squared_error / static_cast<double>(width * height);

	double compression_ratio = static_cast<double>(raw_bytes) / bc1_bytes;

	fprintf(stderr,
		"BC1 texture: %u x %u | raw: %zu B | BC1: %zu B | ratio: %.2fx | MSE: %.8f\n",
		width, height, raw_bytes, bc1_bytes, compression_ratio, mse);

	stbi_write_png("bc1_reference_leaf.png", static_cast<int>(width), static_cast<int>(height), 4, reference_pixels.data(), static_cast<int>(width * 4));

	stbi_write_png("bc1_texture_preview_leaf.png", static_cast<int>(width), static_cast<int>(height), 4, bc1_preview_pixels.data(), static_cast<int>(width * 4));

}