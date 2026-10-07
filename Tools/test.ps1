param(
    [string]$Compiler = 'gcc',
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('bus-scope-tests-' + [Guid]::NewGuid().ToString('N')))
)

$ErrorActionPreference = 'Stop'
$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if (-not $compilerCommand) {
    throw 'GCC was not found. Pass -Compiler with the path to gcc.exe.'
}
$compilerPath = $compilerCommand.Source
$projectRoot = Split-Path -Parent $PSScriptRoot
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

$cases = @(
    @{
        Name = 'acquisition'
        Sources = @('Tests/acquisition/test_bus_scope_acquisition.c')
        Includes = @('Tests/acquisition/stubs', 'App/Inc')
        Flags = @('-pedantic')
    },
    @{
        Name = 'output-hardware'
        Sources = @('Tests/output_hw/test_bus_scope_output_hw.c', 'App/bus_scope_output.c')
        Includes = @('Tests/output_hw/stubs', 'App/Inc')
        Flags = @('-pedantic')
    },
    @{
        Name = 'output'
        Sources = @('App/bus_scope_output.c', 'Tests/test_bus_scope_output.c')
        Includes = @('App/Inc')
        Flags = @('-Wconversion', '-pedantic')
    },
    @{
        Name = 'capture'
        Sources = @('App/bus_scope_capture.c', 'Tests/test_bus_scope_capture.c')
        Includes = @('App/Inc')
        Flags = @('-O2', '-Wconversion', '-pedantic')
    },
    @{
        Name = 'signal'
        Sources = @('App/bus_scope_signal.c', 'Tests/test_bus_scope_signal.c')
        Includes = @('App/Inc')
        Flags = @('-Wconversion', '-pedantic')
    },
    @{
        Name = 'usb'
        Sources = @('USB_DEVICE/App/usbd_cdc_if.c', 'Tests/usb/test_usbd_cdc_if.c')
        Includes = @('Tests/usb/stubs', 'USB_DEVICE/App')
        Flags = @('-pedantic')
    },
    @{
        Name = 'app'
        Sources = @('Tests/app/test_bus_scope_app.c', 'App/bus_scope_signal.c', 'App/bus_scope_output.c', 'App/bus_scope_capture.c')
        Includes = @('Tests/app/stubs', 'App/Inc')
        # LTO discards uncalled hardware functions from the production source.
        Flags = @('-O2', '-flto', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections')
    }
)

Push-Location $projectRoot
try {
    foreach ($case in $cases) {
        $executable = Join-Path $OutputDirectory ($case.Name + '.exe')
        $arguments = @('-std=c99', '-Wall', '-Wextra', '-Werror') + $case.Flags
        foreach ($include in $case.Includes) {
            $arguments += @('-I', $include)
        }
        $arguments += $case.Sources + @('-o', $executable)
        Write-Host ('Building {0} tests' -f $case.Name)
        & $compilerPath @arguments
        if ($LASTEXITCODE -ne 0) { throw ('{0} test compilation failed.' -f $case.Name) }
        & $executable
        if ($LASTEXITCODE -ne 0) { throw ('{0} tests failed.' -f $case.Name) }
    }
}
finally {
    Pop-Location
}
Write-Host ('All test suites passed. Executables: {0}' -f $OutputDirectory)
