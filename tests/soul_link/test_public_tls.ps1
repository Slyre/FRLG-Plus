# Requires PowerShell 7 (.NET certificate PEM export); no OpenSSL installation.
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskFixture = Join-Path $taskRoot ('build/tests/tls-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture -Force | Out-Null
$taskKey = [Security.Cryptography.RSA]::Create(2048)
try {
    $taskRequest = [Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=localhost', $taskKey, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $taskSan = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    $taskSan.AddDnsName('localhost')
    $taskRequest.CertificateExtensions.Add($taskSan.Build())
    $taskCertificate = $taskRequest.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5), [DateTimeOffset]::UtcNow.AddDays(1))
    try {
        $taskCertPath = Join-Path $taskFixture 'cert.pem'
        $taskKeyPath = Join-Path $taskFixture 'key.pem'
        [IO.File]::WriteAllText($taskCertPath, $taskCertificate.ExportCertificatePem())
        [IO.File]::WriteAllText($taskKeyPath, $taskKey.ExportPkcs8PrivateKeyPem())
        & python (Join-Path $PSScriptRoot 'test_public_tls.py') $taskCertPath $taskKeyPath
        if ($LASTEXITCODE -ne 0) { throw 'TLS tests failed' }
    } finally { $taskCertificate.Dispose() }
} finally {
    $taskKey.Dispose()
    # These are the exact two generated test files, not a recursive cleanup.
    foreach ($taskName in @('cert.pem','key.pem')) {
        $taskPath = Join-Path $taskFixture $taskName
        if (Test-Path -LiteralPath $taskPath) { Remove-Item -LiteralPath $taskPath -Force }
    }
}
