#include <atomic>
#include <libaio.h>
#include <list>
#include <string>
#include <thread>

struct aio_t {
  struct iocb cb;
  uint64_t offset;
  uint64_t length;
  void *ioc;

  aio_t(iocb cb, uint64_t off, uint64_t len, void *ioc)
      : cb(cb), offset(off), length(len), ioc(ioc) {}
};

struct IOContext {
  std::list<aio_t> pending_aios;
  std::list<aio_t> running_aios;
  std::atomic<int> num_pending = {0};
  std::atomic<int> num_running = {0};
};

class KernelDevice {
private:
  int fd;
  uint64_t size;
  uint64_t block_size;

  int bdev_aio_max_queue_depth;
  int bdev_aio_reap_max;
  int bdev_aio_poll_ms;
  io_context_t io_ctx;

  bool aio_stop;

  std::string path;
  std::string device_name;

public:
  KernelDevice();
  ~KernelDevice();

  int get_aio_max_queue_depth() { return bdev_aio_max_queue_depth; };

  int open(const std::string &p);
  int aio_write(uint64_t off, uint64_t len, char *buf, IOContext *ioc);
  int aio_read(uint64_t off, uint64_t len, char *buf, IOContext *ioc);
  void aio_submit(IOContext *ioc);
  int aio_get_completed();
};