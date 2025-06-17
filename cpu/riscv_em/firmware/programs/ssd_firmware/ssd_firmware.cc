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

#include "ssd_firmware.hh"
#include "utils.h"

Firmware::Firmware() { 
  read_buffer((uint64_t)&ssd_params, FIRMWARE_REQ);
  printf("ssd params bad block threshold %u\n", ssd_params.bad_block_threshold);
  printf("ssd params page size %u\n", ssd_params.pageSize);
  printf("ssd params pages in block %u\n", ssd_params.pagesInBlock);
  for(uint32_t i = 0; i < ssd_params.pageCountToMaxPerf; i++) {
    lastFreeBlock.push_back(0);
  }
  test.emplace_back(Pair<int,int>(1, 2));
}

Firmware::~Firmware() {

}
 
void Firmware::print_status() {
  printf("hi!\n");
}