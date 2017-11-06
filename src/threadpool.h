/*
 * CS 6210 - Fall 2017
 * Project 3
 * Threadpool management
 */

#pragma once

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <list>
#include <condition_variable>

class threadpool {
   public:
      threadpool(unsigned num_threads)
         : threads(num_threads), rem(0), giveup(false), done(false) 
      {	
         unsigned int i;
         printf("Maximum threads in threadpool:\t%u\n", num_threads);

         for(i = 0; i < num_threads; i++)
         {
            threads[i] = std::move(std::thread([this, i]{this->task();}));
         }
      }

      ~threadpool() 
      {
         joinAll();
      }
      
      void addJob(std::function<void()> job) 
      {
         std::lock_guard<std::mutex> lock(m_queue);
         job_queue.emplace_back(job);
         rem++;
         is_avail.notify_one();
      }

      void joinAll() 
      {
         bool wait_all = true;
         if (!done) 
         {
            if (wait_all)
            {
               all_wait();
            }

            giveup = true;
            is_avail.notify_all();
            for (auto &x : threads)
            {
               if(x.joinable())
               {
                  x.join();
               }
            }
            done = true;
         }
      }

      void all_wait() 
      {
         if (rem) 
         {
            std::unique_lock<std::mutex> lock(m_wait);
            is_wait.wait(lock, [this]{return (this->rem == 0);});
            lock.unlock();
         }
      }

   private:
      void task() 
      {
         while (!giveup)
         {
            next_task()();
            rem--;
            is_wait.notify_one();
         }
      }

      std::function<void()> next_task() 
      {
         std::function<void()> task;
         std::unique_lock<std::mutex> lock(m_queue);

         is_avail.wait(lock, [this]()->bool{return (job_queue.size() || giveup);});

         if (!giveup)
         {
            task = job_queue.front();  //first task on queue
            job_queue.pop_front();  //Remove task from queue
         }
         else 
         { 
            task = []{};
            rem++;
         }
         return task;
      }

      std::vector<std::thread> threads;
      std::list<std::function<void()>> job_queue;
      std::atomic_int rem;
      std::atomic_bool giveup;
      std::atomic_bool done;
      std::condition_variable is_avail;
      std::condition_variable is_wait;
      std::mutex m_wait;
      std::mutex m_queue;
};

#endif
