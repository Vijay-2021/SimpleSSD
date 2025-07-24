/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "partitioned_icl.hh"
#include "random.h"
#include "memory.h"
#include "def.hh"
#include "cs_instructions.h"
#include "new.hh"
#include "firmware_utils.h"
namespace ICL {


PartitionedICL::PartitionedICL(icl_params& cparams, FTL::FTL *ftl) :
      AbstractICL(cparams, ftl) {
    printf("calling partitioned icl initializer!\n");
    cache_mutex = (Mutex *)malloc(sizeof(Mutex) * waySize);
    for (int i = 0; i < waySize; i++) {
        mutex_init(&cache_mutex[i]); // we take DeepFlash setup(give each core its own way)
    }
}

bool PartitionedICL::check_specific_way(uint32_t set, uint32_t way, uint32_t lca) {
    Line &line = cacheData[set][way];
    if (line.valid && line.tag == lca) {
        return true;
    }
    return false;
}

// True when hit
bool PartitionedICL::read(Request &req) {
    bool ret = false;
    uint64_t start_cycle;
    uint64_t end_cycle;
    uint64_t read_req_cycles;
    bool cache_hit = false;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    // debugprint(LOG_ICL_GENERIC_CACHE,
    //           "READ  | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
    //           req.reqID, req.reqSubID, req.range.slpn, req.length);
    
    if (useReadCaching && req.length < lineSize) {
        uint32_t setIdx = calcSetIndex(req.range.slpn);
        uint32_t wayIdx = req.range.slpn % waySize; // simple way selection
        mutex_lock(&cache_mutex[wayIdx]);
        if (check_specific_way(setIdx, wayIdx, req.range.slpn)) {
            cache_hit = true;
            mutex_unlock(&cache_mutex[wayIdx]); // assume that there aren't race conditions, and we can read freely(not accurate but used for our simple model to gain some basic parallelism)
            dram_read((uint64_t)(cache_buffer + (setIdx * waySize + wayIdx) * lineSize), req.length);
            ret = true;
            uint64_t tick;
            read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
            process_request(tick);
        } else {
            FTL::Request reqInternal(lineCountInSuperPage, req);
            uint64_t dramAt;
            Line *pLine = &cacheData[setIdx][wayIdx];
            if (pLine->valid && pLine->dirty) { // if we have valid dirty data and its not a hit, we need to evict it(in direct mapped setup)
                reqInternal.ioFlag.reset();
                uint32_t row, col;  // Variable for I/O position (IOFlag)
                calcIOPosition(req.range.slpn, row, col);
                reqInternal.ioFlag.set(row);
                read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
                read_req_cycles += end_cycle - start_cycle;
                pFTL->write(reqInternal); // write dirty data to NVM
                read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes(eviction can cause FTL writes so we skip)
                mutex_lock(&stat_mutex); // we technically will accquire two locks here, but there should be no circular wait
                icl_stats.cache_evictions++; // only count evictions if we write data to FTL
                mutex_unlock(&stat_mutex);
            }
            
            // Read data
            reqInternal.ioFlag.reset();
            reqInternal.ioFlag.set(req.range.slpn % lineCountInSuperPage);

            read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
            read_req_cycles += end_cycle - start_cycle; // ignore the ftl time
            uint64_t read_time = pFTL->read(reqInternal);
            read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
        
            dram_write((uint64_t)copy_buffer, lineSize);
            // Set cache data

            pLine->insertedAt = read_time;
            pLine->lastAccessed = read_time;
            pLine->tag = req.range.slpn;
            pLine->valid = true;
            pLine->dirty = false;

            mutex_unlock(&cache_mutex[wayIdx]);
            uint64_t tick;
            read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
            process_request(tick);
        }
    } else {
        uint64_t tick;
        read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
        for (size_t lpn = req.range.slpn; lpn < req.range.slpn + req.range.nlp; lpn++) {
            mutex_lock(&cache_mutex[lpn % waySize]);
            FTL::Request reqInternal(lineCountInSuperPage, req);
            //simulates writing data to dram after retrieving it from NVM, however we do in opposite order for timing correctness
            dram_write((uint64_t)copy_buffer, req.length);
            read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
            read_req_cycles += end_cycle - start_cycle;
            tick = MAX(tick, pFTL->read(reqInternal));
            read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
            mutex_unlock(&cache_mutex[lpn % waySize]);
        }
        process_request(tick);
    }

    read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
    read_req_cycles += end_cycle - start_cycle;
    mutex_lock(&stat_mutex);
    if (cache_hit) {
        icl_stats.read_cache_hits++;
    } else {
        icl_stats.read_cache_misses++;
    }
    icl_stats.read_req_cycles += read_req_cycles;
    icl_stats.read_requests++;
    icl_stats.read_bytes += req.length;
    icl_stats.heap_top = get_heap_top();
    mutex_unlock(&stat_mutex);
    return ret;
}

// True when cold-miss/hit
bool PartitionedICL::write(Request &req) {
    uint64_t tick;
    uint64_t start_cycle;
    uint64_t end_cycle;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    bool ret = false;
    bool dirty = false;
    uint64_t write_req_cycles = 0;
    bool cache_hit = false;
    read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
    uint64_t flash = tick;
    FTL::Request reqInternal(lineCountInSuperPage, req);

    if (req.length < lineSize) {
        dirty = true; // if our request is smaller than line size, we will write it to cache and mark it dirty
    }
    else {
        read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
        write_req_cycles += end_cycle - start_cycle;
        mutex_lock(&cache_mutex[req.range.slpn % waySize]);
        flash = pFTL->write(reqInternal); // write to NVM first, then write to cache
        mutex_unlock(&cache_mutex[req.range.slpn % waySize]);
        read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
        process_request(flash);
    }

    if (useWriteCaching) {
        mutex_lock(&cache_mutex[req.range.slpn % waySize]);
        uint32_t setIdx = calcSetIndex(req.range.slpn);
        uint32_t wayIdx = req.range.slpn % waySize;

        // Can we update old data?
        if (check_specific_way(setIdx, wayIdx, req.range.slpn)) {
            cache_hit = true;
            uint64_t arrived = tick;

            // TODO: TEMPORAL CODE
            // We should only show DRAM latency when cache become dirty
            if (dirty) {
                // Update last accessed time
                cacheData[setIdx][wayIdx].insertedAt = tick;
                cacheData[setIdx][wayIdx].lastAccessed = tick;
            }
            else {
                cacheData[setIdx][wayIdx].insertedAt = flash;
                cacheData[setIdx][wayIdx].lastAccessed = flash;
            }

            // Update last accessed time
            cacheData[setIdx][wayIdx].dirty = dirty; 

            dram_write((uint64_t)copy_buffer, req.length);

            // debugprint(LOG_ICL_GENERIC_CACHE,
            //           "WRITE | Cache hit at (%u, %u) | %" PRIu64 " - %" PRIu64
            //           " (%" PRIu64 ")",
            //           setIdx, wayIdx, arrived, tick, tick - arrived);

            ret = true;
            if (dirty) {
                read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
                process_request(tick);
            }
        } else {
            uint64_t arrived = tick;

            // Do we have place to write data?
            if (check_specific_way(setIdx, wayIdx, req.range.slpn)) {

                if (dirty) {
                    // Update last accessed time
                    read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
                    cacheData[setIdx][wayIdx].insertedAt = tick;
                    cacheData[setIdx][wayIdx].lastAccessed = tick;
                }
                else {
                    cacheData[setIdx][wayIdx].insertedAt = flash;
                    cacheData[setIdx][wayIdx].lastAccessed = flash;
                }

                // Update last accessed time
                cacheData[setIdx][wayIdx].valid = true;
                cacheData[setIdx][wayIdx].dirty = dirty;
                cacheData[setIdx][wayIdx].tag = req.range.slpn;

                // DRAM access
                dram_write((uint64_t)copy_buffer, req.length);
                if (dirty) {
                    uint64_t tick;
                    read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
                    process_request(tick);
                }
                ret = true;
            }
            // We have to flush
            else {
                Line *pLine = &cacheData[setIdx][wayIdx];
                if (pLine->valid && pLine->dirty) { // if we have valid dirty data and its not a hit, we need to evict it(in direct mapped setup)
                    uint32_t row, col;  // Variable for I/O position (IOFlag)
                    calcIOPosition(req.range.slpn, row, col);
                    reqInternal.ioFlag.reset();
                    reqInternal.ioFlag.set(row);
                    read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
                    write_req_cycles += end_cycle - start_cycle;
                    pFTL->write(reqInternal); // update with ftl return time
                    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes(eviction can cause FTL writes so we skip)
                    mutex_lock(&stat_mutex); // we technically will accquire two locks here, but there should be no circular wait
                    icl_stats.cache_evictions++; // only count evictions if we write data to FTL
                    mutex_unlock(&stat_mutex);
                }

                uint64_t tick;
                read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
                cacheData[setIdx][wayIdx].insertedAt = tick;
                cacheData[setIdx][wayIdx].lastAccessed = tick;
                cacheData[setIdx][wayIdx].valid = true;
                cacheData[setIdx][wayIdx].dirty = true;
                cacheData[setIdx][wayIdx].tag = req.range.slpn;

                // DRAM latency
                dram_write((uint64_t)copy_buffer, req.length);
                // Update cache data
                
                if (dirty) {
                    read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
                    process_request(tick);
                }

            }
        }
        mutex_unlock(&cache_mutex[req.range.slpn % waySize]);

    } else {
        mutex_lock(&cache_mutex[req.range.slpn % waySize]);
        if (dirty) {
            read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
            write_req_cycles += end_cycle - start_cycle;
            process_request(pFTL->write(reqInternal));
            read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); 
        }
        // TEMP: Disable DRAM calculation for prevent conflict
        dram_read((uint64_t)copy_buffer, req.length);
        mutex_unlock(&cache_mutex[req.range.slpn % waySize]);
    }
  
    read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
    write_req_cycles += end_cycle - start_cycle;
    mutex_lock(&stat_mutex);
    if (cache_hit) {
        icl_stats.write_cache_hits++;
    } else {
        icl_stats.write_cache_misses++;
    }
    icl_stats.write_req_cycles += write_req_cycles;
    icl_stats.write_requests++;
    icl_stats.write_bytes += req.length;
    icl_stats.heap_top = get_heap_top();
    mutex_unlock(&stat_mutex);
    return ret;
}

// True when flushed
void PartitionedICL::flush(LPNRange &range) {
    uint64_t flush_req_cycles = 0;
    uint64_t start_cycle;
    uint64_t end_cycle;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    if (useReadCaching || useWriteCaching) {
        uint64_t finishedAt;
        read_buffer((uint64_t)&finishedAt, 0, FIRMWARE_TICK);
        FTL::Request reqInternal(lineCountInSuperPage);
        read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes(eviction can cause FTL writes so we skip)
        if (range.nlp < setSize * waySize) {
            for (uint64_t lpn = range.slpn; lpn < range.slpn + range.nlp; lpn++) {
                mutex_lock(&cache_mutex[lpn % waySize]);
                uint32_t setIdx = calcSetIndex(lpn);
                uint32_t wayIdx = getValidWay(lpn);

                Line &line = cacheData[setIdx][wayIdx];
                if (wayIdx != waySize && line.dirty) {
                    reqInternal.lpn = line.tag / lineCountInSuperPage;
                    reqInternal.ioFlag.set(line.tag % lineCountInSuperPage);
                    read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
                    flush_req_cycles += end_cycle - start_cycle;
                    uint64_t ftlTick = pFTL->write(reqInternal);
                    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
                    finishedAt = MAX(finishedAt, ftlTick);
                }
                line.valid = false;
                mutex_unlock(&cache_mutex[lpn % waySize]);
            }
        } else {
            for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
                mutex_lock(&cache_mutex[wayIdx]); // cache space is partitioned by way, so we can lock only one way at a time
                for (uint32_t setIdx = 0; setIdx < setSize; setIdx++) {
                    Line &line = cacheData[setIdx][wayIdx];

                    if (line.tag >= range.slpn && line.tag < range.slpn + range.nlp) {
                        
                        if (line.dirty) {
                            reqInternal.lpn = line.tag / lineCountInSuperPage;
                            reqInternal.ioFlag.set(line.tag % lineCountInSuperPage);
                            read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
                            flush_req_cycles += end_cycle - start_cycle;
                            uint64_t ftlTick = pFTL->write(reqInternal);
                            read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
                            finishedAt = MAX(finishedAt, ftlTick);
                        }
                        line.valid = false;
                    }
                }
                mutex_unlock(&cache_mutex[wayIdx]); // unlock way mutex
            }
        }
        process_request(finishedAt);
    } else {
        process_request_failed();
    }
    
    read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
    flush_req_cycles += end_cycle - start_cycle;
    mutex_lock(&stat_mutex);
    icl_stats.flush_req_cycles += flush_req_cycles;
    icl_stats.flush_requests++;
    icl_stats.flush_bytes += range.nlp * lineCountInSuperPage * lineSize;
    icl_stats.heap_top = get_heap_top();
    mutex_unlock(&stat_mutex);
}

// True when hit
void PartitionedICL::trim(LPNRange &range) {
  
    uint64_t start_cycle;
    uint64_t end_cycle;
    uint64_t trim_req_cycles = 0;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    if (useReadCaching || useWriteCaching) {
        uint64_t finishedAt;
        read_buffer((uint64_t)&finishedAt, 0, FIRMWARE_TICK);
        FTL::Request reqInternal(lineCountInSuperPage);
        mutex_lock(&cache_mutex[range.slpn % waySize]);
        for (uint32_t setIdx = 0; setIdx < setSize; setIdx++) {
            for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
                Line &line = cacheData[setIdx][wayIdx];

                if (line.tag >= range.slpn && line.tag < range.slpn + range.nlp) {
                    reqInternal.lpn = line.tag / lineCountInSuperPage;
                    reqInternal.ioFlag.set(line.tag % lineCountInSuperPage);
                    read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
                    trim_req_cycles += end_cycle - start_cycle;
                    uint64_t trim_tick = pFTL->trim(reqInternal);
                    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
                    finishedAt = MAX(finishedAt, trim_tick);
                    line.valid = false;
                }
            }
        }
        mutex_unlock(&cache_mutex[range.slpn % waySize]);
        process_request(finishedAt);
    } else {
        process_request_failed();
    }
    read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
    mutex_lock(&stat_mutex);
    icl_stats.trim_req_cycles += end_cycle - start_cycle;
    icl_stats.trim_requests++;
    icl_stats.trim_bytes += range.nlp * lineCountInSuperPage * lineSize;
    icl_stats.heap_top = get_heap_top();
    mutex_unlock(&stat_mutex);

}

void PartitionedICL::format(LPNRange &range) {
    uint64_t start_cycle;
    uint64_t end_cycle;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    mutex_lock(&cache_mutex[range.slpn % waySize]);
    if (useReadCaching || useWriteCaching) {
        uint64_t lpn;
        uint32_t setIdx;
        uint32_t wayIdx;

        for (uint64_t i = 0; i < range.nlp; i++) {
        lpn = range.slpn + i;
        setIdx = calcSetIndex(lpn);
        wayIdx = getValidWay(lpn);
        if (wayIdx != waySize) {
            // Invalidate
            cacheData[setIdx][wayIdx].valid = false;
        }
        }
    }

    // Convert unit
    range.slpn /= lineCountInSuperPage;
    range.nlp = (range.nlp - 1) / lineCountInSuperPage + 1;
    read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
    process_request(pFTL->format(range)); // adds one cycle to ICL time, real processing time captured in FTL
    mutex_unlock(&cache_mutex[range.slpn % waySize]);
    mutex_lock(&stat_mutex);
    icl_stats.format_req_cycles += end_cycle - start_cycle;
    icl_stats.format_requests++;
    icl_stats.format_bytes += range.nlp * lineCountInSuperPage * lineSize;
    icl_stats.heap_top = get_heap_top();
    mutex_unlock(&stat_mutex);
  
}

}