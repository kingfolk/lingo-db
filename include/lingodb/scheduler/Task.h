#ifndef LINGODB_SCHEDULER_TASK_H
#define LINGODB_SCHEDULER_TASK_H
#include <atomic>
namespace lingodb::scheduler {
class Task {
   protected:
   bool singleRun{false};
   std::atomic<bool> workExhausted{false};

   // fixedSize > 0 means this task can be split into `fixedSize` pieces
   size_t fixedSize{0};
   std::atomic<size_t> fixedSizeCursor{0};


   public:
   bool onlySingleRun() {
      return singleRun;
   }
   bool hasWork() {
      return !workExhausted.load();
   }
   bool isFixedSize() {
      return fixedSize > 0;
   }
   bool fetchNextFixedSizeUnit() {
      auto nextId = fixedSizeCursor.fetch_add(1);
      if (nextId < fixedSize) {
         return true;
      }
      return false;
   }

   virtual void run() = 0;
   virtual ~Task() {}
};
} // namespace lingodb::scheduler
#endif //LINGODB_SCHEDULER_TASK_H
