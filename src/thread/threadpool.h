#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <iostream>
#include "locker.h"
#include "utils/logger.h"

// Fixed-size worker pool for connection tasks.
template<typename T>
class threadpool {
public:
    // Creates worker threads and prepares the bounded task queue.
    threadpool(int thread_number = 8, int max_requests = 10000);

    // Releases the worker thread handle array and marks the pool as stopped.
    ~threadpool();

    // Adds a connection task to the queue and wakes one waiting worker.
    bool append(T *request);

private:
    // Static pthread entry point that forwards execution to run().
    static void * worker(void *arg);

    // Worker loop: waits for queued tasks and processes them.
    void run();

private:
    int m_thread_number;
    pthread_t * m_threads;
    int m_max_requests;

    std::list<T*> m_workqueue;

    locker m_queuelocker;
    sem m_queuestat;

    bool m_stop;
};


template<typename T>
// Starts and detaches the fixed set of worker threads.
threadpool<T>::threadpool(int thread_number, int max_requests) : 
m_thread_number(thread_number), 
m_max_requests(max_requests),
m_stop(false), m_threads(NULL) {

        if ((thread_number <= 0) || (max_requests <= 0)){
            throw std::exception();
        }
        m_threads = new pthread_t[m_thread_number];
        if (!m_threads) {
            throw std::exception();
        }

        for (int i = 0; i < thread_number; ++i){
            Logger::get_instance()->log(INFO, "Create thread " + std::to_string(i));
            if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
                delete [] m_threads;
                throw std::exception();
            }
            if (pthread_detach(m_threads[i])){
                delete[] m_threads;
                throw std::exception();
            }
        }
}


template<typename T>
// Marks the pool stopped and releases the pthread id array.
threadpool<T>::~threadpool(){
    delete[]m_threads;
    m_stop = 1;
}

template<typename T>
// Pushes a task into the queue if capacity allows.
bool threadpool<T>::append(T *request){

    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return 1;
}


template<typename T>
// Bridges the C pthread callback API to the C++ threadpool instance.
void* threadpool<T>::worker(void * arg) {
    threadpool *pool = (threadpool *) arg;
    pool->run();
    return pool;
}


template<typename T>
// Repeatedly pulls tasks from the queue and invokes request->process().
void threadpool<T>::run(){
    while(!m_stop) {
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
