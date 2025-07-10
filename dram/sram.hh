#ifndef __SRAM_RISCV__
#define __SRAM_RISCV__

#include <cmath>
#include <cstdint>
#include <vector>

namespace SimpleSSD {

namespace SRAM {

struct Line{
	bool valid;
	uint64_t tag;
	Line() {
		valid = false;
		tag = 0;
	}
};

class SRAM{
	public:
		SRAM(int cacheSize, int blockSize, int numWays);
		bool read(uint64_t address);
		bool write(uint64_t address);		
		void setLRU(uint64_t index, uint64_t way);	
		
	private:	
		int cache_size;
		int block_size;
		int num_ways;
		int reads;
		int read_misses;
		int writes;
		int write_misses;
		int num_sets;
		int tag_bits;
		int index_bits;
		int offset_bits;
		
		std::vector<std::vector<Line>> cache_lines;
		std::vector<std::vector<int>> LRUData;
};

}

}

#endif