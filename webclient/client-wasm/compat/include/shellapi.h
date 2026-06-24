#pragma once
#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

void* ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, int nShowCmd);
void* ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, int nShowCmd);

#ifdef __cplusplus
}
#endif

#ifndef ShellExecute
#define ShellExecute ShellExecuteA
#endif
