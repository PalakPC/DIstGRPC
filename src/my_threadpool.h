class Threadpool
{
   private:

      std::atomic_int rem_jobs;
      std::atomic_bool give_up;
      std::atomic_bool done;
      std::condition_variable is_available;
      std::condition_variable is_wait;
      std::mutex wait;
      std::mutex line;
      std::vector<std::thread> threads;
      std::list<std::function<void(void)>> job_line;

      void job()
      {
         while (give_up == false)
         {
            next_job()();
            rem--;
            is_wait.notify_one();
         }
      }

      std::function<void(void)> next_job()
      {
         std::unique_lock<std::mutex> 


