# Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

param(
    [int]$DurationSeconds = 3600
)

$RuleNameTcp = "TEMP_ALLOW_TCP_RANGE"
$RuleNameUdp = "TEMP_ALLOW_UDP_9000"

$TcpPorts = @("9001", "9002", "15000-16000")
$UdpPort  = "9000"

Write-Host "Creating temporary firewall rules..."

# 기존 룰 제거
Get-NetFirewallRule -DisplayName $RuleNameTcp -ErrorAction SilentlyContinue | Remove-NetFirewallRule
Get-NetFirewallRule -DisplayName $RuleNameUdp -ErrorAction SilentlyContinue | Remove-NetFirewallRule

# TCP 인바운드 허용
New-NetFirewallRule `
    -DisplayName $RuleNameTcp `
    -Direction Inbound `
    -Protocol TCP `
    -LocalPort $TcpPorts `
    -Action Allow `
    -Profile Any

# UDP 9000 인바운드 허용
New-NetFirewallRule `
    -DisplayName $RuleNameUdp `
    -Direction Inbound `
    -Protocol UDP `
    -LocalPort $UdpPort `
    -Action Allow `
    -Profile Any

Write-Host "Allowed TCP ports: 9001, 9002, 15000-16000"
Write-Host "Allowed UDP port : 9000"
Write-Host "Duration: $DurationSeconds seconds"
Write-Host "Waiting..."

Start-Sleep -Seconds $DurationSeconds

Write-Host "Removing firewall rules..."

Get-NetFirewallRule -DisplayName $RuleNameTcp -ErrorAction SilentlyContinue | Remove-NetFirewallRule
Get-NetFirewallRule -DisplayName $RuleNameUdp -ErrorAction SilentlyContinue | Remove-NetFirewallRule

Write-Host "Done."