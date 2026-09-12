#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <string>
#include <fstream>

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Shell32.lib")

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    const wchar_t* psCommand = LR"PS(
$ErrorActionPreference = 'Stop'

$log = 'D:\AudioFix\AudioFix.log'

try {

    Add-Content $log "=============================="
    Add-Content $log "AUDIO FIX START"
    Add-Content $log "=============================="
    Add-Content $log "USER: $([System.Security.Principal.WindowsIdentity]::GetCurrent().Name)"
    Add-Content $log "ADMIN: $(([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))"

    $LogonUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

    $devices = Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'

    Add-Content $log "DEVICES: $($devices.Count)"

    foreach ($device in $devices) {

        $fxPath = Join-Path $device.PsPath 'FxProperties'

        Add-Content $log "DEVICE: $($device.PSChildName)"
        Add-Content $log "FXPATH: $fxPath"

        if (Test-Path $fxPath) {

            $relativePath = $device.Name.Substring(19) + '\FxProperties'

            $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
                $relativePath,
                [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
                [System.Security.AccessControl.RegistryRights]::ChangePermissions
            )

            if ($null -ne $key) {

                $acl = $key.GetAccessControl()

                $rule = New-Object System.Security.AccessControl.RegistryAccessRule(
                    $LogonUser,
                    'FullControl',
                    'ContainerInherit,ObjectInherit',
                    'None',
                    'Allow'
                )

                $acl.ResetAccessRule($rule)
                $key.SetAccessControl($acl)
                $key.Close()

                Add-Content $log "PERMISSIONS OK"

                Set-ItemProperty `
                    -Path $fxPath `
                    -Name '{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5' `
                    -Type DWord `
                    -Value 1 `
                    -Force

                Add-Content $log "PARAMETER 1 OK"

                Set-ItemProperty `
                    -Path $fxPath `
                    -Name '{250e3ce7-95c2-46c7-8ea2-639b9f2040d2},0' `
                    -Type DWord `
                    -Value 1 `
                    -Force

                Add-Content $log "PARAMETER 2 OK"
            }
            else {
                Add-Content $log "ERROR: Registry key could not be opened"
            }
        }
    }

    Restart-Service Audiosrv -Force

    Add-Content $log "AUDIO SERVICE RESTART OK"
    Add-Content $log "AUDIO FIX FINISHED"
}
catch {
    Add-Content $log "ERROR:"
    Add-Content $log $_.Exception.ToString()
}
)PS";

    DWORD inputLength =
        static_cast<DWORD>(wcslen(psCommand) * sizeof(wchar_t));

    DWORD encodedLength = 0;

    if (!CryptBinaryToStringW(
        reinterpret_cast<const BYTE*>(psCommand),
        inputLength,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        nullptr,
        &encodedLength))
    {
        MessageBoxW(nullptr, L"Ошибка подготовки PowerShell.", L"AudioFix", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::wstring encodedCommand(encodedLength, L'\0');

    if (!CryptBinaryToStringW(
        reinterpret_cast<const BYTE*>(psCommand),
        inputLength,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        reinterpret_cast<LPWSTR>(&encodedCommand[0]),
        &encodedLength))
    {
        MessageBoxW(nullptr, L"Ошибка кодирования PowerShell.", L"AudioFix", MB_OK | MB_ICONERROR);
        return 1;
    }

    encodedCommand.resize(encodedLength);

    wchar_t powershellPath[MAX_PATH];

    GetSystemDirectoryW(powershellPath, MAX_PATH);

    lstrcatW(
        powershellPath,
        L"\\WindowsPowerShell\\v1.0\\powershell.exe"
    );

    std::wstring parameters =
        L"-NoProfile -NonInteractive -ExecutionPolicy Bypass "
        L"-EncodedCommand ";

    parameters += encodedCommand;

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = powershellPath;
    sei.lpParameters = parameters.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei))
    {
        MessageBoxW(
            nullptr,
            L"Не удалось запустить PowerShell с правами администратора.",
            L"AudioFix",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    if (sei.hProcess)
    {
        WaitForSingleObject(sei.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);

        CloseHandle(sei.hProcess);

        if (exitCode != 0)
        {
            MessageBoxW(
                nullptr,
                L"PowerShell завершился с ошибкой.\n\nПроверь D:\\AudioFix\\AudioFix.log",
                L"AudioFix",
                MB_OK | MB_ICONERROR
            );
        }
    }

    return 0;
}
