#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <string>

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/MANIFESTUAC:level='requireAdministrator'")
#pragma comment(lib, "Crypt32.lib")

int WINAPI WinMain(
    HINSTANCE,
    HINSTANCE,
    LPSTR,
    int
)
{
    const wchar_t* psCommand = LR"PS(
$ErrorActionPreference = 'Stop'

$LogonUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render' |
ForEach-Object {

    $fxPath = Join-Path $_.PsPath 'FxProperties'

    if (Test-Path $fxPath) {

        $relativePath = $_.Name.Substring(19) + '\FxProperties'

        $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
            $relativePath,
            [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
            [System.Security.AccessControl.RegistryRights]::ChangePermissions
        )

        if ($null -ne $key) {

            try {
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
            }
            finally {
                $key.Close()
            }

            Set-ItemProperty `
                -Path $fxPath `
                -Name '{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5' `
                -Type DWord `
                -Value 1 `
                -Force

            Set-ItemProperty `
                -Path $fxPath `
                -Name '{250e3ce7-95c2-46c7-8ea2-639b9f2040d2},0' `
                -Type DWord `
                -Value 1 `
                -Force
        }
    }
}

Restart-Service Audiosrv -Force
)PS";

    // Размер исходной PowerShell-команды в байтах
    DWORD inputLength =
        static_cast<DWORD>(wcslen(psCommand) * sizeof(wchar_t));

    // Узнаём необходимый размер Base64
    DWORD encodedLength = 0;

    if (!CryptBinaryToStringW(
        reinterpret_cast<const BYTE*>(psCommand),
        inputLength,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        nullptr,
        &encodedLength))
    {
        return 1;
    }

    std::wstring encodedCommand(encodedLength, L'\0');

    // Кодируем PowerShell в Base64
    if (!CryptBinaryToStringW(
        reinterpret_cast<const BYTE*>(psCommand),
        inputLength,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        &encodedCommand[0],
        &encodedLength))
    {
        return 1;
    }

    encodedCommand.resize(encodedLength);

    wchar_t powershellPath[MAX_PATH];

    GetSystemDirectoryW(
        powershellPath,
        MAX_PATH
    );

    lstrcatW(
        powershellPath,
        L"\\WindowsPowerShell\\v1.0\\powershell.exe"
    );

    std::wstring parameters =
        L"-NoProfile "
        L"-NonInteractive "
        L"-ExecutionPolicy Bypass "
        L"-WindowStyle Hidden "
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
        return 1;
    }

    if (sei.hProcess)
    {
        WaitForSingleObject(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
    }

    return 0;
}
