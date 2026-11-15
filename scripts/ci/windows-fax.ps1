param([string]$Destination = 'C:/stel-deps')
$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Checked([scriptblock]$Command) { & $Command; if ($LASTEXITCODE) { throw "Command failed: $Command" } }
Checked { python "$Project/scripts/fax-sources.py" --work "$Destination/src/fax" spandsp libtiff }
$TiffSource = "$Destination/src/fax/libtiff-v4.7.0"
$SpanSource = "$Destination/src/fax/spandsp-0.0.6+dfsg"
Checked { cmake -S $TiffSource -B "$Destination/build-tiff" -G Ninja `
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$Destination/fax" -DBUILD_SHARED_LIBS=OFF `
    -Dtiff-tools=OFF -Dtiff-tests=OFF -Dtiff-contrib=OFF -Dtiff-docs=OFF `
    -Dzlib=OFF -Dlibdeflate=OFF -Djpeg=OFF -Djbig=OFF -Dlerc=OFF -Dlzma=OFF -Dzstd=OFF -Dwebp=OFF }
Checked { cmake --build "$Destination/build-tiff" --target install --parallel 4 }
Checked { cmake -S "$Project/cmake/FaxSpandsp" -B "$Destination/build-spandsp" -G Ninja `
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$Destination/fax" `
    "-DCMAKE_PREFIX_PATH=$Destination/fax" "-DSPANDSP_SOURCE=$SpanSource" }
Checked { cmake --build "$Destination/build-spandsp" --target install --parallel 4 }
Remove-Item "$Destination/build-tiff", "$Destination/build-spandsp" -Recurse -Force
