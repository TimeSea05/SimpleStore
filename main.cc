#include "KernelDevice.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>

int main() {
  KernelDevice *dev = new KernelDevice();
  dev->open("/dev/loop22");

  __attribute__((aligned(4096))) char wbuffer[4096];
  __attribute__((aligned(4096))) char rbuffer[4096];
  memset(wbuffer, 'b', 4096);
  IOContext *ioc = new IOContext();

  // read iolog file
  std::ifstream log_file("seq_rw.log");
  std::string line;
  int io_reqs = 0;

  while (std::getline(log_file, line)) {
    std::istringstream iss(line);
    std::string p1, p2, p3, p4;
    iss >> p1 >> p2 >> p3 >> p4;

    if (p2 != "read" && p2 != "write") {
      continue;
    }

    uint64_t off = std::stoull(p3);
    uint64_t len = std::stoull(p4);
    if (p2 == "read") {
      dev->aio_read(off, len, rbuffer, ioc);
    } else {
      dev->aio_write(off, len, wbuffer, ioc);
    }
    io_reqs++;

    if (io_reqs % 1024 == 0) {
      dev->aio_submit(ioc);
      int r, finished = 0;
      while ((r = dev->aio_get_completed()) != 0) {
        finished += r;
        if (finished == io_reqs) {
          break;
        }
      }
    }
  }

  return 0;
}