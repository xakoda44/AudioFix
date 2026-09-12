#include <windows.h>
#include <shellapi.h>
#include <string>

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/MANIFESTUAC:level='requireAdministrator'")

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

    // Получаем путь к PowerShell, встроенному в Windows
    wchar_t powershellPath[MAX_PATH];

    GetSystemDirectoryW(powershellPath, MAX_PATH);

    lstrcatW(
        powershellPath,
        L"\\WindowsPowerShell\\v1.0\\powershell.exe"
    );

    /*
        PowerShell -EncodedCommand требует Base64
        от UTF-16LE строки.
    */

    int byteCount = static_cast<int>(wcslen(psCommand) * sizeof(wchar_t));

    DWORD base64Length = 0;

    CryptStringToBinaryW(
        reinterpret_cast<const wchar_t*>(psCommand),
        byteCount,
        CRYPT_STRING_BASE64,
        nullptr,
        &base64Length,
        nullptr,
        nullptr
    );

    std::wstring encoded;
    encoded.resize(base64Length);

    CryptStringToBinaryW(
        reinterpret_cast<const wchar_t*>(psCommand),
        byteCount,
        CRYPT_STRING_BASE64,
        reinterpret_cast<BYTE*>(&encoded[0]),
        &base64Length,
        nullptr,
        nullptr
    );

    encoded.resize(base64Length);

    std::wstring params =
        L"-NoProfile -NonInteractive -ExecutionPolicy Bypass "
        L"-WindowStyle Hidden -EncodedCommand ";

    params += encoded;

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);

    // Запуск с повышенными правами
    sei.lpVerb = L"runas";

    sei.lpFile = powershellPath;
    sei.lpParameters = params.c_str();

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
