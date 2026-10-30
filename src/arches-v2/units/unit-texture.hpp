#pragma once 
#include "stdafx.hpp"

#include "unit-memory-base.hpp"
#include "bc1.hpp"

#include "util/arbitration.hpp"

namespace Arches {
namespace Units {

class UnitTexture : public UnitMemoryBase
{
public:
	struct Configuration
	{
		uint            num_clients{1};
		uint8_t*        cheat_mem{nullptr};
		UnitMemoryBase* cache{nullptr};
		uint            cache_port{0};
		TextureFormat   texture_format{TextureFormat::RGBA8};
	};

private:
	UnitMemoryBase* cache;
	uint cache_port;

	Cascade<MemoryRequest, RoundRobinArbiter<uint128_t>> _request_network;
	ReturnCascade _return_network;

	uint32_t filter_mode{1};

	uint8_t* _cheat_mem;

	struct Sample
	{
		rtm::vec2 fract_uv;
		TextureFormat format{TextureFormat::RGBA8};
		// RGBA8 texel addresses or BC1 block addresses, one per sampled texel.
		paddr_t texel_addrs[8];
		Texture2D::Texel texels[8];
		BC1Block bc1_blocks[8];
		uint8_t bc1_texel_indices[8];
		uint32_t num_texels{0};
		// A dependency is cleared only once, even when addresses are duplicated.
		uint32_t pending_texel_mask{0};
		MemoryRequest req;
	};

	std::unordered_map<uint64_t, Sample> _pending_samples;
	std::queue<MemoryRequest> _texel_fill_queue;
	LatencyFIFO<Sample> _filter_pipline;

public:
	UnitTexture(const Configuration& config) : UnitMemoryBase(), cache(config.cache), cache_port(config.cache_port), _cheat_mem(config.cheat_mem),
		_filter_pipline(10), _request_network(config.num_clients, 1), _return_network(1, config.num_clients)
	{
		// BC1 fetches may be staged; decoding remains guarded in clock_fall().
		if(config.texture_format != TextureFormat::BC1)
			require_rgba8_texture_format(config.texture_format);
	}

	void clock_rise() override;
	void clock_fall() override;

	bool request_port_write_valid(uint port_index) override
	{
		return _request_network.is_write_valid(port_index);
	}

	void write_request(const MemoryRequest& request) override
	{
		_request_network.write(request, request.port);
	}

	bool return_port_read_valid(uint port_index) override
	{
		return _return_network.is_read_valid(port_index);
	}

	const MemoryReturn& peek_return(uint port_index) override
	{
		return _return_network.peek(port_index);
	}

	const MemoryReturn read_return(uint port_index) override
	{
		return _return_network.read(port_index);
	}

	class Log
	{
		const static uint NUM_COUNTERS = 4;

	public:
		union
		{
			struct
			{
				uint64_t texels;
				uint64_t loads;
			};
			uint64_t counters[NUM_COUNTERS];
		};

	public:
		Log() { reset(); }

		void reset()
		{
			for(uint i = 0; i < NUM_COUNTERS; ++i)
				counters[i] = 0;
		}

		void accumulate(const Log& other)
		{
			for(uint i = 0; i < NUM_COUNTERS; ++i)
				counters[i] += other.counters[i];
		}

		void print(cycles_t cycles, uint units = 1)
		{
			printf("Texels: %lld\n", texels / units);
			printf("Loads: %lld\n", loads / units);
		}
	}
	log;
};

}
}
