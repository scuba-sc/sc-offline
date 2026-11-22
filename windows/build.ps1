# SPDX-License-Identifier: GPL-3.0-only
# Build-time tooling only. Installed applications never invoke a script.
param([string]$RuntimeArchive, [switch]$RunTests)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$out=Join-Path $root 'out'
$obj=Join-Path $root 'obj'
New-Item -ItemType Directory -Force $out,$obj | Out-Null
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(-not(Test-Path $csc)){throw '.NET Framework 4.x x64 compiler is required.'}
$lock=Get-Content (Join-Path $root 'runtime-lock.json') -Raw | ConvertFrom-Json
if(-not $RuntimeArchive){
    $RuntimeArchive=Join-Path $obj ('runtime-'+$lock.version+'.zip')
    if(-not(Test-Path $RuntimeArchive)){Invoke-WebRequest -Uri $lock.url -OutFile $RuntimeArchive -UseBasicParsing}
}
if((Get-FileHash -LiteralPath $RuntimeArchive -Algorithm SHA256).Hash -ne $lock.sha256){throw 'Runtime archive checksum mismatch. Nothing will be packaged.'}
$stage=Join-Path $obj 'package'
if(Test-Path $stage){
    $resolved=[IO.Path]::GetFullPath($stage)
    if($resolved -ne [IO.Path]::GetFullPath((Join-Path $root 'obj\package'))){throw 'Invalid staging path.'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $stage | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$runtime=Join-Path $stage 'runtime'
New-Item -ItemType Directory -Path $runtime | Out-Null
$archive=[IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $RuntimeArchive))
try {
    $prefix='sc-offline-'+$lock.tag+'/'
    foreach($entry in $archive.Entries){
        if(-not $entry.FullName.StartsWith($prefix,[StringComparison]::Ordinal)){throw 'Unexpected runtime archive layout.'}
        $relative=$entry.FullName.Substring($prefix.Length)
        if(-not $relative){continue}
        if($relative.Split('/') -contains '..' -or $relative.Contains(':') -or $relative.Contains('\')){throw 'Unsafe runtime archive path.'}
        $destination=[IO.Path]::GetFullPath((Join-Path $runtime $relative))
        if(-not $destination.StartsWith($runtime+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Runtime file outside staging.'}
        if($entry.FullName.EndsWith('/')){New-Item -ItemType Directory -Path $destination -Force | Out-Null;continue}
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$destination,$false)
    }
} finally {$archive.Dispose()}
$manifest=Get-Content (Join-Path $runtime 'manifest.json') -Raw | ConvertFrom-Json
if($manifest.commit -ne $lock.source_commit -or $manifest.version -ne $lock.version){throw 'Runtime provenance mismatch.'}
foreach($entry in $manifest.files){
    $file=Join-Path $runtime $entry.path
    if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256){throw "Runtime manifest mismatch: $($entry.path)"}
}
$hashes=[ordered]@{}
Get-ChildItem $runtime -Recurse -File | Where-Object {$_.Extension -in '.exe','.dll'} | Sort-Object FullName | ForEach-Object {$hashes[$_.FullName.Substring($runtime.Length+1).Replace('\','/')]=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $stage 'runtime-hashes.json'),($hashes | ConvertTo-Json),$utf8)
Copy-Item (Join-Path $root 'runtime-lock.json') $stage
Copy-Item (Join-Path (Split-Path $root) 'LICENSE') $stage
Copy-Item (Join-Path $root 'README.md') (Join-Path $stage 'README.txt')

# Own, code-generated application icon; no game artwork or copied assets.
Add-Type -AssemblyName System.Drawing
$icon=Join-Path $obj 'app.ico'
$bitmap=New-Object Drawing.Bitmap 48,48
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([Drawing.Color]::FromArgb(15,22,34))
$graphics.SmoothingMode='AntiAlias'
$pen=New-Object Drawing.Pen ([Drawing.Color]::FromArgb(72,190,241)),3
$graphics.DrawEllipse($pen,5,5,37,37)
$brush=New-Object Drawing.SolidBrush ([Drawing.Color]::White)
$font=New-Object Drawing.Font 'Segoe UI',12,([Drawing.FontStyle]::Bold)
$graphics.DrawString('SC',$font,$brush,10,13)
$png=New-Object IO.MemoryStream
$bitmap.Save($png,[Drawing.Imaging.ImageFormat]::Png)
$stream=[IO.File]::Create($icon);$writer=New-Object IO.BinaryWriter $stream
$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]1)
$writer.Write([byte]48);$writer.Write([byte]48);$writer.Write([byte]0);$writer.Write([byte]0)
$writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$png.Length);$writer.Write([uint32]22);$writer.Write($png.ToArray());$writer.Dispose()
$font.Dispose();$pen.Dispose();$brush.Dispose();$graphics.Dispose();$bitmap.Dispose();$png.Dispose()
$refs='/r:System.dll','/r:System.Core.dll','/r:System.Drawing.dll','/r:System.Windows.Forms.dll','/r:System.Web.Extensions.dll','/r:System.ServiceProcess.dll','/r:Microsoft.CSharp.dll','/r:System.IO.Compression.dll','/r:System.IO.Compression.FileSystem.dll'
$sources=@(Get-ChildItem (Join-Path $root 'desktop') -Filter '*.cs' | ForEach-Object FullName)
$installer=Join-Path $root 'installer\Installer.cs'
$launcher=Join-Path $stage 'SCOfflineLauncher.exe'
& $csc /nologo /warnaserror+ /target:winexe /platform:x64 /optimize+ /main:ScOffline.Desktop.Program "/out:$launcher" "/win32icon:$icon" $refs $sources
if($LASTEXITCODE -ne 0){throw 'Desktop launcher build failed.'}
if($RunTests){
    $tests=Join-Path $obj 'DesktopTests.exe'
    & $csc /nologo /warnaserror+ /target:exe /platform:x64 /main:ScOffline.Tests.TestProgram "/out:$tests" $refs $sources $installer (Join-Path $root 'tests\Tests.cs')
    if($LASTEXITCODE -ne 0){throw 'Desktop test build failed.'}
    & $tests
    if($LASTEXITCODE -ne 0){throw 'Desktop tests failed.'}
}
$payload=Join-Path $obj 'payload.zip'
if(Test-Path $payload){Remove-Item -LiteralPath $payload}
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$payload)
$digest=Join-Path $obj 'payload.sha256'
[IO.File]::WriteAllText($digest,(Get-FileHash $payload).Hash.ToLowerInvariant(),$utf8)
$setup=Join-Path $out 'SCOfflineSetup.exe'
& $csc /nologo /warnaserror+ /target:winexe /platform:x64 /optimize+ /main:ScOffline.Setup.SetupProgram "/out:$setup" "/win32icon:$icon" "/resource:$payload,payload.zip" "/resource:$digest,payload.sha256" $refs $sources $installer
if($LASTEXITCODE -ne 0){throw 'Installer build failed.'}
Get-FileHash -LiteralPath $setup -Algorithm SHA256
Write-Output "Built $setup"
