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

#include "dram/simple.hh"

#include "util/algorithm.hh"

namespace SimpleSSD {

namespace Memory {

#define REFRESH_PERIOD 64000000000

SimpleDRAM::Stat::Stat() : count(0), size(0) {}

SimpleDRAM::SimpleDRAM(ConfigReader &p)
    : AbstractDRAM(p), lastDRAMAccess(0), ignoreScheduling(false) {
  pageFetchLatency = pTiming->tRP + pTiming->tRAS;
  interfaceBandwidth = 2.0 * pStructure->busWidth * pStructure->chip *
                       pStructure->channel / 8.0 / pTiming->tCK;

  autoRefresh = allocate([this](uint64_t now) {
    dramPower->doCommand(Data::MemCommand::REF, 0, now / pTiming->tCK);

    lastDRAMAccess = MAX(lastDRAMAccess, now + pTiming->tRFC);

    schedule(autoRefresh, now + REFRESH_PERIOD);
  });
  for (size_t i = 0; i < pStructure->bank; i++) {
    open_rows.push_back(0);
  }

  schedule(autoRefresh, getTick() + REFRESH_PERIOD);
}

SimpleDRAM::~SimpleDRAM() {
  // DO NOTHING
}

uint64_t SimpleDRAM::getBank(uint64_t addr) {
  return (addr >> pStructure->colBits) & ((1 << pStructure->bankBits) - 1);
}

uint64_t SimpleDRAM::getRow(uint64_t addr) {
  return addr >> (pStructure->colBits + pStructure->bankBits);
}

uint64_t SimpleDRAM::access(uint64_t addr, uint64_t size) {
    uint64_t ticks = 0;
    uint64_t offset = 0;
    while (offset < size) {
        uint64_t a = addr + offset;
        uint64_t bank = getBank(a);
        uint64_t row = getRow(a);
        uint64_t rowOffset = a % pStructure->rowSize;
        uint64_t chunk = std::min(size - offset, pStructure->rowSize - rowOffset);
        uint64_t bursts = std::ceil(chunk / pStructure->burstLength);

        if (open_rows[bank] == row) {
            ticks += pTiming->tCL + bursts * pTiming->tBURST;
        } else {
            ticks += pTiming->tRP + pTiming->tRAS + pTiming->tRCD + pTiming->tCL + bursts * pTiming->tBURST;
            open_rows[bank] = row;
        }
        offset += chunk;
    }
    return ticks;
}

uint64_t SimpleDRAM::updateDelay(uint64_t latency, uint64_t &tick) {
  uint64_t beginAt = tick;

  if (tick > 0) {
    if (ignoreScheduling) {
      tick += latency;
    }
    else {
      if (lastDRAMAccess <= tick) {
        lastDRAMAccess = tick + latency;
      }
      else {
        beginAt = lastDRAMAccess;
        lastDRAMAccess += latency;
      }

      tick = lastDRAMAccess;
    }
  }

  return beginAt;
}

void SimpleDRAM::updateStats(uint64_t cycle) {
  dramPower->calcWindowEnergy(cycle);

  auto &energy = dramPower->getEnergy();
  auto &power = dramPower->getPower();

  totalEnergy += energy.window_energy;
  totalPower = power.average_power;
}

void SimpleDRAM::setScheduling(bool enable) {
  ignoreScheduling = !enable;
}

bool SimpleDRAM::isScheduling() {
  return !ignoreScheduling;
}

void SimpleDRAM::read(void *, uint64_t size, uint64_t &tick) {
  uint64_t pageCount = (size > 0) ? (size - 1) / pStructure->pageSize + 1 : 0;
  uint64_t latency =
      (uint64_t)(pageCount * (pageFetchLatency +
                              pStructure->pageSize / interfaceBandwidth));

  uint64_t beginAt = updateDelay(latency, tick);

  // DRAMPower uses cycle unit
  beginAt /= pTiming->tCK;

  dramPower->doCommand(Data::MemCommand::ACT, 0, beginAt);

  for (uint64_t i = 0; i < pageCount; i++) {
    dramPower->doCommand(Data::MemCommand::RD, 0,
                         beginAt + spec.memTimingSpec.RCD);

    beginAt += spec.memTimingSpec.RCD;
  }

  beginAt -= spec.memTimingSpec.RCD;

  dramPower->doCommand(Data::MemCommand::PRE, 0,
                       beginAt + spec.memTimingSpec.RAS);

  // Stat Update
  updateStats(beginAt + spec.memTimingSpec.RAS + spec.memTimingSpec.RP);
  readStat.count++;
  readStat.size += size;
}

void SimpleDRAM::write(void *, uint64_t size, uint64_t &tick) {
  uint64_t pageCount = (size > 0) ? (size - 1) / pStructure->pageSize + 1 : 0;
  uint64_t latency =
      (uint64_t)(pageCount * (pageFetchLatency +
                              pStructure->pageSize / interfaceBandwidth));

  uint64_t beginAt = updateDelay(latency, tick);

  // DRAMPower uses cycle unit
  beginAt /= pTiming->tCK;

  dramPower->doCommand(Data::MemCommand::ACT, 0, beginAt);

  for (uint64_t i = 0; i < pageCount; i++) {
    dramPower->doCommand(Data::MemCommand::WR, 0,
                         beginAt + spec.memTimingSpec.RCD);

    beginAt += spec.memTimingSpec.RCD;
  }

  beginAt -= spec.memTimingSpec.RCD;

  dramPower->doCommand(Data::MemCommand::PRE, 0,
                       beginAt + spec.memTimingSpec.RAS);

  // Stat Update
  updateStats(beginAt + spec.memTimingSpec.RAS + spec.memTimingSpec.RP);
  writeStat.count++;
  writeStat.size += size;
}

void SimpleDRAM::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  AbstractDRAM::getStatList(list, prefix);

  temp.name = prefix + "read.request_count";
  temp.desc = "Read request count";
  list.push_back(temp);

  temp.name = prefix + "read.bytes";
  temp.desc = "Read data size in byte";
  list.push_back(temp);

  temp.name = prefix + "write.request_count";
  temp.desc = "Write request count";
  list.push_back(temp);

  temp.name = prefix + "write.bytes";
  temp.desc = "Write data size in byte";
  list.push_back(temp);

  temp.name = prefix + "request_count";
  temp.desc = "Total request count";
  list.push_back(temp);

  temp.name = prefix + "bytes";
  temp.desc = "Total data size in byte";
  list.push_back(temp);
}

void SimpleDRAM::getStatValues(std::vector<double> &values) {
  AbstractDRAM::getStatValues(values);

  values.push_back(readStat.count);
  values.push_back(readStat.size);
  values.push_back(writeStat.count);
  values.push_back(writeStat.size);
  values.push_back(readStat.count + writeStat.count);
  values.push_back(readStat.size + writeStat.size);
}

void SimpleDRAM::resetStatValues() {
  AbstractDRAM::resetStatValues();

  readStat = Stat();
  writeStat = Stat();
}

}  // namespace DRAM

}  // namespace SimpleSSD
