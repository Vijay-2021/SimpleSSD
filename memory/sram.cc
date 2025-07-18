#include "sram.hh"

static const int addr_width = 64; // use uint64_t addresses for RISCV

namespace SimpleSSD {

namespace Memory {

SRAM::SRAM(int cacheSize, int blockSize, int num_ways) : cache_size(cacheSize), block_size(blockSize), num_ways(num_ways), reads(0), read_misses(0), writes(0), write_misses(0) {

	num_sets = cache_size / (block_size * num_ways);

	index_bits = std::log2(num_sets);
	offset_bits = std::log2(block_size);
	tag_bits = addr_width - index_bits - offset_bits;
	
	for(int i = 0; i < num_sets; i++){
		cache_lines.push_back(std::vector<Line>(num_ways));
	}
	for(int i = 0; i < num_sets; i++){
		LRUData.push_back(std::vector<int>(num_ways));
	}			
	
	for(int i = 0; i < num_sets; i++){
		for(int j = 0; j < num_ways; j++){
			LRUData[i][j] = j;
		}
	}
}

void SRAM::setLRU(uint64_t index, uint64_t way){
	int temp = -1;
	for(int i = 0; i < num_ways; i++){
		if(LRUData[index][i] == way){
			temp = i;
			break;
		}
	}
	for(int i = temp; i > 0; i--){
		LRUData[index][i] = LRUData[index][i-1];
	}
	LRUData[index][0] = way;
	return;
	
}

bool SRAM::read(uint64_t address){
	reads++;
    uint64_t tag = address >> (offset_bits + index_bits);
    uint64_t index = 0;
    if(num_sets > 1){	       
        index = (address << tag_bits) >> (tag_bits + offset_bits);
    }
    for(int i = 0; i < num_ways; i++){
        if(cache_lines[index][i].valid == true && cache_lines[index][i].tag == tag){
            setLRU(index, i);
            return true;
        }
    }
	read_misses++;
	for(int i = 0; i < num_ways; i++){
		if(cache_lines[index][i].valid == false){
			cache_lines[index][i].valid = true;
			cache_lines[index][i].tag = tag; 
			setLRU(index, i);
			return false;
		}
	}	

	int evictColumn = LRUData[index][num_ways-1]; // theoretically should write eviction back to DRAM(not modelled for now)
	cache_lines[index][evictColumn].tag=tag;
	cache_lines[index][evictColumn].valid=true;
	setLRU(index, evictColumn);
	return false; 
}

bool SRAM::write(uint64_t address){
	writes++;
	uint64_t tag = address >> (offset_bits + index_bits);
    uint64_t index = 0;
    if(num_sets > 1){
        index = (address << tag_bits) >> (tag_bits + offset_bits);
    }
	for(int i = 0 ; i < num_ways; i++){
		if(cache_lines[index][i].tag==tag){
			setLRU(index, i);
			cache_lines[index][i].valid = true; // we can write regardless of valid bit
			return true;
		}

	}
	write_misses++;

	for(int i = 0; i < num_ways; i++){

		if(cache_lines[index][i].valid == false){

			cache_lines[index][i].tag = tag;
			cache_lines[index][i].valid = true;
			setLRU(index, i);
			return false;	
		}

	}
	int evictColumn = LRUData[index][num_ways-1]; // again ignore writeback to dram for now
	cache_lines[index][evictColumn].tag = tag;
	cache_lines[index][evictColumn].valid = true;
	setLRU(index, evictColumn);
	return false;	
}

} 

}