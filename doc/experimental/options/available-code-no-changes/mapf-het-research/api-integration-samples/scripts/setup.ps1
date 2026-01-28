# Finance Platform Setup Script
# Run this script to start the entire infrastructure

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Finance Platform - Event-Driven Demo" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check prerequisites
Write-Host "Checking prerequisites..." -ForegroundColor Yellow

# Check Docker
try {
    $dockerVersion = docker --version
    Write-Host "  Docker: $dockerVersion" -ForegroundColor Green
} catch {
    Write-Host "  Docker not found. Please install Docker Desktop." -ForegroundColor Red
    exit 1
}

# Check if Docker is running
try {
    docker info | Out-Null
    Write-Host "  Docker daemon: Running" -ForegroundColor Green
} catch {
    Write-Host "  Docker daemon not running. Please start Docker Desktop." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Starting services..." -ForegroundColor Yellow

# Navigate to project root
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptPath
Set-Location $projectRoot

# Start EventStoreDB first
Write-Host "  Starting EventStoreDB..." -ForegroundColor Cyan
docker-compose up -d eventstoredb

# Wait for EventStoreDB to be healthy
Write-Host "  Waiting for EventStoreDB to be ready..." -ForegroundColor Cyan
$maxAttempts = 30
$attempt = 0
do {
    $attempt++
    Start-Sleep -Seconds 2
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:2113/health/live" -UseBasicParsing -TimeoutSec 5
        if ($response.StatusCode -eq 200) {
            Write-Host "  EventStoreDB is ready!" -ForegroundColor Green
            break
        }
    } catch {
        Write-Host "    Attempt $attempt/$maxAttempts..." -ForegroundColor Gray
    }
} while ($attempt -lt $maxAttempts)

if ($attempt -ge $maxAttempts) {
    Write-Host "  EventStoreDB failed to start. Check docker logs." -ForegroundColor Red
    exit 1
}

# Option to start backend and frontend in Docker or locally
Write-Host ""
Write-Host "How would you like to run the application?" -ForegroundColor Yellow
Write-Host "  1. Docker Compose (all services in containers)"
Write-Host "  2. Local development (EventStoreDB in Docker, app locally)"
Write-Host ""
$choice = Read-Host "Enter choice (1 or 2)"

if ($choice -eq "1") {
    Write-Host ""
    Write-Host "  Starting backend and frontend containers..." -ForegroundColor Cyan
    docker-compose up -d backend frontend

    Write-Host ""
    Write-Host "All services started!" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "EventStoreDB is running. Start the backend and frontend manually:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Backend:" -ForegroundColor Cyan
    Write-Host "    cd backend/FinancePlatform.Functions"
    Write-Host "    func start"
    Write-Host ""
    Write-Host "  Frontend:" -ForegroundColor Cyan
    Write-Host "    cd frontend"
    Write-Host "    npm install"
    Write-Host "    npm run dev"
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Access Points" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Frontend:      http://localhost:5173" -ForegroundColor White
Write-Host "  Backend API:   http://localhost:7071/api" -ForegroundColor White
Write-Host "  EventStoreDB:  http://localhost:2113" -ForegroundColor White
Write-Host "                 (admin / changeit)" -ForegroundColor Gray
Write-Host ""
Write-Host "Run .\scripts\seed-data.ps1 to populate sample data" -ForegroundColor Yellow
Write-Host ""
