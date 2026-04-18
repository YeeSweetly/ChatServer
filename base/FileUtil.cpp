// @Author Lin Ya
// @Email xxbbb@vip.qq.com
#include "FileUtil.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

AppendFile::AppendFile(string filename) : fp_(fopen(filename.c_str(), "ae")) {
  if (fp_) {
    setbuffer(fp_, buffer_, sizeof buffer_);
  } else {
    // 打开失败时输出错误信息，避免后续空指针崩溃
    fprintf(stderr, "AppendFile: failed to open log file '%s'\n", filename.c_str());
  }
}

AppendFile::~AppendFile() {
  if (fp_) fclose(fp_);
}

void AppendFile::append(const char* logline, const size_t len) {
  if (!fp_) return;                     // 新增保护
  size_t n = this->write(logline, len);
  size_t remain = len - n;
  while (remain > 0) {
    size_t x = this->write(logline + n, remain);
    if (x == 0) {
      int err = ferror(fp_);
      if (err) fprintf(stderr, "AppendFile::append() failed !\n");
      break;
    }
    n += x;
    remain = len - n;
  }
}

void AppendFile::flush() {
  if (fp_) fflush(fp_);                 // 新增保护
}

size_t AppendFile::write(const char* logline, size_t len) {
  if (!fp_) return 0;
  return fwrite_unlocked(logline, 1, len, fp_);
}