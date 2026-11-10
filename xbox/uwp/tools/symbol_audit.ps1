# Lists the external symbols the frontend objects need and nobody defines (frontend + uwp\bridge objects).
# Needs the objects: tmp\objall (check_all_c.cmd) and tmp\obj (check_cpp.cmd for every bridge file).
param([string]$FrontObjs = 'E:\claude-work\tmp\objall', [string]$BridgeObjs = 'E:\claude-work\tmp\obj')
$dumpbin = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
$defined = New-Object 'System.Collections.Generic.HashSet[string]'
$undef = @{}
foreach ($dir in $FrontObjs, $BridgeObjs) {
    foreach ($o in Get-ChildItem $dir -Filter *.obj) {
        $isBridge = ($dir -eq $BridgeObjs)
        foreach ($line in (& $dumpbin /NOLOGO /SYMBOLS $o.FullName)) {
            if ($line -notmatch '\| ') { continue }
            if ($line -notmatch ' External ') { continue }
            $name = ($line -split '\| ', 2)[1].Trim()
            $name = ($name -split ' ')[0]
            if ($line -match ' UNDEF ') {
                if (-not $undef.ContainsKey($name)) { $undef[$name] = New-Object System.Collections.Generic.List[string] }
                $undef[$name].Add($o.BaseName)
            } else {
                [void]$defined.Add($name)
            }
        }
    }
}
$missing = foreach ($k in ($undef.Keys | Sort-Object)) {
    if ($defined.Contains($k)) { continue }
    if ($k.StartsWith('__imp_') -or $k.StartsWith('__') -or $k.StartsWith('?') -or $k.StartsWith('_RTC')) { continue }
    $k + '   <- ' + (($undef[$k] | Select-Object -Unique) -join ', ')
}
$missing
