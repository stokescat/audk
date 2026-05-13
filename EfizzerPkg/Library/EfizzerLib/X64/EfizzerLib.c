#include <Library/EfizzerLib.h>

#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>

STATIC volatile UINT64 *mEfizzerMmioReg = (volatile UINT64*)EFIZZER_BASEADDR;

//
// gEfizzerNotFvModuleGuid = 500DF8D1-CA05-3042-959E-8872D94F6BC6
//
EFI_GUID gEfizzerNotFvModuleGuid = {0x500DF8D1,0xCA05,0x3042,{0x95,0x9E,0x88,0x72,0xD9,0x4F,0x6B,0xC6}};

//
// gEfizzerUndefinedGuidGuid = 336F5A24-E9F9-B643-972A-14C8738E3F5B
//
EFI_GUID gEfizzerUndefinedGuidGuid = {0x336F5A24,0xE9F9,0xB643,{0x97,0x2A,0x14,0xC8,0x73,0x8E,0x3F,0x5B}};

EFIZZER_IGNORE
VOID
EFIAPI
__sanitizer_cov_trace_pc(VOID)
{
  UINT64 ReturnAddr;

  ReturnAddr = (UINT64) __builtin_return_address(0);

  MemoryFence();
  mEfizzerMmioReg[EFIZZER_REG_HIT] = ReturnAddr;
  MemoryFence();
}

EFIZZER_IGNORE
VOID
EFIAPI
EfizzerEmitModuleEvent (
  IN CONST EFI_GUID   *ModGuid,
  IN UINT64           ModSize,
  IN CONST VOID       *ModAddr
  )
{
  CONST UINT64 *Guid;

  Guid = (CONST UINT64*)ModGuid;

  MemoryFence();
  mEfizzerMmioReg[EFIZZER_REG_MODGUID_LO] = Guid[0];
  mEfizzerMmioReg[EFIZZER_REG_MODGUID_HI] = Guid[1];
  mEfizzerMmioReg[EFIZZER_REG_MODSIZE] = ModSize;
  mEfizzerMmioReg[EFIZZER_REG_MODADDR] = (UINT64)ModAddr;
  MemoryFence();
}

EFIZZER_IGNORE
VOID
EFIAPI
EfizzerEmitModuleEventByDevPath (
  IN CONST EFI_DEVICE_PATH_PROTOCOL   *ModFilePath,
  IN UINT64                           ModSize,
  IN CONST VOID                       *ModAddr
  )
{
  CONST MEDIA_FW_VOL_FILEPATH_DEVICE_PATH   *FvFile     = NULL;
  CONST EFI_DEVICE_PATH_PROTOCOL            *PathNode   = ModFilePath;
  CONST EFI_GUID                            *Guid       = &gEfizzerNotFvModuleGuid;

  if (ModFilePath != NULL) {
    while (!IsDevicePathEnd(PathNode)) {

      if ((DevicePathType(PathNode)      == MEDIA_DEVICE_PATH) &&
          (DevicePathSubType(PathNode)   == MEDIA_PIWG_FW_FILE_DP)) {

          FvFile = (CONST MEDIA_FW_VOL_FILEPATH_DEVICE_PATH*)PathNode;
          Guid = &(FvFile->FvFileName);
          break;
      }

      PathNode = NextDevicePathNode(PathNode);
    }
  } else {

    Guid = &gEfizzerUndefinedGuidGuid;
  }

  EfizzerEmitModuleEvent(Guid, ModSize, ModAddr);
}

EFIZZER_IGNORE
STATIC
EFI_STATUS
EfizzerWrite(
  IN UINT64 Opt,
  IN UINTN  Len,
  IN VOID   *Buf OPTIONAL
  )
{
  UINT64  CmdValue = (Opt << 12) | (Len & 0x0FFFUL);
  UINTN   Offset;
  UINT64  Last;

  Len = Len & 0x0FFFUL;
  if (Len > EFIZZER_REG_BUF_MAXSIZE) {
    return EFI_BAD_BUFFER_SIZE;
  }

  //
  // Сначала нужно скопировать буффер, а только потом заполнять регистр CMD
  // потому что устройство триггерится на отправку только после записи в CMD
  //
  if ((Buf != NULL) && (Len > 0)) {
    //
    // Копируем данные
    //
    MemoryFence();
    for (Offset = 0; Offset + 8 <= Len; Offset+= 8) {
      mEfizzerMmioReg[EFIZZER_REG_BUF + Offset/8] = *((UINT64*)(Buf + Offset));
    }
    if (Offset < Len) {
      Last = 0;
      CopyMem((VOID*)&Last, Buf + Offset, Len - Offset);
      mEfizzerMmioReg[EFIZZER_REG_BUF + Offset/8] = Last;
    }
    MemoryFence();
  }

  //
  // Когда скопировали буфер, если он был, то тогда записываем CMD
  MemoryFence();
  mEfizzerMmioReg[EFIZZER_REG_CMD] = CmdValue;
  MemoryFence();

  return EFI_SUCCESS;
}

EFIZZER_IGNORE
STATIC
EFI_STATUS
EfizzerRead(
  OUT     UINT64 *Opt,
  IN OUT  UINTN  *Len,
  OUT     VOID   *Buf
  )
{
  UINT64 CmdValue = 0;
  UINT64 OptValue;
  UINT64 Value;
  UINTN  Size = 0;
  UINTN  Offset;

  //
  // Len должен быть заранее вместимым
  //
  if (*Len < EFIZZER_REG_BUF_MAXSIZE) {
    *Len = EFIZZER_REG_BUF_MAXSIZE;
    return EFI_BAD_BUFFER_SIZE;
  }

  //
  // Теперь сначала прочитаем из CMD, чтобы стриггерить устройство
  // читать из сокета. Чтение из сокета должно быть блокирующим.
  // но на всякий случай может вернуться ноль, как признак того, что
  // устройство ничего не делает
  //
  MemoryFence();
  while (CmdValue == 0) {
    CmdValue = mEfizzerMmioReg[EFIZZER_REG_CMD];
  }
  MemoryFence();

  // Когда командный регистр вернулся, то продолжаем теперь читать буффер
  Size      = (UINTN)(CmdValue & 0x0FFFUL);
  OptValue  = (CmdValue >> 12) & 0x0FUL;

  if (Size > 0) {
    // Есть какие-то данные, читаем
    MemoryFence();
      for (Offset = 0; Offset + 8 <= Size; Offset+=8) {
        Value = mEfizzerMmioReg[EFIZZER_REG_BUF + Offset/8];
        CopyMem(Buf+Offset, (VOID*)&Value, 8);
      }

      if (Offset < Size) {
        Value = mEfizzerMmioReg[EFIZZER_REG_BUF + Offset/8];
        CopyMem(Buf+Offset, &Value, Size - Offset);
      }
    MemoryFence();
  }

  //
  // Все скопировали, возвращаем
  //
  *Opt = OptValue;
  *Len = Size;
  return EFI_SUCCESS;
}

//
// FNV1-a 64 Hash для контрольной суммы
//
STATIC
UINT64
EfizzerFnv1a64(
  IN CONST VOID *Data,
  IN UINTN      Size
  )
{
  CONST UINT8 *Bytes = Data;
  UINT64 Hash = 0xCBF29CE484222325ULL;
  UINTN i;

  for (i = 0; i < Size; i++) {
    Hash ^= Bytes[i];
    Hash *= 0x100000001B3ULL;
  }
  return Hash;
}


EFIZZER_IGNORE
VOID
EFIAPI
EfizzerExecEmitReadyEvent(VOID)
{
  EfizzerWrite(EFIZZER_EXEC_RDY_EVENT, 0, NULL);
}

EFIZZER_IGNORE
VOID
EFIAPI
EfizzerExecEmitStartEvent(VOID)
{
  EfizzerWrite(EFIZZER_EXEC_RUN_EVENT, 0, NULL);
}

EFIZZER_IGNORE
VOID
EFIAPI
EfizzerExecEmitErrorEvent(VOID)
{
  EfizzerWrite(EFIZZER_EXEC_ERR_EVENT, 0, NULL);
}

EFIZZER_IGNORE
VOID
EFIAPI
EfizzerExecEmitStopEvent(VOID)
{
  EfizzerWrite(EFIZZER_EXEC_FIN_EVENT, 0, NULL);
}

#define EFIZZER_CHUNK_MAXSIZE 4096

EFI_STATUS
EFIAPI
EfizzerExecWaitProg(
  IN OUT VOID   *Buffer,
  IN OUT UINTN  *Size,
  IN     UINTN  MaxSize
  )
{
  EFIZZER_PROGINFO ProgInfo;
  STATIC UINT8      ChunkData[EFIZZER_CHUNK_MAXSIZE];
  VOID             *Data = (VOID*) ChunkData;
  UINT64           Opt = 0;
  UINTN            DataLen = 0;
  UINT64           ChunkId;
  UINTN            Offset;
  UINT64           Checksum;

  //
  // Ожидаем начала транзакции
  //
  while (Opt != EFIZZER_MONITOR_START_EVENT) {
    Opt = 0;
    DataLen = EFIZZER_CHUNK_MAXSIZE;
    EfizzerRead(&Opt, &DataLen, Data);
  }


  if (DataLen != sizeof(EFIZZER_PROGINFO)) {
    return EFI_ABORTED;
  }

  //
  // Считываем информацию о передаваемой программе
  //
  ProgInfo.TotalSize   = ((UINT64*)Data)[0];
  ProgInfo.ChunkCount  = ((UINT64*)Data)[1];
  ProgInfo.FnvChecksum = ((UINT64*)Data)[2];


  if (ProgInfo.TotalSize > MaxSize) {
    return EFI_ABORTED;
  }

  //
  // Теперь считываем чанки
  //
  Offset = 0;
  for (ChunkId = 0; ChunkId < ProgInfo.ChunkCount; ChunkId++) {
    SetMem(Data, EFIZZER_CHUNK_MAXSIZE, 0);
    //
    // Считываем чанк
    //
    Opt = 0;
    DataLen = EFIZZER_CHUNK_MAXSIZE;
    EfizzerRead(&Opt, &DataLen, Data);
    if (Opt != EFIZZER_MONITOR_CHUNK_EVENT) {
      return EFI_ABORTED;
    }

    if (Offset + DataLen > ProgInfo.TotalSize) {
      return EFI_ABORTED;
    }

    //
    // Отлично, прочитали данные, теперь надо скопировать
    //
    CopyMem(Buffer + Offset, Data, DataLen);
    Offset+= DataLen;
  }

  //
  // Теперь надо прочитать последнее сообщение, которое завершает
  // транзакцию программы
  //
  Opt = 0;
  DataLen = EFIZZER_CHUNK_MAXSIZE;
  EfizzerRead(&Opt, &DataLen, Data);
  if (Opt != EFIZZER_MONITOR_STOP_EVENT) {
    return EFI_ABORTED;
  }

  *Size = Offset;

  //
  // Проверяем контрольную сумму
  //
  Checksum = EfizzerFnv1a64(Buffer, *Size);

  if (Checksum != ProgInfo.FnvChecksum) {
    return EFI_CRC_ERROR;
  }

  return EFI_SUCCESS;
}
