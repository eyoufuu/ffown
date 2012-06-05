#ifndef _TIMER_SERVICE_H_
#define _TIMER_SERVICE_H_

#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

#include <list>
using namespace std;

#include "thread.h"
#include "lock.h"

class timer_service_t 
{
    struct interupt_info_t
    {
        int pipe_fds[2];
        interupt_info_t()
        {
            ::pipe(pipe_fds);
            ::fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);
            ::fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK);
        }
        ~interupt_info_t()
        {
            ::close(pipe_fds[0]);
            ::close(pipe_fds[1]);
        }
        int read_fd() { return pipe_fds[0]; }
    };
    struct registered_info_t
    {
        registered_info_t(long ms_, const task_t& t_):
            dest_tm(ms_),
            callback(t_)
        {}
        bool is_timeout(long cur_ms_)       { return dest_tm <= cur_ms_; }
        long    dest_tm;
        task_t  callback;
    };
    typedef list<registered_info_t> registered_info_list_t;
public:
    timer_service_t(long tick);
    virtual ~timer_service_t();

    void timer_callback(long ms_, task_t func);

    void run();

private:
    void interupt();
    void process_timer_callback(long cost_ms_);
private:
    volatile bool            m_runing;
    int                      m_efd;
    volatile long            m_min_timeout;
    int                      m_cache_list;
    int                      m_checking_list;
    registered_info_list_t   m_registered_data[2];
    //! interupt_info_t          m_interupt_info;
    thread_t                 m_thread;
    mutex_t                  m_mutex;
};

timer_service_t::timer_service_t(long tick):
    m_runing(true),
    m_efd(-1),
    m_min_timeout(tick),
    m_cache_list(0),
    m_checking_list(1)
{
    m_efd = ::epoll_create(16);

    struct lambda_t
    {
        static void run(void* p_)
        {
            ((timer_service_t*)p_)->run();
        }
    };
    m_thread.create_thread(task_t(&lambda_t::run, this), 1);
}

timer_service_t::~timer_service_t()
{
    m_runing = false;
    //! interupt();
    ::close(m_efd);
    m_thread.join();
}

void timer_service_t::run()
{
    struct epoll_event ev_set[64];
    //! interupt();

    struct timeval tv;

    do
    {
        ::epoll_wait(m_efd, ev_set, 64, m_min_timeout);

        if (false == m_runing)//! cancel
        {
            break;
        }

        gettimeofday(&tv, NULL);
        long cur_ms = tv.tv_sec*1000 + tv.tv_usec / 1000;

        process_timer_callback(cur_ms);
        
    }while (true) ;
}

void timer_service_t::timer_callback(long ms_, task_t func)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long   dest_ms = tv.tv_sec*1000 + tv.tv_usec / 1000 + ms_;

    lock_guard_t lock(m_mutex);
    m_registered_data[m_cache_list].push_back(registered_info_t(dest_ms, func));
}

void timer_service_t::interupt()
{
    /*
    epoll_event ev = { 0, { 0 } };
    ev.events = EPOLLIN | EPOLLPRI | EPOLLOUT | EPOLLHUP | EPOLLET;
    ev.data.fd= m_interupt_info.read_fd();
    ::epoll_ctl(m_efd, EPOLL_CTL_ADD, ev.data.fd, &ev);
     */
}

void timer_service_t::process_timer_callback(long cost_ms_)
{
    {
        lock_guard_t lock(m_mutex);
        std::swap(m_checking_list, m_cache_list);
    }
    
    registered_info_list_t::iterator it = m_registered_data[m_checking_list].begin();
    while (it != m_registered_data[m_checking_list].end()) 
    {
        if (it->is_timeout(cost_ms_))
        {
            it->callback.run();
            registered_info_list_t::iterator tmp = it++;
            m_registered_data[m_checking_list].erase(tmp);
        }
        else
        {
            ++it;
        }
    }
    
    if (false == m_registered_data[m_checking_list].empty())
    {
        lock_guard_t lock(m_mutex);
        m_registered_data[m_cache_list].insert(m_registered_data[m_cache_list].end(),
                                               m_registered_data[m_checking_list].begin(),
                                               m_registered_data[m_checking_list].end());
        m_registered_data[m_checking_list].clear();
    }
}

#endif

