#define HAL_STORAGE_IMPL
#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Board.h>
#include <Logging.h>
#include <SdFat.h>

#include <cassert>

namespace {
constexpr uint32_t SD_SPI_FREQUENCY = 40000000;
SdFat sd;

bool openFileForReadUnlocked(const char* moduleName, const char* path, FsFile& file) {
  if (!sd.exists(path)) {
    LOG_ERR(moduleName, "File does not exist: %s", path);
    return false;
  }

  file = sd.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR(moduleName, "Failed to open file for reading: %s", path);
    return false;
  }
  return true;
}

bool openFileForWriteUnlocked(const char* moduleName, const char* path, FsFile& file) {
  file = sd.open(path, O_RDWR | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR(moduleName, "Failed to open file for writing: %s", path);
    return false;
  }
  return true;
}

bool removeDirUnlocked(const char* path) {
  auto dir = sd.open(path);
  if (!dir) {
    return false;
  }
  if (!dir.isDirectory()) {
    dir.close();
    return false;
  }

  auto file = dir.openNextFile();
  char name[128];
  while (file) {
    String filePath = path;
    if (!filePath.endsWith("/")) {
      filePath += "/";
    }
    file.getName(name, sizeof(name));
    filePath += name;

    const bool ok = file.isDirectory() ? removeDirUnlocked(filePath.c_str()) : sd.remove(filePath.c_str());
    file.close();
    if (!ok) {
      dir.close();
      return false;
    }
    file = dir.openNextFile();
  }

  dir.close();
  return sd.rmdir(path);
}
}  // namespace

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  storageMutex = xSemaphoreCreateMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() {
  Board::prepareSdBus();
  initialized = sd.begin(BoardPins::SdCs, SD_SPI_FREQUENCY);
  if (initialized) {
    LOG_INF("SD", "SD card detected");
  } else {
    LOG_ERR("SD", "SD card not detected");
  }
  return initialized;
}

bool HalStorage::ready() const { return initialized; }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTake(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGive(HalStorage::getInstance().storageMutex); }
};

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  StorageLock lock;
  std::vector<String> ret;
  if (!initialized) {
    LOG_ERR("SD", "Not initialized, returning empty file list");
    return ret;
  }

  auto root = sd.open(path);
  if (!root) {
    LOG_ERR("SD", "Failed to open directory: %s", path);
    return ret;
  }
  if (!root.isDirectory()) {
    LOG_ERR("SD", "Path is not a directory: %s", path);
    root.close();
    return ret;
  }

  int count = 0;
  char name[128];
  while (count < maxFiles) {
    auto f = root.openNextFile();
    if (!f) {
      break;
    }
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    f.getName(name, sizeof(name));
    ret.emplace_back(name);
    f.close();
    count++;
  }
  root.close();
  return ret;
}

String HalStorage::readFile(const char* path) {
  StorageLock lock;
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot read file: %s", path);
    return "";
  }

  FsFile f;
  if (!openFileForReadUnlocked("SD", path, f)) {
    return "";
  }

  String content;
  constexpr size_t maxSize = 50000;
  size_t readSize = 0;
  while (f.available() && readSize < maxSize) {
    content += static_cast<char>(f.read());
    readSize++;
  }
  f.close();
  return content;
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  StorageLock lock;
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot stream file: %s", path);
    return false;
  }

  FsFile f;
  if (!openFileForReadUnlocked("SD", path, f)) {
    return false;
  }

  constexpr size_t localBufSize = 256;
  uint8_t buf[localBufSize];
  const size_t toRead = (chunkSize == 0) ? localBufSize : (chunkSize < localBufSize ? chunkSize : localBufSize);

  while (f.available()) {
    const int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, static_cast<size_t>(r));
    } else {
      break;
    }
  }

  f.close();
  return true;
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  StorageLock lock;
  if (!buffer || bufferSize == 0) {
    return 0;
  }
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot read file to buffer: %s", path);
    buffer[0] = '\0';
    return 0;
  }

  FsFile f;
  if (!openFileForReadUnlocked("SD", path, f)) {
    buffer[0] = '\0';
    return 0;
  }

  const size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : min(maxBytes, bufferSize - 1);
  size_t total = 0;

  while (f.available() && total < maxToRead) {
    constexpr size_t chunk = 64;
    const size_t want = maxToRead - total;
    const size_t readLen = (want < chunk) ? want : chunk;
    const int r = f.read(buffer + total, readLen);
    if (r > 0) {
      total += static_cast<size_t>(r);
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  f.close();
  return total;
}

bool HalStorage::writeFile(const char* path, const String& content) {
  StorageLock lock;
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot write file: %s", path);
    return false;
  }

  if (sd.exists(path)) {
    sd.remove(path);
  }

  FsFile f;
  if (!openFileForWriteUnlocked("SD", path, f)) {
    return false;
  }

  const size_t written = f.print(content);
  f.close();
  return written == content.length();
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  StorageLock lock;
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot create directory: %s", path);
    return false;
  }

  if (sd.exists(path)) {
    FsFile dir = sd.open(path);
    const bool isDirectory = dir && dir.isDirectory();
    dir.close();
    if (isDirectory) {
      return true;
    }
  }

  if (sd.mkdir(path)) {
    LOG_DBG("SD", "Created directory: %s", path);
    return true;
  }

  LOG_ERR("SD", "Failed to create directory: %s", path);
  return false;
}

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
  FsFile file;
};

HalFile::HalFile() = default;

HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

HalFile::~HalFile() = default;

HalFile::HalFile(HalFile&&) = default;

HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  return HalFile(std::make_unique<HalFile::Impl>(sd.open(path, oflag)));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  StorageLock lock;
  return sd.mkdir(path, pFlag);
}

bool HalStorage::exists(const char* path) {
  StorageLock lock;
  return sd.exists(path);
}

bool HalStorage::remove(const char* path) {
  StorageLock lock;
  return sd.remove(path);
}
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  StorageLock lock;
  return sd.rename(oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) {
  StorageLock lock;
  return sd.rmdir(path);
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = openFileForReadUnlocked(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = openFileForWriteUnlocked(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) {
  StorageLock lock;
  return removeDirUnlocked(path);
}

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe, no need to wrap
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, ); }
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  return HalFile(std::make_unique<Impl>(impl->file.openNextFile()));
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
