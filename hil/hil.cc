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

#include "hil/hil.hh"

#include "util/algorithm.hh"
#include "cpu/cpu.hh"

namespace SimpleSSD {

namespace HIL {

HIL::HIL(ConfigReader &c) : conf(c), reqCount(0), lastScheduled(0) {
  pICL = new ICL::ICL(conf);

  memset(&stat, 0, sizeof(stat));

  completionEvent = allocate([this](uint64_t) { completion(); });
}

HIL::~HIL() {
  delete pICL;
}

void HIL::read(Request &req) {
  DMAFunction doRead = [this](uint64_t tick, void *context) {
    auto pReq = (Request *)context;

    printf("calling dma for read at tick %lu with reqId %lu\n", tick, pReq->reqID);
    //debugprint(LOG_HIL,
    //           "READ  | REQ %7u | LCA %" PRIu64 " + %" PRIu64 " | BYTE %" PRIu64
    //           " + %" PRIu64,
     //          pReq->reqID, pReq->range.slpn, pReq->range.nlp, pReq->offset,
     //          pReq->length);

    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };
  Request *pReq = new Request(req);
  pReq->reqID = ++reqCount;
  pCPU->submitRead(pReq, doRead);
}

void HIL::write(Request &req) {
  DMAFunction doWrite = [this](uint64_t tick, void *context) {
    auto pReq = (Request *)context;
    printf("calling dma for write at tick %lu with reqId %lu\n", tick, pReq->reqID);
    //debugprint(LOG_HIL,
    //           "WRITE | REQ %7u | LCA %" PRIu64 " + %" PRIu64 " | BYTE %" PRIu64
    //           " + %" PRIu64,
    //           pReq->reqID, pReq->range.slpn, pReq->range.nlp, pReq->offset,
    //           pReq->length);

    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };
  Request *pReq = new Request(req);
  pReq->reqID = ++reqCount; 
  pCPU->submitWrite(pReq, doWrite);

}

void HIL::flush(Request &req) {
  DMAFunction doFlush = [this](uint64_t tick, void *context) {
    auto pReq = (Request *)context;
    printf("calling dma for flush at tick %lu with reqId %lu\n", tick, pReq->reqID);
    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };
  Request *pReq = new Request(req);
  pReq->reqID = ++reqCount;
  pCPU->submitFlush(pReq, doFlush);
}

void HIL::trim(Request &req) {
  DMAFunction doFlush = [this](uint64_t tick, void *context) {
    auto pReq = (Request *)context;
    printf("calling dma for trim at tick %lu with reqId %lu\n", tick, pReq->reqID);
    //debugprint(LOG_HIL, "TRIM  | REQ %7u | LCA %" PRIu64 " + %" PRIu64,
    //           pReq->reqID, pReq->range.slpn, pReq->range.nlp);

    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };
  Request *pReq = new Request(req);
  pReq->reqID = ++reqCount;
  pCPU->submitTrim(pReq, doFlush);
}

void HIL::format(Request &req, bool erase) {
  DMAFunction doFlush = [this, erase](uint64_t tick, void *context) {
    auto pReq = (Request *)context;
    printf("calling dma for format at tick %lu with reqId %lu\n", tick, pReq->reqID);
    pReq->finishedAt = tick;
    completionQueue.push(*pReq);

    updateCompletion();

    delete pReq;
  };
  Request *pReq = new Request(req);
  pReq->reqID = ++reqCount;
  if (erase) {
    pCPU->submitFlush(pReq, req.function);
  } else {
    pCPU->submitTrim(pReq, req.function);
  }
}

void HIL::getLPNInfo(uint64_t &totalLogicalPages, uint32_t &logicalPageSize) {
  pICL->getLPNInfo(totalLogicalPages, logicalPageSize);
}

uint64_t HIL::getUsedPageCount(uint64_t lcaBegin, uint64_t lcaEnd) {
  return pICL->getUsedPageCount(lcaBegin, lcaEnd);
}

void HIL::updateBusyTime(int idx, uint64_t begin, uint64_t end) {
  if (end <= stat.lastBusyAt[idx]) {
    return;
  }

  if (begin < stat.lastBusyAt[idx]) {
    stat.busy[idx] += end - stat.lastBusyAt[idx];
  }
  else {
    stat.busy[idx] += end - begin;
  }

  stat.lastBusyAt[idx] = end;
}

void HIL::updateCompletion() {
  if (completionQueue.size() > 0) {
    if (lastScheduled != completionQueue.top().finishedAt) {
      lastScheduled = completionQueue.top().finishedAt;
      schedule(completionEvent, MAX(lastScheduled, getTick()));
    }
  }
}

void HIL::completion() {
  uint64_t tick = getTick();

  while (completionQueue.size() > 0) {
    auto &req = completionQueue.top();

    if (req.finishedAt <= tick) {
      req.function(tick, req.context);

      completionQueue.pop();
    }
    else {
      break;
    }
  }

  updateCompletion();
}

void HIL::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "read.request_count";
  temp.desc = "Read request count";
  list.push_back(temp);

  temp.name = prefix + "read.bytes";
  temp.desc = "Read data size in byte";
  list.push_back(temp);

  temp.name = prefix + "read.busy";
  temp.desc = "Device busy time when read";
  list.push_back(temp);

  temp.name = prefix + "write.request_count";
  temp.desc = "Write request count";
  list.push_back(temp);

  temp.name = prefix + "write.bytes";
  temp.desc = "Write data size in byte";
  list.push_back(temp);

  temp.name = prefix + "write.busy";
  temp.desc = "Device busy time when write";
  list.push_back(temp);

  temp.name = prefix + "request_count";
  temp.desc = "Total request count";
  list.push_back(temp);

  temp.name = prefix + "bytes";
  temp.desc = "Total data size in byte";
  list.push_back(temp);

  temp.name = prefix + "busy";
  temp.desc = "Total device busy time";
  list.push_back(temp);

  pICL->getStatList(list, prefix);
}

void HIL::getStatValues(std::vector<double> &values) {
  values.push_back(stat.request[0]);
  values.push_back(stat.iosize[0]);
  values.push_back(stat.busy[0]);
  values.push_back(stat.request[1]);
  values.push_back(stat.iosize[1]);
  values.push_back(stat.busy[1]);
  values.push_back(stat.request[0] + stat.request[1]);
  values.push_back(stat.iosize[0] + stat.iosize[1]);
  values.push_back(stat.busy[2]);

  pICL->getStatValues(values);
}

void HIL::resetStatValues() {
  memset(&stat, 0, sizeof(stat));

  pICL->resetStatValues();
}

void HIL::setCPU(CPU::CPU *cpu) {
  pCPU = cpu;
}

ICL::ICL *HIL::getICL() {
  return pICL;
}

}  // namespace HIL

}  // namespace SimpleSSD
