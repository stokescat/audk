#ifndef __EFIZZER_LIB_H__
#define __EFIZZER_LIB_H__

#include <Uefi.h>
#include <Protocol/DevicePath.h>

#define EFIZZER_ENABLED 1

//
// При сборке подразумевается, что заданы макросы EFIZZER_ENABLED
// и EFIZZER_BASEADDR
//

#ifdef EFIZZER_ENABLED

#ifndef EFIZZER_BASEADDR
#define EFIZZER_BASEADDR 0xFEB00000ULL
#endif

#define EFIZZER_EMIT_MODULE_EVENT(guid, size, addr) EfizzerEmitModuleEvent((guid), (size), (addr));
#define EFIZZER_EMIT_MODULE_EVENT_BY_DEVICE_PATH(fpath, size, addr) EfizzerEmitModuleEventByDevPath((fpath), (size), (addr));

#define EFIZZER_IGNORE __attribute__((no_sanitize("coverage")))

#define EFIZZER_PROTECT_DEVICE()                                         \
    Status = gDS->AddMemorySpace(                                        \
                    EfiGcdMemoryTypeMemoryMappedIo,                      \
                    EFIZZER_BASEADDR,                                    \
                    EFI_PAGE_SIZE,                                       \
                    EFI_MEMORY_UC                                        \
                    );                                                   \
    if (Status == EFI_ACCESS_DENIED) {                                   \
      Status = gDS->SetMemorySpaceAttributes(                            \
                      EFIZZER_BASEADDR,                                  \
                      EFI_PAGE_SIZE,                                     \
                      EFI_MEMORY_UC                                      \
                      );                                                 \
    }                                                                    \
    ASSERT_EFI_ERROR(Status);                                            \

#else

#define EFIZZER_EMIT_MODULE_EVENT(guid, size, addr)
#define EFIZZER_EMIT_MODULE_EVENT_BY_DEVICE_PATH(fpath, size, addr)

#define EFIZZER_IGNORE

#define EFIZZER_PROTECT_DEVICE()

#endif

extern EFI_GUID gEfizzerNotFvModuleGuid;
extern EFI_GUID gEfizzerUndefinedGuidGuid;

#define EFIZZER_REG_HIT         0
#define EFIZZER_REG_MODGUID_LO  1
#define EFIZZER_REG_MODGUID_HI  2
#define EFIZZER_REG_MODSIZE     3
#define EFIZZER_REG_MODADDR     4

#define EFIZZER_REG_CMD         7
#define EFIZZER_REG_BUF         8

#define EFIZZER_REG_BUF_MAX     504
#define EFIZZER_REG_BUF_MAXSIZE 4032

#define EFIZZER_EXEC_RDY_EVENT (0x00)
#define EFIZZER_EXEC_RUN_EVENT (0x01)
#define EFIZZER_EXEC_ERR_EVENT (0x0E)
#define EFIZZER_EXEC_FIN_EVENT (0x0F)

#define EFIZZER_MONITOR_START_EVENT (0x01)
#define EFIZZER_MONITOR_CHUNK_EVENT (0x02)
#define EFIZZER_MONITOR_STOP_EVENT  (0x0F)

typedef struct {
  UINT64    TotalSize;
  UINT64    ChunkCount;
  UINT64    FnvChecksum;
} EFIZZER_PROGINFO;

VOID
EFIAPI
__sanitizer_cov_trace_pc (
  VOID
  );


VOID
EFIAPI
EfizzerEmitModuleEvent (
  IN CONST EFI_GUID   *ModGuid,
  IN UINT64           ModSize,
  IN CONST VOID       *ModAddr
  );

VOID
EFIAPI
EfizzerEmitModuleEventByDevPath (
  IN CONST EFI_DEVICE_PATH_PROTOCOL   *ModFilePath,
  IN UINT64                           ModSize,
  IN CONST VOID                       *ModAddr
  );

VOID
EFIAPI
EfizzerExecEmitReadyEvent(
  VOID
  );

VOID
EFIAPI
EfizzerExecEmitStartEvent(
  VOID
  );

VOID
EFIAPI
EfizzerExecEmitErrorEvent(
  VOID
  );

VOID
EFIAPI
EfizzerExecEmitStopEvent(
  VOID
  );

EFI_STATUS
EFIAPI
EfizzerExecWaitProg(
  IN OUT VOID   *Buffer,
  IN OUT UINTN  *Size,
  IN     UINTN  MaxSize
  );

#endif
