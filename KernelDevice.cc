#include <cassert>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>

#include <blkid/blkid.h>

#include "KernelDevice.h"

// for ceph cluster default configurations
// see https://gist.github.com/ekuric/7a50ef515d4d1f6280541a92569b8a6b
KernelDevice::KernelDevice() {
  bdev_aio_max_queue_depth = 1024;
  bdev_aio_reap_max = 16;
  bdev_aio_poll_ms = 250;
}

KernelDevice::~KernelDevice() {
  io_destroy(io_ctx);
}

int KernelDevice::open(const std::string &p) {
  path = p;
  int r = 0;

  fd = ::open(p.c_str(), O_RDWR | O_DIRECT);
  if (fd < 0) {
    r = -errno;
    std::cout << "Failed to open " << p << ": " << strerror(errno) << std::endl;
    return r;
  }

  struct flock l;
  memset(&l, 0, sizeof(l));
  l.l_type = F_WRLCK;
  l.l_whence = SEEK_SET;
  r = ::fcntl(fd, F_SETLK, &l);
  if (r < 0) {
    r = -errno;
    std::cout << "Failed to lock " << p << ": " << strerror(errno) << std::endl;
    return r;
  }

  struct stat st;
  r = ::fstat(fd, &st);
  if (r < 0) {
    r = -errno;
    std::cout << "Failed to fstat " << strerror(errno) << std::endl;
  }
  block_size = st.st_blksize;
  size = st.st_size;

  char devname[4096];
  dev_t devid = st.st_rdev;
  char *t = blkid_devno_to_devname(devid);
  if (t == nullptr) {
    return -EINVAL;
  }
  free(t);
  dev_t diskdev;
  r = blkid_devno_to_wholedisk(devid, devname, 4096, &diskdev);
  if (r < 0) {
    return -EINVAL;
  }
  device_name = std::string(devname);
  std::cout << "Device Name: " << device_name << std::endl;

  r = io_setup(bdev_aio_max_queue_depth, &io_ctx);
  if (r < 0) {
    if (io_ctx) {
      io_destroy(io_ctx);
      io_ctx = 0;
    }
    std::cout << "Failed to io_setup: " << strerror(errno) << std::endl;
  }

  aio_stop = false;
  return 0;
}

int KernelDevice::aio_write(uint64_t off, uint64_t len, char *buf,
                            IOContext *ioc) {
  struct iocb iocb;
  io_prep_pwrite(&iocb, fd, buf, len, off);
  ioc->pending_aios.push_back(aio_t{iocb, off, len, ioc});
  ++ioc->num_pending;

  return 0;
}

int KernelDevice::aio_read(uint64_t off, uint64_t len, char *buf,
                           IOContext *ioc) {
  struct iocb iocb;
  io_prep_pread(&iocb, fd, buf, len, off);
  ioc->pending_aios.push_back(aio_t{iocb, off, len, ioc});
  ++ioc->num_pending;

  return 0;
}

void KernelDevice::aio_submit(IOContext *ioc) {
  if (ioc->num_pending.load() == 0) {
    return;
  }

  std::list<aio_t>::iterator it = ioc->running_aios.begin();
  ioc->running_aios.splice(it, ioc->pending_aios);

  int pending = ioc->num_pending.load();
  ioc->num_running += pending;
  ioc->num_pending -= pending;

  assert(ioc->num_pending.load() == 0);
  assert(ioc->pending_aios.size() == 0);

  struct iocb *piocb[ioc->num_running.load()];
  int left = 0, done = 0;
  for (auto it = ioc->running_aios.begin(); it != ioc->running_aios.end();
       it++, left++) {
    piocb[left] = &it->cb;
  }

  int attempts = 0, delay = 125;
  while (left > 0) {
    int r = io_submit(io_ctx, left, piocb + done);
    if (r < 0) {
      if (r == -EAGAIN && attempts-- > 0) {
        usleep(delay);
        delay *= 2;
        continue;
      }
      assert(0 == "unknown error from io_submit");
    }
    assert(r > 0);
    done += r;
    left -= r;
  }
}

int KernelDevice::aio_get_completed() {
  io_event events[bdev_aio_reap_max];
  struct timespec t = {bdev_aio_poll_ms / 1000,
                       (bdev_aio_poll_ms % 1000) * 1000000};

  int r = 0;
  do {
    r = io_getevents(io_ctx, 1, bdev_aio_reap_max, events, &t);
  } while (r == -EINTR);

  if (r < 0) {
    std::cout << "io_getevents got " << strerror(r) << std::endl;
    assert(0 == "got unexpected error from io_getevents");
  } else {
    for (int i = 0; i < r; i++) {
      aio_t *aio_item = (aio_t *)(events[i].obj);
      std::string op =
          (aio_item->cb.aio_lio_opcode == IO_CMD_PREAD) ? "read" : "write";
      std::cout << "aio finished. " << op << " at " << aio_item->offset
                << ", length " << aio_item->length << std::endl;

      IOContext *ioc = static_cast<IOContext *>(aio_item->ioc);
      ioc->num_running--;
      if (ioc->num_running.load() == 0) {
        ioc->running_aios.clear();
      }
    }
  }

  return r;
}
