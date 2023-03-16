/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include <atomic>
#include <mutex>
#include <string>

#include <windows.h>

#include <cm3p/cppdap/io.h>

#include "cmsys/Encoding.hxx"

namespace cmDebugger {

class cmDebuggerNamedPipe : public dap::ReaderWriter
{
  std::mutex ReadMutex;
  std::mutex WriteMutex;
  std::atomic<bool> closed = { false };
  HANDLE Pipe;
  OVERLAPPED ReadOverlap;
  OVERLAPPED WriteOverlap;

public:
  cmDebuggerNamedPipe(std::string const& name)
  {
    Pipe = CreateFileW(cmsys::Encoding::ToWide(name.c_str()).c_str(),
                       GENERIC_READ | GENERIC_WRITE,
                       0,                    // no sharing
                       NULL,                 // default security attributes
                       OPEN_EXISTING,        // pipe exists
                       FILE_FLAG_OVERLAPPED, // default attributes
                       NULL                  // no template file
    );

    ZeroMemory(&ReadOverlap, sizeof(OVERLAPPED));
    ZeroMemory(&WriteOverlap, sizeof(OVERLAPPED));

    if (Pipe == INVALID_HANDLE_VALUE) {
      closed.store(true);
      return;
    }

    ReadOverlap.hEvent = CreateEvent(NULL,   // default security attribute
                                     TRUE,   // manual-reset event
                                     FALSE,  // initial state
                                     NULL);  // unnamed event object
    WriteOverlap.hEvent = CreateEvent(NULL,  // default security attribute
                                      TRUE,  // manual-reset event
                                      FALSE, // initial state
                                      NULL); // unnamed event object
  }

  cmDebuggerNamedPipe(HANDLE pipe)
    : Pipe(pipe)
  {
    ZeroMemory(&ReadOverlap, sizeof(OVERLAPPED));
    ReadOverlap.hEvent = CreateEvent(NULL,  // default security attribute
                                     TRUE,  // manual-reset event
                                     FALSE, // initial state
                                     NULL); // unnamed event object
    ZeroMemory(&WriteOverlap, sizeof(OVERLAPPED));
    WriteOverlap.hEvent = CreateEvent(NULL,  // default security attribute
                                      TRUE,  // manual-reset event
                                      FALSE, // initial state
                                      NULL); // unnamed event object
  }

  ~cmDebuggerNamedPipe() { close(); }

  bool isOpen() override { return !closed; }

  void close() override
  {
    if (!closed.exchange(true)) {
      CloseHandle(Pipe);
      CloseHandle(ReadOverlap.hEvent);
      CloseHandle(WriteOverlap.hEvent);
    }
  }

  size_t read(void* buffer, size_t n) override
  {
    std::unique_lock<std::mutex> lock(ReadMutex);
    auto out = reinterpret_cast<char*>(buffer);
    for (size_t i = 0; i < n; i++) {
      DWORD bytesRead;
      BOOL success =
        ReadFile(Pipe, &out[i], sizeof(out[i]), &bytesRead, &ReadOverlap);
      if (!success && GetLastError() != ERROR_IO_PENDING) {
        return i;
      }

      success = GetOverlappedResult(Pipe, &ReadOverlap, &bytesRead, TRUE);

      if (!success) {
        return i;
      }
    }

    return n;
  }

  bool write(const void* buffer, size_t n) override
  {
    std::unique_lock<std::mutex> lock(WriteMutex);
    DWORD totalBytesWritten = 0;
    DWORD bytesWritten;
    while (totalBytesWritten < n) {
      BOOL success =
        WriteFile(Pipe, &(static_cast<const char*>(buffer)[totalBytesWritten]),
                  n, &bytesWritten, &WriteOverlap);
      if (!success && GetLastError() != ERROR_IO_PENDING) {
        return false;
      }

      // Wait for the write to complete
      success = GetOverlappedResult(Pipe, &WriteOverlap, &bytesWritten, TRUE);
      if (!success) {
        return false;
      }
      totalBytesWritten += bytesWritten;
    }

    return true;
  }
};


std::shared_ptr<dap::ReaderWriter> CreateDebuggerNamedPipe(std::string const& name)
{
  return std::make_shared<cmDebuggerNamedPipe>(name);
}

std::shared_ptr<dap::ReaderWriter> CreateDebuggerNamedPipe(HANDLE pipe)
{
  return std::make_shared<cmDebuggerNamedPipe>(pipe);
}

} // namespace cmDebugger
