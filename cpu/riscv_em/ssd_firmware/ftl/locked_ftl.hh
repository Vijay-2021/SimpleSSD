// #ifndef __RISCV_LOCKED_FTL__
// #define __RISCV_LOCKED_FTL__

// namespace FTL {

// class LockedFTL : public AbstractFTL {
//   private:
//     uint32_t num_active_cores;
//     Mutex queue_mutex;

//   public:
//     LockedFTL(ftl_params &fparams);
//     ~LockedFTL();
//     void run(FTL::Request &req, ICL::AbstractICL *cache);

// };

// }

// #endif