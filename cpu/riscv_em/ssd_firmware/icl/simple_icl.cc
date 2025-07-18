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


#include "simple_icl.hh"
#include "random.h"
#include "memory.h"
#include "def.hh"
#include "cs_instructions.h"
#include "new.hh"
#include "firmware_utils.h"
namespace ICL {

SimpleICL::SimpleICL(icl_params& cparams, FTL::FTL *ftl) :
      AbstractICL(cparams, ftl) {
  printf("calling simple locked icl initializer!\n");
  mutex_init(&cache_mutex);
}
  

void SimpleICL::evictCache(bool flush) {
  FTL::Request reqInternal(lineCountInSuperPage);
  uint64_t tick;
  for (uint32_t row = 0; row < lineCountInSuperPage; row++) {
    read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
    for (uint32_t col = 0; col < parallelIO; col++) {
      uint64_t beginAt = tick; // all parallel requests start at the same time

      if (evictData[row][col] == nullptr) {
        continue;
      }

      if (evictData[row][col]->valid && evictData[row][col]->dirty) {
        reqInternal.lpn = evictData[row][col]->tag / lineCountInSuperPage;
        reqInternal.ioFlag.reset();
        reqInternal.ioFlag.set(row);
        beginAt = pFTL->write(reqInternal); // update with ftl return time
        mutex_lock(&stat_mutex); // we technically will accquire two locks here, but there should be no circular wait
        icl_stats.cache_evictions++; // only count evictions if we write data to FTL
        mutex_unlock(&stat_mutex);
      }

      if (flush) {
        evictData[row][col]->valid = false;
        evictData[row][col]->tag = 0;
      }

      evictData[row][col]->insertedAt = beginAt;
      evictData[row][col]->lastAccessed = beginAt;
      evictData[row][col]->dirty = false;
      evictData[row][col] = nullptr;
    }
  }
}

// True when hit
bool SimpleICL::read(Request &req) {
    bool ret = false;
    uint64_t start_cycle;
    uint64_t end_cycle;
    uint64_t read_req_cycles;
    bool cache_hit = false;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    // debugprint(LOG_ICL_GENERIC_CACHE,
    //           "READ  | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
    //           req.reqID, req.reqSubID, req.range.slpn, req.length);

    if (useReadCaching) {
        uint32_t setIdx = calcSetIndex(req.range.slpn);
        uint32_t wayIdx;
        // uint64_t arrived = getTick();
        mutex_lock(&cache_mutex);
        if (useReadPrefetch) {
            checkSequential(req, readDetect);
        }
        wayIdx = getValidWay(req.range.slpn);
        // Do we have valid data?
        if (wayIdx != waySize) {
            cache_hit = true;
            uint64_t tick;
            read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
            cacheData[setIdx][wayIdx].lastAccessed = tick;
            mutex_unlock(&cache_mutex); // assume that there aren't race conditions, and we can read freely(not accurate but used for our simple model to gain some basic parallelism)
            dram_read((uint64_t)(cache_buffer + (setIdx * waySize + wayIdx) * lineSize), req.length);
            ret = true;
            // Do we need to prefetch data?
            if (useReadPrefetch && req.range.slpn == prefetchTrigger) {
                // debugprint(LOG_ICL_GENERIC_CACHE, "READ  | Prefetch triggered");
                req.range.slpn = lastPrefetched;
                mutex_lock(&cache_mutex);
                goto ICL_GENERIC_CACHE_READ;
            } else {
                uint64_t tick;
                read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
                process_request(tick);
            }
        }
        // We should read data from NVM
        else {
            ICL_GENERIC_CACHE_READ:
            FTL::Request reqInternal(lineCountInSuperPage, req);
            Vector<Pair<uint64_t, uint64_t>> readList;
            uint32_t row, col;  // Variable for I/O position (IOFlag)
            uint64_t dramAt;
            uint64_t beginLCA, endLCA;

            if (readDetect.enabled) {
                if (!ret) {
                    // debugprint(LOG_ICL_GENERIC_CACHE, "READ  | Read ahead triggered");
                }

                beginLCA = req.range.slpn;

                // If super-page is disabled, just read all pages from all planes
                    if (prefetchMode == MODE_ALL || !bSuperPage) {
                    endLCA = beginLCA + lineCountInMaxIO;
                    prefetchTrigger = beginLCA + lineCountInMaxIO / 2;
                }
                    else {
                    endLCA = beginLCA + lineCountInSuperPage;
                    prefetchTrigger = beginLCA + lineCountInSuperPage / 2;
                }

                lastPrefetched = endLCA;
            }
            else {
                beginLCA = req.range.slpn;
                endLCA = beginLCA + 1;
            }
            bool need_to_evict = false;
            for (uint64_t lca = beginLCA; lca < endLCA; lca++) {

                // Check cache
                if (getValidWay(lca) != waySize) {
                    continue;
                }

                // Find way to write data read from NVM
                setIdx = calcSetIndex(lca);
                wayIdx = getEmptyWay(setIdx);

                if (wayIdx == waySize) {
                wayIdx = evictFunction(setIdx);

                if (cacheData[setIdx][wayIdx].dirty) {
                    // We need to evict data before write
                    calcIOPosition(cacheData[setIdx][wayIdx].tag, row, col);
                    evictData[row][col] = cacheData[setIdx] + wayIdx;
                    need_to_evict = true;
                }
                }
                uint64_t tick;
                read_buffer((uint64_t)&tick, 0, FIRMWARE_TICK);
                cacheData[setIdx][wayIdx].insertedAt = tick;
                cacheData[setIdx][wayIdx].lastAccessed = tick;
                cacheData[setIdx][wayIdx].valid = true;
                cacheData[setIdx][wayIdx].dirty = false;
                readList.push_back({lca, ((uint64_t)setIdx << 32) | wayIdx});
            }
            if (need_to_evict) {
                read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
                read_req_cycles += end_cycle - start_cycle;
                evictCache();
                read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes(eviction can cause FTL reads so we skip)
            }
            uint64_t beginAt, finishedAt;
            read_buffer((uint64_t)&finishedAt, 0, FIRMWARE_TICK);
            beginAt = finishedAt;
            readList.size();
            for (auto &iter : readList) {
                Line *pLine = &cacheData[iter.second >> 32][iter.second & 0xFFFFFFFF];
                // Read data
                reqInternal.lpn = iter.first / lineCountInSuperPage;
                reqInternal.ioFlag.reset();
                reqInternal.ioFlag.set(iter.first % lineCountInSuperPage);
                read_buffer((uint64_t)&beginAt, 0, FIRMWARE_TICK);  // Ignore cache metadata access

                // If superPageSizeData is true, read first LPN only
                read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
                read_req_cycles += end_cycle - start_cycle; // ignore the ftl time
                uint64_t read_time = pFTL->read(reqInternal);
                read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
            
                dram_write((uint64_t)copy_buffer, lineSize);
                // Set cache data
                beginAt = MAX(beginAt, read_time);

                pLine->insertedAt = beginAt;
                pLine->lastAccessed = beginAt;
                pLine->tag = iter.first;

                if (pLine->tag == req.range.slpn) {
                    finishedAt = beginAt;
                } 

                // debugprint(LOG_ICL_GENERIC_CACHE,
                //           "READ  | Cache miss at (%u, %u) | %" PRIu64 " - %" PRIu64
                //           " (%" PRIu64 ")",
                //           iter.second >> 32, iter.second & 0xFFFFFFFF, tick, beginAt,
                //           beginAt - tick);
            }
            mutex_unlock(&cache_mutex);
            process_request(finishedAt);
        }
    }
    else {
        mutex_lock(&cache_mutex);
        FTL::Request reqInternal(lineCountInSuperPage, req);
        //simulates writing data to dram after retrieving it from NVM, however we do in opposite order for timing correctness
        dram_write((uint64_t)copy_buffer, req.length);
        read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
        read_req_cycles += end_cycle - start_cycle;
        process_request(pFTL->read(reqInternal));
        read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
        mutex_unlock(&cache_mutex);
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
bool SimpleICL::write(Request &req) {
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
        mutex_lock(&cache_mutex);
        flash = pFTL->write(reqInternal); // write to NVM first, then write to cache
        mutex_unlock(&cache_mutex);
        read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes
        process_request(flash);
    }

    if (useWriteCaching) {
        mutex_lock(&cache_mutex);
        uint32_t setIdx = calcSetIndex(req.range.slpn);
        uint32_t wayIdx;

        wayIdx = getValidWay(req.range.slpn);

        // Can we update old data?
        if (wayIdx != waySize) {
            cache_hit = true;
            uint64_t arrived = tick;

            // Wait cache to be valid
            if (tick < cacheData[setIdx][wayIdx].insertedAt) {
                tick = cacheData[setIdx][wayIdx].insertedAt;
            }

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
            wayIdx = getEmptyWay(setIdx);

            // Do we have place to write data?
            if (wayIdx != waySize) {

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
                uint32_t row, col;  // Variable for I/O position (IOFlag)
                uint32_t setToFlush = calcSetIndex(req.range.slpn);

                for (setIdx = 0; setIdx < setSize; setIdx++) {
                    for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
                        if (cacheData[setIdx][wayIdx].valid) {
                            calcIOPosition(cacheData[setIdx][wayIdx].tag, row, col);

                            evictData[row][col] = compareFunction(evictData[row][col],
                                                                    cacheData[setIdx] + wayIdx);
                        }
                    }
                }

                if (evictMode == MODE_SUPERPAGE) {
                    uint32_t row, col;  // Variable for I/O position (IOFlag)

                    for (row = 0; row < lineCountInSuperPage; row++) {
                        for (col = 0; col < parallelIO - 1; col++) {
                            evictData[row][col + 1] =
                                compareFunction(evictData[row][col], evictData[row][col + 1]);
                            evictData[row][col] = nullptr;
                        }
                    }
                }

                // We must flush setToFlush set
                bool have = false;

                for (row = 0; row < lineCountInSuperPage; row++) {
                    for (col = 0; col < parallelIO; col++) {
                        if (evictData[row][col] &&
                            calcSetIndex(evictData[row][col]->tag) == setToFlush) {
                            have = true;
                        }
                    }
                }

                // We don't have setToFlush
                if (!have) {
                    Line *pLineToFlush = nullptr;

                    for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
                        if (cacheData[setToFlush][wayIdx].valid) {
                        pLineToFlush =
                            compareFunction(pLineToFlush, cacheData[setToFlush] + wayIdx);
                        }
                    }

                    if (pLineToFlush) {
                        calcIOPosition(pLineToFlush->tag, row, col);

                        evictData[row][col] = pLineToFlush;
                    }
                }

                read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
                write_req_cycles += end_cycle - start_cycle;
                evictCache(true);
                read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes(eviction can cause FTL writes so we skip)

                // Update cacheline of current request
                setIdx = setToFlush;
                wayIdx = getEmptyWay(setIdx);

                if (wayIdx == waySize) {
                    panic("Cache corrupted!");
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
        mutex_unlock(&cache_mutex);

    }   
    else {
        mutex_lock(&cache_mutex);
        if (dirty) {
        read_buffer((uint64_t)&end_cycle, 0, FIRMWARE_CYCLE);
        write_req_cycles += end_cycle - start_cycle;
        process_request(pFTL->write(reqInternal));
        read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); 
        }
        // TEMP: Disable DRAM calculation for prevent conflict
        dram_read((uint64_t)copy_buffer, req.length);
        mutex_unlock(&cache_mutex);
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
void SimpleICL::flush(LPNRange &range) {
    uint64_t flush_req_cycles = 0;
    uint64_t start_cycle;
    uint64_t end_cycle;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    if (useReadCaching || useWriteCaching) {
        uint64_t finishedAt;
        read_buffer((uint64_t)&finishedAt, 0, FIRMWARE_TICK);
        FTL::Request reqInternal(lineCountInSuperPage);
        mutex_lock(&cache_mutex);
        read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE); // reset start cycle after FTL finishes(eviction can cause FTL writes so we skip)
        if (range.nlp < setSize * waySize) {
            for (uint64_t lpn = range.slpn; lpn < range.slpn + range.nlp; lpn++) {
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
            }
        } else {
            mutex_lock(&cache_mutex);
            for (uint32_t setIdx = 0; setIdx < setSize; setIdx++) {
                for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
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
            }
        }
        mutex_unlock(&cache_mutex);
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
void SimpleICL::trim(LPNRange &range) {
  
    uint64_t start_cycle;
    uint64_t end_cycle;
    uint64_t trim_req_cycles = 0;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    if (useReadCaching || useWriteCaching) {
        uint64_t finishedAt;
        read_buffer((uint64_t)&finishedAt, 0, FIRMWARE_TICK);
        FTL::Request reqInternal(lineCountInSuperPage);
        mutex_lock(&cache_mutex);
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
        mutex_unlock(&cache_mutex);
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

void SimpleICL::format(LPNRange &range) {
    uint64_t start_cycle;
    uint64_t end_cycle;
    read_buffer((uint64_t)&start_cycle, 0, FIRMWARE_CYCLE);
    mutex_lock(&cache_mutex);
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
    mutex_unlock(&cache_mutex);
    mutex_lock(&stat_mutex);
    icl_stats.format_req_cycles += end_cycle - start_cycle;
    icl_stats.format_requests++;
    icl_stats.format_bytes += range.nlp * lineCountInSuperPage * lineSize;
    icl_stats.heap_top = get_heap_top();
    mutex_unlock(&stat_mutex);
  
}

}