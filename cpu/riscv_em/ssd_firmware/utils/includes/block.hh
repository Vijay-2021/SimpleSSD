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

#ifndef __FTL_COMMON_BLOCK__
#define __FTL_COMMON_BLOCK__

#include <stdint.h>
#include "vector.hh"

#include "bitset.hh"

class Block {
 private:
  uint32_t idx;
  uint32_t pageCount;
  uint32_t ioUnitInPage;
  uint32_t *pNextWritePageIndex;

  // Following variables are used when ioUnitInPage == 1
  Bitset *pValidBits;
  Bitset *pErasedBits;
  uint64_t *pLPNs;

  // Following variables are used when ioUnitInPage > 1
  
  Vector<Bitset> erasedBits;
  uint64_t **ppLPNs;

  uint64_t lastAccessed;
  uint32_t eraseCount;
  bool is_valid; 
 public:
  Vector<Bitset> validBits;
  Block();  // Default constructor
  Block(uint32_t, uint32_t, uint32_t);
  Block(const Block &);      // Copy constructor
  Block(Block &&) noexcept;  // Move constructor
  ~Block();

  Block &operator=(const Block &);  // Copy assignment
  Block &operator=(Block &&);       // Move assignment

  uint32_t getBlockIndex() const;
  uint64_t getLastAccessedTime();
  uint32_t getEraseCount();
  uint32_t getValidPageCount();
  uint32_t getValidPageCountRaw();
  uint32_t getDirtyPageCount();
  uint32_t getNextWritePageIndex();
  uint32_t getNextWritePageIndex(uint32_t);
  bool getPageInfo(uint32_t, Vector<uint64_t> &, Bitset &);
  bool read(uint32_t, uint32_t);
  bool write(uint32_t, uint64_t, uint32_t);
  void erase();
  void invalidate(uint32_t, uint32_t);
  bool isValid();
};

#endif
