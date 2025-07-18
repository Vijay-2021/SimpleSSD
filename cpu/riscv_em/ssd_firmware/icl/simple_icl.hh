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

#ifndef __ICL_SIMPLE_CACHE__
#define __ICL_SIMPLE_CACHE__

#include "vector.hh"
#include "ftl.hh"
#include "mutex.h"
#include "abstract_icl.hh"

namespace ICL {

class SimpleICL : AbstractICL {
 private:
  Mutex cache_mutex;
  void evictCache(bool flush = true) override;
 public:
  SimpleICL(icl_params& cparams, FTL::FTL *ftl);
  ~SimpleICL();

  bool read(Request &) override;
  bool write(Request &) override;
  void flush(LPNRange &) override;
  void trim(LPNRange &) override;
  void format(LPNRange &) override;
  
};

}

#endif
