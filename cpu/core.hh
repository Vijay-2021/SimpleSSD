#pragma once

#include "fetch_unit.hh"
#include "decoder.hh"

namespace SimpleSSD {

namespace CPU {


class Core {
    private:
        FetchUnit fetch_unit;
        Decoder decoder;
        bool busy;
        uint64_t cycles;
        Event jobEvent;
        std::queue<JobEntry> jobs;
 
        CoreStat stat;
 
        void handleJob();
        void jobDone();
        void tick(); 

    public:
        Core();
        ~Core();
    
        void submitJob(JobEntry, uint64_t = 0); // method to interface with HIL
     
        void addStat(InstStat &);
 
        bool isBusy();
        uint64_t getJobListSize();
        CoreStat &getStat();
};

}

}