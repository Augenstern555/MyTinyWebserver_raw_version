/*同步/异步日志系统
===============
同步/异步日志系统主要涉及了两个模块，一个是日志模块，一个是阻塞队列模块,其中加入阻塞队列模块主要是解决异步写入日志做准备.
> * 自定义阻塞队列
> * 单例模式创建日志
> * 同步日志
> * 异步日志
> * 实现按天、超行分类
*/

#ifndef LOG_H
#define LOG_H

#include<stdio.h>
#include<iostream>
#include<string>
#include<stdarg.h>
#include<pthread.h>
#include"block_queue.h"

using namespace std;
class Log{
public:
    static Log *get_instance() {
        static Log instance;
        return &instance;
    }
    static void *flush_log_thread(void *args) {
        Log::get_instance()->async_write_log();
    }
    bool init(const char*file_name, int log_buf_size = 8192, int spilt_lines = 5000000, int max_queue_size = 0);
    void write_log(int level, const char* format, ...);
    void flush(void);
private:
    Log();
    virtual ~Log();
    void* async_write_log() {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while(m_log_queue->pop(single_log)){
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
private:
    char dir_name[128];
    char log_name[128];
    int m_spilt_lines;//日志最大行数
    int m_log_buf_size;//日志缓冲区大小
    long long m_count; //日志行数记录
    int m_today;
    FILE *m_fp;
    char * m_buf;
    block_queue<string> *m_log_queue;//阻塞队列
    bool m_is_async;
    locker m_mutex;
};

#define LOG_BUG(format, ...) Log::get_instance->write_log(0, format, ##_VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif