#include "unit-texture.hpp"

namespace Arches { namespace Units {

void UnitTexture::clock_rise()
{
	_request_network.clock();

	if(_filter_pipline.is_write_valid() && cache->return_port_read_valid(cache_port))
	{
		MemoryReturn ret = cache->read_return(cache_port);
		Sample& sample = _pending_samples[ret.dst.raw];

		if(filter_mode == 0) //nearest
		{
			uint32_t block_offset = sample.texel_addrs[0] % 32;
			uint32_t block_addr = sample.texel_addrs[0] - block_offset;

			_assert(block_addr == ret.paddr);
			std::memcpy(&sample.texels[0], ret.data + block_offset, sizeof(Texture2D::Texel));
			sample.pending_texels--;
		}
		else
		{
			for(uint32_t i = 0; i < 4; ++i)
			{
				uint32_t block_offset = sample.texel_addrs[i] % 32;
				uint32_t block_addr = sample.texel_addrs[i] - block_offset;
				if(ret.paddr == block_addr)
				{
					std::memcpy(&sample.texels[i], ret.data + block_offset, sizeof(Texture2D::Texel));
					sample.pending_texels--;
				}
			}
		}
		
		if(sample.pending_texels == 0)
			_filter_pipline.write(sample);
	}

	if(_request_network.is_read_valid(0))
	{
		MemoryRequest req = _request_network.read(0);
		req.dst.push(req.port, 8);

		Sample& sample = _pending_samples[req.dst.raw];
		sample.req = req;

		rtm::vec2 uv;
		std::memcpy(&uv, req.data, sizeof(rtm::vec2));
		const Texture2DMetadata& metadata = *(const Texture2DMetadata*)&_cheat_mem[req.vaddr];
		require_rgba8_texture_format(metadata.format);
		Texture2D texture(metadata);

		if(filter_mode == 0) //nearest
		{
			rtm::uvec2 iuv = texture.get_iuv(uv, rtm::vec2(0.5f));
			sample.texel_addrs[0] = (paddr_t)texture.get_texel_addr(iuv);
			sample.pending_texels = 1;
		}
		else if(filter_mode == 1) //linear
		{
			sample.fract_uv = texture.get_fract_uv(uv);

			//             y  x
			rtm::uvec2 iuv[2][2];
			iuv[0][0] = texture.get_iuv(uv, rtm::vec2(0.0f, 0.0f));
			iuv[0][1] = texture.get_iuv(uv, rtm::vec2(1.0f, 0.0f));
			iuv[1][0] = texture.get_iuv(uv, rtm::vec2(0.0f, 1.0f));
			iuv[1][1] = texture.get_iuv(uv, rtm::vec2(1.0f, 1.0f));

			sample.texel_addrs[0] = (paddr_t)texture.get_texel_addr(iuv[0][0]);
			sample.texel_addrs[1] = (paddr_t)texture.get_texel_addr(iuv[0][1]);
			sample.texel_addrs[2] = (paddr_t)texture.get_texel_addr(iuv[1][0]);
			sample.texel_addrs[3] = (paddr_t)texture.get_texel_addr(iuv[1][1]);
			sample.pending_texels = 4;
		}
		else _assert(false);

		uint32_t block_cnt = 0;
		paddr_t blocks[4];
		for(uint32_t i = 0; i < sample.pending_texels; ++i)
		{
			uint32_t block_offset = sample.texel_addrs[i] % 32;
			uint32_t block_addr = sample.texel_addrs[i] - block_offset;

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

		MemoryReturn ret2(sample.req);
		ret2.size = sizeof(rtm::vec4);
		ret2.dst.pop(8);

		if(filter_mode == 0)
		{
			rtm::vec4 color = Texture2D::decode_texel(sample.texels[0]);
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
