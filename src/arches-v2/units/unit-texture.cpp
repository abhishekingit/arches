#include "unit-texture.hpp"

namespace Arches { namespace Units {

void UnitTexture::clock_rise()
{
	_request_network.clock();

	if(_filter_pipline.is_write_valid() && cache->return_port_read_valid(cache_port))
	{
		MemoryReturn ret = cache->read_return(cache_port);
		auto sample_it = _pending_samples.find(ret.dst.raw);
		if(sample_it == _pending_samples.end() || ret.size != 32)
		{
			fprintf(stderr, "Error: texture cache return has no pending sample or an invalid size.\n");
			exit(EXIT_FAILURE);
		}
		Sample& sample = sample_it->second;
		for(uint32_t i = 0; i < sample.num_texels; ++i)
		{
			const uint32_t texel_bit = 1u << i;
			if(!(sample.pending_texel_mask & texel_bit)) continue;
			uint32_t block_offset = sample.texel_addrs[i] % 32;
			paddr_t block_addr = sample.texel_addrs[i] - block_offset;
			if(ret.paddr != block_addr) continue;

			// One region return can satisfy several texels, including the same block.
			if(sample.format == TextureFormat::BC1)
				// Reconstruct the eight-byte block without alignment-dependent buffer casts.
				std::memcpy(&sample.bc1_blocks[i], ret.data + block_offset, sizeof(BC1Block));
			else
				std::memcpy(&sample.texels[i], ret.data + block_offset, sizeof(Texture2D::Texel));
			sample.pending_texel_mask &= ~texel_bit;
		}
		
		if(sample.pending_texel_mask == 0)
		{
			_filter_pipline.write(sample);
			_pending_samples.erase(sample_it);
		}
	}

	if(_request_network.is_read_valid(0))
	{
		MemoryRequest req = _request_network.read(0);
		req.dst.push(req.port, 8);

		auto inserted = _pending_samples.emplace(req.dst.raw, Sample{});
		if(!inserted.second)
		{
			fprintf(stderr, "Error: texture samples have duplicate in-flight destination tags.\n");
			exit(EXIT_FAILURE);
		}
		Sample& sample = inserted.first->second;
		sample.req = req;

		rtm::vec2 uv;
		std::memcpy(&uv, req.data, sizeof(rtm::vec2));
		const Texture2DMetadata& metadata = *(const Texture2DMetadata*)&_cheat_mem[req.vaddr];
		sample.format = metadata.format;
		require_supported_filter(metadata.format, filter_mode);
		if(metadata.format == TextureFormat::BC1 &&
			(!metadata.texels || metadata.width <= 0 || metadata.height <= 0 ||
			reinterpret_cast<paddr_t>(metadata.texels) % sizeof(BC1Block) != 0))
		{
			fprintf(stderr, "Error: invalid BC1 texture dimensions or block-data alignment.\n");
			exit(EXIT_FAILURE);
		}
		Texture2D texture(metadata);
		auto set_texel_address = [&](uint32_t index, const rtm::uvec2& iuv)
		{
			if(sample.format == TextureFormat::RGBA8)
			{
				sample.texel_addrs[index] = (paddr_t)texture.get_texel_addr(iuv);
				return;
			}
			// Match the existing coordinate wrapping before finding the row-major block.
			const uint32_t x = iuv[0] % texture.width;
			const uint32_t y = iuv[1] % texture.height;
			const uint32_t blocks_x = (static_cast<uint32_t>(texture.width) + 3u) / 4u;
			const paddr_t block_index = static_cast<paddr_t>(y / 4u) * blocks_x + x / 4u;
			sample.texel_addrs[index] = reinterpret_cast<paddr_t>(metadata.texels) + block_index * sizeof(BC1Block);
			sample.bc1_texel_indices[index] = static_cast<uint8_t>((y % 4u) * 4u + x % 4u);
		};

		if(filter_mode == FilterMode::NEAREST)
		{
			rtm::uvec2 iuv = texture.get_iuv(uv, rtm::vec2(0.5f));
			set_texel_address(0, iuv);
			sample.num_texels = 1;
		}
		else if(filter_mode == FilterMode::LINEAR)
		{
			sample.fract_uv = texture.get_fract_uv(uv);

			//             y  x
			rtm::uvec2 iuv[2][2];
			iuv[0][0] = texture.get_iuv(uv, rtm::vec2(0.0f, 0.0f));
			iuv[0][1] = texture.get_iuv(uv, rtm::vec2(1.0f, 0.0f));
			iuv[1][0] = texture.get_iuv(uv, rtm::vec2(0.0f, 1.0f));
			iuv[1][1] = texture.get_iuv(uv, rtm::vec2(1.0f, 1.0f));

			set_texel_address(0, iuv[0][0]);
			set_texel_address(1, iuv[0][1]);
			set_texel_address(2, iuv[1][0]);
			set_texel_address(3, iuv[1][1]);
			sample.num_texels = 4;
		}
		else _assert(false);

		sample.pending_texel_mask = (1u << sample.num_texels) - 1u;
		uint32_t block_cnt = 0;
		paddr_t blocks[4];
		for(uint32_t i = 0; i < sample.num_texels; ++i)
		{
			uint32_t block_offset = sample.texel_addrs[i] % 32;
			paddr_t block_addr = sample.texel_addrs[i] - block_offset;

			MemoryRequest texel_req;
			log.texels++;

			for(uint32_t j = 0; j < block_cnt; ++j)
				if(blocks[j] == block_addr)
					goto BREAK_CONTINUE;

			texel_req.type = MemoryRequest::Type::LOAD;
			texel_req.paddr = block_addr;
			texel_req.size = 32;
			texel_req.port = cache_port;
			texel_req.dst = req.dst;
			_texel_fill_queue.push(texel_req);
			log.loads++;

			blocks[block_cnt++] = block_addr;
		BREAK_CONTINUE:;
		}
	}

}

void UnitTexture::clock_fall()
{
	_filter_pipline.clock();

	if(_filter_pipline.is_read_valid() && _return_network.is_write_valid(0))
	{
		Sample sample = _filter_pipline.read();
		require_supported_filter(sample.format, filter_mode);

		MemoryReturn ret2(sample.req);
		ret2.size = sizeof(rtm::vec4);
		ret2.dst.pop(8);

		if(filter_mode == FilterMode::NEAREST)
		{
			rtm::vec4 color = sample.format == TextureFormat::BC1
				? decode_bc1_texel(sample.bc1_blocks[0], sample.bc1_texel_indices[0])
				: Texture2D::decode_texel(sample.texels[0]);
			std::memcpy(ret2.data, &color, sizeof(rtm::vec4));
		}
		else
		{
			rtm::vec4 c[2][2];
			c[0][0] = Texture2D::decode_texel(sample.texels[0]);
			c[0][1] = Texture2D::decode_texel(sample.texels[1]);
			c[1][0] = Texture2D::decode_texel(sample.texels[2]);
			c[1][1] = Texture2D::decode_texel(sample.texels[3]);

			c[0][0] = rtm::mix(c[0][0], c[0][1], sample.fract_uv[0]);
			c[1][0] = rtm::mix(c[1][0], c[1][1], sample.fract_uv[0]);
			c[0][0] = rtm::mix(c[0][0], c[1][0], sample.fract_uv[1]);
			//c[0][0] = rtm::vec4(sample.fract_uv[0], sample.fract_uv[1], 0.0, 1.0f);

			std::memcpy(ret2.data, &c[0][0], sizeof(rtm::vec4));
		}

		_return_network.write(ret2, 0);
	}

	if(_texel_fill_queue.size() && cache->request_port_write_valid(cache_port))
	{
		cache->write_request(_texel_fill_queue.front());
		_texel_fill_queue.pop();
	}

	_return_network.clock();
}

}
}
