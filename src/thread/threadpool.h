#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <iostream>
#include "locker.h"
#include "utils/logger.h"

// threadpool class, defined as template to enable the reuse of codes
// T is the task class
template<typename T>
class threadpool {
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T *request);

private:
    static void * worker(void *arg);
    void run();

private:
    // number of threads
    int m_thread_number;

    // array of threads, with size of m_thread_number
    pthread_t * m_threads;

    // maximum #tasks waiting to be processed in the requests queue 
    int m_max_requests;

    // request queue or task queue
    std::list<T*> m_workqueue;

    // exclusive mutex
    locker m_queuelocker;

    // semaphore to judge if there is task to be processed
    sem m_queuestat;

    // whether or not terminates the thread
    bool m_stop;
};


template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : 
m_thread_number(thread_number), 
m_max_requests(max_requests),
m_stop(false), m_threads(NULL) {

        if ((thread_number <= 0) || (max_requests <= 0)){
            throw std::exception();
        }
        // initialize the semaphore
        m_threads = new pthread_t[m_thread_number];
        if (!m_threads) {
            throw std::exception();
        }

        // create thread_number threads and detach them
        for (int i = 0; i < thread_number; ++i){
            // std::cout << "Create the " << i << "th thread\n";
            Logger::get_instance()->log(INFO, "Create thread " + std::to_string(i));
            // create a thread
            if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
                delete [] m_threads;
                throw std::exception();
            }
            // ask it to wait for task available in the m_workqueue
            if (pthread_detach(m_threads[i])){
                delete[] m_threads;
                throw std::exception();
            }
        }
}


template<typename T>
threadpool<T>::~threadpool(){
    delete[]m_threads;
    m_stop = 1;
}

template<typename T>
bool threadpool<T>::append(T *request){

    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // add semaphore value
    m_queuestat.post();
    return 1;
}


template<typename T>
void* threadpool<T>::worker(void * arg) {
    // static function cannot use the non-static member
    // It points to an existing threadpool object
    threadpool *pool = (threadpool *) arg;
    pool->run();
    return pool;
}


template<typename T>
void threadpool<T>::run(){
    while(!m_stop) {
        // get a task from the request queue
        // have sem value-1
        // no value, block until value > 0, then any one of blocked thread will be woken up
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request){ 
            continue;
        }
        request->process();
    }
}

#endif
