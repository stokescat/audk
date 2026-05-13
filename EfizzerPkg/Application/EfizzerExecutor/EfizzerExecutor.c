#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/EfizzerLib.h>

//
// 16 Mb - максимальный размер программы
//
#define MAX_PROG_SIZE (16*1024*1024)

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  ProgBufferStart;
  EFI_HANDLE            ProgHandle;
  VOID                  *ProgBuffer = NULL;
  UINTN                 ProgSize = 0;

  Print(L"[[efizzer]]: executor starting...\n");

  //
  // Надо отключить сторожевой таймер
  // не думал, что он вообще есть в EFI, лол
  // этот таймер нарушит цикл фаззинга без причины перезагрузив QEMU
  // а manager ничего не поймет, потому что перезагрузка qemu не
  // разрывает соединение сокета
  //
  gBS->SetWatchdogTimer(0, 0, 0, NULL);

  //
  // Выделяем память для будущих программ
  //
  Status = gBS->AllocatePages(
              AllocateAnyPages,
              EfiLoaderCode,
              EFI_SIZE_TO_PAGES(MAX_PROG_SIZE),
              &ProgBufferStart
            );
  if (EFI_ERROR(Status)) {
    Print(L"[[efizzer]]: executor failed!\n");
    return Status;
  }
  ProgBuffer = (VOID*)ProgBufferStart;

  Print(L"[[efizzer]]: executor success init!\n");

  Print(L"[[efizzer]]: ready to get progs\n");
  EfizzerExecEmitReadyEvent();

  while (1) {

    Status = EfizzerExecWaitProg(
              ProgBuffer,
              &ProgSize,
              MAX_PROG_SIZE
              );

    if (EFI_ERROR(Status)) {
      Print(L"[[efizzer]]: failed to get new prog\n");
      EfizzerExecEmitErrorEvent();
      continue;
    }
    Print(L"[[efizzer]]: success got new prog. Load it...\n");

    //
    // Получили новую программу, загружаем ее в прошивку
    //
    Status = gBS->LoadImage(
              FALSE,
              ImageHandle,
              NULL,
              ProgBuffer,
              ProgSize,
              &ProgHandle
              );

    if (EFI_ERROR(Status)) {
      Print(L"[[efizzer]]: failed to load prog\n");
      EfizzerExecEmitErrorEvent();
      continue;
    }

    Print(L"[[efizzer]]: load prog success. Run it!\n");
    EfizzerExecEmitStartEvent();
    Status = gBS->StartImage(ProgHandle, NULL, NULL);
    Print(L"[[efizzer]]: prog success exited with status: %r\n", Status);
    EfizzerExecEmitStopEvent();

    gBS->UnloadImage(ProgHandle);
  }

  Status = gBS->FreePages(ProgBufferStart, EFI_SIZE_TO_PAGES(MAX_PROG_SIZE));
  return EFI_SUCCESS;
}
