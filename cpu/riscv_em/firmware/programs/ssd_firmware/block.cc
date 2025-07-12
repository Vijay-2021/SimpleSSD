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

#include "block.hh"
#include "string.h"
#include "memory.h"
#include "cs_instructions.h"
#include "def.hh"

Block::Block() {
  idx = 0;
  pageCount = 0;
  ioUnitInPage = 0;
  pNextWritePageIndex = nullptr;
  pValidBits = nullptr;
  pErasedBits = nullptr;
  pLPNs = nullptr;
  ppLPNs = nullptr;
  lastAccessed = 0;
  eraseCount = 0;
  is_valid = false;
}

Block::Block(uint32_t blockIdx, uint32_t count, uint32_t ioUnit)
    : idx(blockIdx),
      pageCount(count),
      ioUnitInPage(ioUnit),
      pValidBits(nullptr),
      pErasedBits(nullptr),
      pLPNs(nullptr),
      ppLPNs(nullptr),
      lastAccessed(0),
      eraseCount(0) {
  if (ioUnitInPage == 1) {
    pValidBits = new Bitset(pageCount);
    pErasedBits = new Bitset(pageCount);
    pLPNs = (uint64_t *)calloc(pageCount, sizeof(uint64_t));
  }
  else if (ioUnitInPage > 1) {
    Bitset copy(ioUnitInPage);
    validBits = Vector<Bitset>(pageCount, copy);
    erasedBits = Vector<Bitset>(pageCount, copy);
    ppLPNs = (uint64_t **)calloc(pageCount, sizeof(uint64_t *));
    pLPNs = (uint64_t *)calloc(pageCount * ioUnitInPage, sizeof(uint64_t));
    for (uint32_t i = 0; i < pageCount; i++) {
      ppLPNs[i] = pLPNs + i * ioUnitInPage;
    }
  }
  else {
    panic("Invalid I/O unit in page");
  }

  // C-style allocation
  pNextWritePageIndex = (uint32_t *)calloc(ioUnitInPage, sizeof(uint32_t));
  if (ioUnitInPage == 1) {
    pErasedBits->set();
  }
  else {
    for (auto &iter : erasedBits) {
      iter.set();
    }
  }
  eraseCount = 0;
  is_valid = true; // Mark the block as valid
}

Block::Block(const Block &old)
    : Block(old.idx, old.pageCount, old.ioUnitInPage) {
  if (ioUnitInPage == 1) {
    *pValidBits = *old.pValidBits;
    *pErasedBits = *old.pErasedBits;

    memcpy(pLPNs, old.pLPNs, pageCount * sizeof(uint64_t));
  }
  else {
    validBits = old.validBits;
    erasedBits = old.erasedBits;
    memcpy(pLPNs, old.pLPNs, pageCount * ioUnitInPage * sizeof(uint64_t));
  }

  memcpy(pNextWritePageIndex, old.pNextWritePageIndex,
         ioUnitInPage * sizeof(uint32_t));

  eraseCount = old.eraseCount;
  is_valid = old.is_valid; // Copy validity status
}

Block::Block(Block &&old) noexcept
    : idx(move(old.idx)),
      pageCount(move(old.pageCount)),
      ioUnitInPage(move(old.ioUnitInPage)),
      pNextWritePageIndex(move(old.pNextWritePageIndex)),
      pValidBits(move(old.pValidBits)),
      pErasedBits(move(old.pErasedBits)),
      pLPNs(move(old.pLPNs)),
      validBits(move(old.validBits)),
      erasedBits(move(old.erasedBits)),
      ppLPNs(move(old.ppLPNs)),
      lastAccessed(move(old.lastAccessed)),
      eraseCount(move(old.eraseCount)),
      is_valid(move(old.is_valid)) {
  // TODO Use std::exchange to set old value to null (C++14)
  old.idx = 0;
  old.pageCount = 0;
  old.ioUnitInPage = 0;
  old.pNextWritePageIndex = nullptr;
  old.pValidBits = nullptr;
  old.pErasedBits = nullptr;
  old.pLPNs = nullptr;
  old.ppLPNs = nullptr;
  old.lastAccessed = 0;
  old.eraseCount = 0;
  old.is_valid = false;
}

Block::~Block() {
  free(pNextWritePageIndex);
  free(pLPNs);

  delete pValidBits;
  delete pErasedBits;

  if (ppLPNs) {
    //for (uint32_t i = 0; i < pageCount; i++) {
    //  free(ppLPNs[i]);
    //}

    free(ppLPNs);
  }

  pNextWritePageIndex = nullptr;
  pLPNs = nullptr;
  pValidBits = nullptr;
  pErasedBits = nullptr;
  ppLPNs = nullptr;
}

Block &Block::operator=(const Block &rhs) {
  if (this != &rhs) {
    this->~Block();
    *this = Block(rhs);  // Call copy constructor
  }

  return *this;
}

Block &Block::operator=(Block &&rhs) {
  if (this != &rhs) {
    this->~Block();

    idx = move(rhs.idx);
    pageCount = move(rhs.pageCount);
    ioUnitInPage = move(rhs.ioUnitInPage);
    pNextWritePageIndex = move(rhs.pNextWritePageIndex);
    pValidBits = move(rhs.pValidBits);
    pErasedBits = move(rhs.pErasedBits);
    pLPNs = move(rhs.pLPNs);
    validBits = move(rhs.validBits);
    erasedBits = move(rhs.erasedBits);
    ppLPNs = move(rhs.ppLPNs);
    lastAccessed = move(rhs.lastAccessed);
    eraseCount = move(rhs.eraseCount);
    is_valid = move(rhs.is_valid);
    rhs.pNextWritePageIndex = nullptr;
    rhs.pValidBits = nullptr;
    rhs.pErasedBits = nullptr;
    rhs.pLPNs = nullptr;
    rhs.ppLPNs = nullptr;
    rhs.lastAccessed = 0;
    rhs.eraseCount = 0;
  }

  return *this;
}

uint32_t Block::getBlockIndex() const {
  return idx;
}

uint64_t Block::getLastAccessedTime() {
  return lastAccessed;
}

uint32_t Block::getEraseCount() {
  return eraseCount;
}

uint32_t Block::getValidPageCount() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    ret = pValidBits->count();
  }
  else {
    for (auto &iter : validBits) {
      if (iter.any()) {
        ret++;
      }
    }
  }

  return ret;
}

uint32_t Block::getValidPageCountRaw() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    // Same as getValidPageCount()
    ret = pValidBits->count();
  }
  else {
    for (auto &iter : validBits) {
      ret += iter.count();
    }
  }

  return ret;
}

uint32_t Block::getDirtyPageCount() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    ret = (~(*pValidBits | *pErasedBits)).count();
  }
  else {
    for (uint32_t i = 0; i < pageCount; i++) {
      // Dirty: Valid(false), Erased(false)
      if ((~(validBits.at(i) | erasedBits.at(i))).any()) {
        ret++;
      }
    }
  }

  return ret;
}

uint32_t Block::getNextWritePageIndex() {
  uint32_t idx = 0;

  for (uint32_t i = 0; i < ioUnitInPage; i++) {
    if (idx < pNextWritePageIndex[i]) {
      idx = pNextWritePageIndex[i];
    }
  }

  return idx;
}

uint32_t Block::getNextWritePageIndex(uint32_t idx) {
  return pNextWritePageIndex[idx];
}

bool Block::getPageInfo(uint32_t pageIndex, Vector<uint64_t> &lpn,
                        Bitset &map) {
  if (ioUnitInPage == 1 && map.size() == 1) {
    map.set();
    lpn = Vector<uint64_t>(1, pLPNs[pageIndex]);
  }
  else if (map.size() == ioUnitInPage) {
    map = validBits.at(pageIndex);
    lpn = Vector<uint64_t>(ppLPNs[pageIndex], ppLPNs[pageIndex] + ioUnitInPage);
  }
  else {
    panic("I/O map size mismatch");
  }

  return map.any();
}

bool Block::read(uint32_t pageIndex, uint32_t idx) {
  bool read = false;

  if (ioUnitInPage == 1 && idx == 0) {
    read = pValidBits->test(pageIndex);
  }
  else if (idx < ioUnitInPage) {
    read = validBits.at(pageIndex).test(idx);
  }
  else {
    panic("I/O map size mismatch");
  }

  if (read) {
    lastAccessed = getTick();
  }

  return read;
}

bool Block::write(uint32_t pageIndex, uint64_t lpn, uint32_t idx) {
  bool write = false;

  if (ioUnitInPage == 1 && idx == 0) {
    write = pErasedBits->test(pageIndex);
  }
  else if (idx < ioUnitInPage) {
    write = erasedBits.at(pageIndex).test(idx);
  }
  else {
    panic("I/O map size mismatch");
  }

  if (write) {
    if (pageIndex < pNextWritePageIndex[idx]) {
      panic("Write to block should sequential");
    }

    lastAccessed = getTick();

    if (ioUnitInPage == 1) {
      pErasedBits->reset(pageIndex);
      pValidBits->set(pageIndex);

      pLPNs[pageIndex] = lpn;
    }
    else {
      erasedBits.at(pageIndex).reset(idx);
      validBits.at(pageIndex).set(idx);

      ppLPNs[pageIndex][idx] = lpn;
    }

    pNextWritePageIndex[idx] = pageIndex + 1;
  }
  else {
    panic("Write to non erased page");
  }

  return write;
}

void Block::erase() {
  if (ioUnitInPage == 1) {
    pValidBits->reset();
    pErasedBits->set();
  }
  else {
    for (auto &iter : validBits) {
      iter.reset();
    }
    for (auto &iter : erasedBits) {
      iter.set();
    }
  }
  memset(pNextWritePageIndex, 0, sizeof(uint32_t) * ioUnitInPage);
  eraseCount++;
}

void Block::invalidate(uint32_t pageIndex, uint32_t idx) {
  if (ioUnitInPage == 1) {
    pValidBits->reset(pageIndex);
  }
  else {
    validBits.at(pageIndex).reset(idx);
  }
}

bool Block::isValid() {
  return is_valid;
}