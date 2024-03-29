#include <atomic>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <queue>
#include <stdexcept>
#include <vector>

#define THREADPOOL_MAX_NUM 16

//#define  THREADPOOL_AUTO_GROW
using namespace std;

class threadpool {
  unsigned short _initSize;       //初始化线程数量
  using Task = function<void()>;  //定义类型
  vector<thread> _pool;           //线程池
  queue<Task> _tasks;             //任务队列
  mutex _lock;                    //任务队列同步锁
#ifdef THREADPOOL_AUTO_GROW
  mutex _lockGrow;              //线程池增长同步锁
#endif                          // !THREADPOOL_AUTO_GROW
  condition_variable _task_cv;  //条件阻塞
  atomic<bool> _run{true};      //线程池是否执行
  atomic<int> _idlThrNum{0};    //空闲线程数量

 public:
  inline threadpool(unsigned short size = 4) {
    _initSize = size;
    addThread(size);
  }

  inline ~threadpool() {
    _run = false;
    _task_cv.notify_all();  // 唤醒所有线程执行
    for (thread& thread : _pool) {
      // thread.detach(); // 让线程“自生自灭”
      if (thread.joinable())
        thread.join();  // 等待任务结束， 前提：线程一定会执行完
    }
  }

 public:
  template <class F, class... Args>
  auto commit(F&& f, Args&&... args) -> future<decltype(f(args...))> {
    if (!_run)  // stoped ??
      throw runtime_error("commit on ThreadPool is stopped.");

    using RetType =
        decltype(f(args...));  // typename std::result_of<F(Args...)>::type,
                               // 函数 f 的返回值类型
    auto task = make_shared<packaged_task<RetType()>>(std::bind(
        forward<F>(f), forward<Args>(args)...));  // 把函数入口及参数,打包(绑定)
    future<RetType> future = task->get_future();
    {  // 添加任务到队列
      lock_guard<mutex> lock{
          _lock};  //对当前块的语句加锁  lock_guard 是 mutex 的 stack
      //封装类，构造的时候 lock()，析构的时候 unlock()
      _tasks.emplace([task]() {  // push(Task{...}) 放到队列后面
        (*task)();
      });
    }
#ifdef THREADPOOL_AUTO_GROW
    if (_idlThrNum < 1 && _pool.size() < THREADPOOL_MAX_NUM)
      addThread(1);
#endif                      // !THREADPOOL_AUTO_GROW
    _task_cv.notify_one();  // 唤醒一个线程执行

    return future;
  }

  template <class F, class Container,
            class TC = typename std::decay<Container>::type,
            class T = typename TC::value_type>
  auto map_container(F&& f, Container&& c)
      -> vector<future<typename decay<decltype(f(T{}))>::type>> {
    static_assert(is_same<TC, vector<T>>::value ||
                      is_same<TC, deque<T>>::value ||
                      is_same<TC, list<T>>::value,
                  "Container must be vector or list or deque");
    vector<future<typename decay<decltype(f(T{}))>::type>> res{};
    res.reserve(c.size());
    for (auto&& v : c) {
      res.push_back(commit(forward<F>(f), forward<T>(v)));
    }
    return res;
  }

  // 提交一个无参任务, 且无返回值
  template <class F>
  void commit2(F&& task) {
    if (!_run)
      return;
    {
      lock_guard<mutex> lock{_lock};
      _tasks.emplace(std::forward<F>(task));
    }
#ifdef THREADPOOL_AUTO_GROW
    if (_idlThrNum < 1 && _pool.size() < THREADPOOL_MAX_NUM)
      addThread(1);
#endif  // !THREADPOOL_AUTO_GROW
    _task_cv.notify_one();
  }

  //空闲线程数量
  int idlCount() {
    return _idlThrNum;
  }

  //线程数量
  int thrCount() {
    return _pool.size();
  }

#ifndef THREADPOOL_AUTO_GROW
 private:
#endif  // !THREADPOOL_AUTO_GROW \
        //添加指定数量的线程
  void addThread(unsigned short size) {
#ifdef THREADPOOL_AUTO_GROW
    if (!_run)  // stoped ??
      throw runtime_error("Grow on ThreadPool is stopped.");
    unique_lock<mutex> lockGrow{_lockGrow};  //自动增长锁
#endif                                       // !THREADPOOL_AUTO_GROW
    for (; _pool.size() < THREADPOOL_MAX_NUM && size > 0;
         --size) {  //增加线程数量,但不超过 预定义数量 THREADPOOL_MAX_NUM
      _pool.emplace_back([this] {  //工作线程函数
        while (true)  //防止 _run==false 时立即结束,此时任务队列可能不为空
        {
          Task task;  // 获取一个待执行的 task
          {
            // unique_lock 相比 lock_guard 的好处是：可以随时 unlock() 和 lock()
            unique_lock<mutex> lock{_lock};
            _task_cv.wait(lock, [this] {  // wait 直到有 task, 或需要停止
              return !_run || !_tasks.empty();
            });
            if (!_run && _tasks.empty())
              return;
            _idlThrNum--;
            task = move(_tasks.front());  // 按先进先出从队列取一个 task
            _tasks.pop();
          }
          task();  //执行任务
#ifdef THREADPOOL_AUTO_GROW
          if (_idlThrNum > 0 &&
              _pool.size() >
                  _initSize)  //支持自动释放空闲线程,避免峰值过后大量空闲线程
            return;
#endif  // !THREADPOOL_AUTO_GROW
          {
            unique_lock<mutex> lock{_lock};
            _idlThrNum++;
          }
        }
      });
      {
        unique_lock<mutex> lock{_lock};
        _idlThrNum++;
      }
    }
  }
};

int main() {
  threadpool pool(4);
  auto func = [](int test) {
    cout << test << " | " << endl;
    return 0;
  };
  vector<int> a(4, 0);
  auto fs = pool.map_container(func, a);
  for (auto&& f : fs) {
    f.get();
  }
  return 0;
}
