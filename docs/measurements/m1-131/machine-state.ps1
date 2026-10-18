# The machine's state at the time of a measurement (M1-131 item 4).
# pwsh -NoProfile -File machine-state.ps1
"time: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')
$battery = Get-CimInstance -ClassName Win32_Battery
if ($battery) {
    # BatteryStatus 2 = on mains power ("the system has access to AC power"),
    # 1 = discharging (on battery); Win32_Battery documentation.
    $battery | ForEach-Object { "battery: status $($_.BatteryStatus), charge $($_.EstimatedChargeRemaining) %" }
} else { "battery: none reported" }
$plan = (powercfg /getactivescheme) -join ' '
"power scheme: $plan"
$os = Get-CimInstance Win32_OperatingSystem
"memory: total {0:N1} GiB, free {1:N1} GiB" -f ($os.TotalVisibleMemorySize / 1MB), ($os.FreePhysicalMemory / 1MB)
$disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='C:'"
"disk C: free {0:N2} GiB of {1:N2} GiB" -f ($disk.FreeSpace / 1GB), ($disk.Size / 1GB)
$cpu = Get-CimInstance Win32_Processor
"cpu: $($cpu.Name), load $($cpu.LoadPercentage) %"
"top 12 processes by working set:"
Get-Process | Sort-Object WorkingSet64 -Descending | Select-Object -First 12 |
    ForEach-Object { "  {0,-28} {1,8:N0} MiB  cpu {2,10:N1} s" -f $_.ProcessName, ($_.WorkingSet64 / 1MB), $_.CPU }
"build processes running:"
Get-Process -Name ninja, cmake, ctest, clang, clang++, clang-tidy, lld-link, python -ErrorAction SilentlyContinue |
    ForEach-Object { "  $($_.ProcessName) $($_.Id)" }
