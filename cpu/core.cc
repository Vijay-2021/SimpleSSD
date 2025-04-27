#include "core.hh"

CPU::Core::Core() : busy(false) {
    jobEvent = allocate([this](uint64_t) { jobDone(); });
  }
  
  CPU::Core::~Core() {}
  
  bool CPU::CPU::Core::isBusy() {
    return busy;
  }
  
  uint64_t CPU::CPU::Core::getJobListSize() {
    return jobs.size();
  }
  
  CPU::CoreStat &CPU::Core::getStat() {
    return stat;
  }
  
  void CPU::Core::submitJob(JobEntry job, uint64_t delay) {
    job.delay = delay;
    job.submitAt = getTick();
    jobs.push(job);
  
    if (!busy) {
      handleJob();
    }
  }
  
  void CPU::Core::handleJob() {
    auto &iter = jobs.front();
    uint64_t now = getTick();
    uint64_t diff = now - iter.submitAt;
    uint64_t finishedAt;
  
    if (diff >= iter.delay) {
      finishedAt = now + iter.inst->latency;
    }
    else {
      finishedAt = now + iter.inst->latency + iter.delay - diff;
    }
  
    busy = true;
  
    schedule(jobEvent, finishedAt);
  }
  
  void CPU::Core::jobDone() {
    auto &iter = jobs.front();
  
    iter.func(getTick(), iter.context);
    stat.busy += iter.inst->latency;
    stat.instStat += *iter.inst;
  
    jobs.pop();
    busy = false;
  
    if (jobs.size() > 0) {
      handleJob();
    }
  }