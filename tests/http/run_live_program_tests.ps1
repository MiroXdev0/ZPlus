param(
    [Parameter(Mandatory = $true)][string]$Zp,
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = "Stop"

function Send-Request {
    param(
        [string]$Uri,
        [string]$Method = "GET",
        [string]$Body = "",
        [string]$ContentType = "text/plain",
        [hashtable]$Headers = @{}
    )
    $request = [System.Net.HttpWebRequest]::Create($Uri)
    $request.Method = $Method
    $request.Timeout = 4000
    $request.ReadWriteTimeout = 4000
    foreach ($name in $Headers.Keys) {
        $request.Headers.Add($name, [string]$Headers[$name])
    }
    if ($Method -eq "POST" -or $Method -eq "PUT" -or $Method -eq "PATCH") {
        $request.ContentType = $ContentType
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Body)
        $request.ContentLength = $bytes.Length
        $stream = $request.GetRequestStream()
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Dispose()
    }
    $response = $request.GetResponse()
    try {
        $reader = [System.IO.StreamReader]::new($response.GetResponseStream())
        try {
            return [pscustomobject]@{
                Status = [int]$response.StatusCode
                Body = $reader.ReadToEnd()
            }
        } finally {
            $reader.Dispose()
        }
    } finally {
        $response.Dispose()
    }
}

function Start-Server {
    param(
        [string]$Executable,
        [string]$Arguments,
        [string]$BaseUri,
        [string]$Name
    )
    $stdout = Join-Path $WorkDir "$Name.stdout.txt"
    $stderr = Join-Path $WorkDir "$Name.stderr.txt"
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments `
        -WorkingDirectory $WorkDir -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try {
        $ready = $false
        for ($attempt = 0; $attempt -lt 50; $attempt++) {
            $process.Refresh()
            if ($process.HasExited) {
                throw "$Name exited before listening. $(Get-Content $stderr -Raw -ErrorAction SilentlyContinue)"
            }
            try {
                $health = Send-Request -Uri "$BaseUri/health"
                if ($health.Status -eq 200) {
                    $ready = $true
                    break
                }
            } catch {
                Start-Sleep -Milliseconds 100
            }
        }
        if (-not $ready) {
            throw "$Name did not become ready. $(Get-Content $stderr -Raw -ErrorAction SilentlyContinue)"
        }
        return $process
    } catch {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id
        }
        throw
    }
}

function Stop-Server {
    param([System.Diagnostics.Process]$Process, [string]$BaseUri, [string]$Name)
    $stopping = Send-Request -Uri "$BaseUri/__stop" -Method "POST" -Body "stop"
    if ($stopping.Status -ne 200 -or -not $Process.WaitForExit(5000)) {
        throw "$Name failed its shutdown route"
    }
    if ($Process.ExitCode -ne 0) {
        throw "$Name exited with $($Process.ExitCode)"
    }
}

function Run-Client {
    param([string]$Executable, [string]$Arguments, [string]$Name, [string]$ExpectedText)
    $stdout = Join-Path $WorkDir "$Name.stdout.txt"
    $stderr = Join-Path $WorkDir "$Name.stderr.txt"
    $client = Start-Process -FilePath $Executable -ArgumentList $Arguments `
        -WorkingDirectory $WorkDir -PassThru -Wait -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $output = Get-Content $stdout -Raw
    if ($client.ExitCode -ne 0 -or -not $output.Contains($ExpectedText)) {
        throw "$Name failed ($($client.ExitCode)): $output $(Get-Content $stderr -Raw)"
    }
}

function Test-Hello {
    param([bool]$Native)
    $name = if ($Native) { "hello-native" } else { "hello-interpreter" }
    [Console]::Error.WriteLine("starting $name")
    $testStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $serverExecutable = if ($Native) {
        Join-Path $WorkDir "hello_server.exe"
    } else {
        $Zp
    }
    $arguments = if ($Native) {
        ""
    } else {
        $source = Join-Path $SourceRoot "test_program\backend\hello_server.zp"
        "run `"$source`""
    }
    $process = Start-Server $serverExecutable $arguments "http://127.0.0.1:8080" $name
    try {
        $health = Send-Request -Uri "http://127.0.0.1:8080/health"
        if ($health.Body -ne "ok`n") { throw "$name returned an unexpected health response" }
        $clientExecutable = if ($Native) {
            Join-Path $WorkDir "external_api_client.exe"
        } else {
            $Zp
        }
        $clientArguments = if ($Native) {
            ""
        } else {
            $source = Join-Path $SourceRoot "test_program\backend\external_api_client.zp"
            "run `"$source`""
        }
        Run-Client $clientExecutable $clientArguments "$name-client" "Health service replied: ok"
    } finally {
        if (-not $process.HasExited) {
            Stop-Server $process "http://127.0.0.1:8080" $name
        }
    }
    [Console]::Error.WriteLine("finished $name in $([int]$testStopwatch.ElapsedMilliseconds) ms")
}

function Test-Rest {
    param([bool]$Native)
    $name = if ($Native) { "rest-native" } else { "rest-interpreter" }
    [Console]::Error.WriteLine("starting $name")
    $testStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $serverExecutable = if ($Native) {
        Join-Path $WorkDir "rest_api.exe"
    } else {
        $Zp
    }
    $arguments = if ($Native) {
        ""
    } else {
        $source = Join-Path $SourceRoot "test_program\backend\rest_api.zp"
        "run `"$source`""
    }
    $base = "http://127.0.0.1:8081"
    $process = Start-Server $serverExecutable $arguments $base $name
    try {
        $created = Send-Request -Uri "$base/users" -Method "POST" `
            -Body '{"name":"Ada"}' -ContentType "application/json"
        if ($created.Status -ne 201 -or $created.Body -notmatch '"name":"Ada"') {
            throw "$name did not create a user from JSON"
        }
        $user = Send-Request -Uri "$base/users/42"
        if ($user.Status -ne 200 -or $user.Body -notmatch '"id":"42"') {
            throw "$name did not resolve its route parameter"
        }
    } finally {
        if (-not $process.HasExited) {
            Stop-Server $process $base $name
        }
    }
    [Console]::Error.WriteLine("finished $name in $([int]$testStopwatch.ElapsedMilliseconds) ms")
}

function Test-MultiClient {
    param([bool]$Native)
    $name = if ($Native) { "multi-native" } else { "multi-interpreter" }
    [Console]::Error.WriteLine("starting $name")
    $testStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $serverExecutable = if ($Native) {
        Join-Path $WorkDir "multi_client_server.exe"
    } else {
        $Zp
    }
    $arguments = if ($Native) {
        ""
    } else {
        $source = Join-Path $SourceRoot "test_program\backend\multi_client_server.zp"
        "run `"$source`""
    }
    $base = "http://127.0.0.1:8082"
    $process = Start-Server $serverExecutable $arguments $base $name
    try {
        $echo = Send-Request -Uri "$base/echo" -Method "POST" `
            -Body '{"message":"isolated"}' -ContentType "application/json" `
            -Headers @{ "x-client-id" = $name }
        if ($echo.Status -ne 200 -or $echo.Body -notmatch [regex]::Escape($name) -or
            $echo.Body -notmatch "isolated") {
            throw "$name returned an invalid isolated response"
        }

        $client = [System.Net.Http.HttpClient]::new()
        $client.Timeout = [TimeSpan]::FromSeconds(5)
        $requests = @()
        $tasks = @()
        $concurrentStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        try {
            for ($index = 0; $index -lt 4; $index++) {
                $clientId = "$name-concurrent-$index"
                $request = [System.Net.Http.HttpRequestMessage]::new(
                    [System.Net.Http.HttpMethod]::Post, "$base/echo")
                $request.Headers.Add("x-client-id", $clientId)
                $request.Content = [System.Net.Http.StringContent]::new(
                    "{`"message`":`"request-$index`"}",
                    [System.Text.Encoding]::UTF8,
                    "application/json")
                $requests += $request
                $tasks += $client.SendAsync($request)
            }
            try {
                [System.Threading.Tasks.Task]::WaitAll(
                    [System.Threading.Tasks.Task[]]$tasks)
            } catch {
                $details = foreach ($task in $tasks) {
                    "task $($task.Id): $($task.Status) $(if ($task.Exception) { $task.Exception.InnerException.Message })"
                }
                throw ("$name concurrent echo requests failed after " +
                    "$([int]$concurrentStopwatch.ElapsedMilliseconds) ms:`n" +
                    ($details -join "`n"))
            }
            for ($index = 0; $index -lt $tasks.Count; $index++) {
                $response = $tasks[$index].Result
                $responseBody = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
                if (-not $response.IsSuccessStatusCode -or
                    $responseBody -notmatch [regex]::Escape("$name-concurrent-$index") -or
                    $responseBody -notmatch "request-$index") {
                    throw "$name returned a mismatched concurrent response for request $index"
                }
                $response.Dispose()
            }
        } finally {
            foreach ($request in $requests) { $request.Dispose() }
            $client.Dispose()
        }
    } finally {
        if (-not $process.HasExited) {
            Stop-Server $process $base $name
        }
    }
    [Console]::Error.WriteLine("finished $name in $([int]$testStopwatch.ElapsedMilliseconds) ms")
}

foreach ($testCase in @({ Test-Hello $false }, { Test-Rest $false }, { Test-MultiClient $false },
        { Test-Hello $true }, { Test-Rest $true }, { Test-MultiClient $true })) {
    & $testCase
}
Write-Output "Live Z+ HTTP program tests passed"
