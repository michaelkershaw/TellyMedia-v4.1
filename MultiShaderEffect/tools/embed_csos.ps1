param(
  [Parameter(Mandatory=$true)][string]$InputDir,
  [Parameter(Mandatory=$true)][string]$OutputHeader
)

if (!(Test-Path $InputDir)) { Write-Error "InputDir not found: $InputDir"; exit 1 }

$files = Get-ChildItem -LiteralPath $InputDir -Filter *.cso -ErrorAction Stop
if ($files.Count -eq 0) { Write-Error "No .cso files found in $InputDir"; exit 1 }

$sb = New-Object System.Text.StringBuilder
$null = $sb.AppendLine("#pragma once")
$null = $sb.AppendLine("// Auto-generated from .cso files. Do not edit manually.")
$null = $sb.AppendLine("#include <cstdint>")
$null = $sb.AppendLine("#include <unordered_map>")
$null = $sb.AppendLine("#include <string>")
$null = $sb.AppendLine("")

$null = $sb.AppendLine("namespace EmbeddedShaders {")

foreach ($f in $files) {
  $name = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
  $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
  $arr = ($bytes | ForEach-Object { "0x{0:X2}" -f $_ }) -join ","
  $null = $sb.AppendLine("static const uint8_t ${name}_data[] = { $arr };")
  $null = $sb.AppendLine("static const size_t ${name}_size = sizeof(${name}_data);")
  $null = $sb.AppendLine("")
}

$null = $sb.AppendLine("inline const std::unordered_map<std::string, std::pair<const uint8_t*, size_t>>& Map() {")
$null = $sb.AppendLine("  static std::unordered_map<std::string, std::pair<const uint8_t*, size_t>> m = {")

$entries = @()
foreach ($f in $files) {
  $name = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
  $entries += "    { `"$name`", { ${name}_data, ${name}_size } }"
}
$null = $sb.AppendLine(($entries -join ",`n"))

$null = $sb.AppendLine("  };")
$null = $sb.AppendLine("  return m;")
$null = $sb.AppendLine("}")
$null = $sb.AppendLine("} // namespace EmbeddedShaders")

[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($OutputHeader)) | Out-Null
[System.IO.File]::WriteAllText($OutputHeader, $sb.ToString())
Write-Host "Generated $OutputHeader with $($files.Count) shader(s)."
