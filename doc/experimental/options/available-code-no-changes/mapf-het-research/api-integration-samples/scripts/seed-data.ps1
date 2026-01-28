# Seed Data Script
# Creates sample transactions for demo purposes

$ErrorActionPreference = "Stop"
$apiBase = "http://localhost:7071/api"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Seeding Demo Data" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if API is available
Write-Host "Checking API availability..." -ForegroundColor Yellow
try {
    $response = Invoke-WebRequest -Uri "$apiBase/events/stats" -UseBasicParsing -TimeoutSec 5
    Write-Host "  API is available!" -ForegroundColor Green
} catch {
    Write-Host "  API not available at $apiBase" -ForegroundColor Red
    Write-Host "  Make sure the backend is running." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "Creating sample transactions..." -ForegroundColor Yellow

# Sample transactions
$transactions = @(
    @{
        amount = 1500.00
        currency = "EUR"
        description = "Invoice payment from Client A"
        category = "Revenue"
        jurisdiction = "RS"
        reference = "INV-2024-001"
    },
    @{
        amount = 2500.00
        currency = "EUR"
        description = "Software development services"
        category = "Revenue"
        jurisdiction = "DE"
        reference = "INV-2024-002"
    },
    @{
        amount = -350.00
        currency = "EUR"
        description = "Office supplies"
        category = "Expense"
        jurisdiction = "RS"
        reference = "EXP-2024-001"
    },
    @{
        amount = 5000.00
        currency = "USD"
        description = "Consulting fee - US client"
        category = "Revenue"
        jurisdiction = "US"
        reference = "INV-2024-003"
    },
    @{
        amount = 750.00
        currency = "GBP"
        description = "UK market research"
        category = "Revenue"
        jurisdiction = "GB"
        reference = "INV-2024-004"
    },
    @{
        amount = -1200.00
        currency = "EUR"
        description = "Server hosting - quarterly"
        category = "Expense"
        jurisdiction = "DE"
        reference = "EXP-2024-002"
    },
    @{
        amount = 3500.00
        currency = "EUR"
        description = "Product sales - batch order"
        category = "Revenue"
        jurisdiction = "RS"
        reference = "INV-2024-005"
    },
    @{
        amount = -180.50
        currency = "EUR"
        description = "Business travel expenses"
        category = "Expense"
        jurisdiction = "RS"
        reference = "EXP-2024-003"
    },
    @{
        amount = 890000.00
        currency = "RSD"
        description = "Local client - Dinar payment"
        category = "Revenue"
        jurisdiction = "RS"
        reference = "INV-2024-006"
    },
    @{
        amount = 2200.00
        currency = "EUR"
        description = "Annual subscription renewal"
        category = "Revenue"
        jurisdiction = "DE"
        reference = "INV-2024-007"
    }
)

$created = 0
foreach ($tx in $transactions) {
    try {
        $body = $tx | ConvertTo-Json
        $response = Invoke-RestMethod -Uri "$apiBase/transactions" -Method Post -Body $body -ContentType "application/json"
        $created++
        Write-Host "  Created: $($tx.reference) - $($tx.amount) $($tx.currency)" -ForegroundColor Green
    } catch {
        Write-Host "  Failed: $($tx.reference) - $($_.Exception.Message)" -ForegroundColor Red
    }
    Start-Sleep -Milliseconds 200
}

Write-Host ""
Write-Host "Created $created transactions" -ForegroundColor Cyan

# Generate tax reports
Write-Host ""
Write-Host "Generating tax reports..." -ForegroundColor Yellow

$jurisdictions = @("RS", "DE", "GB", "US")
$currentYear = (Get-Date).Year
$currentQuarter = [Math]::Ceiling((Get-Date).Month / 3)

foreach ($jurisdiction in $jurisdictions) {
    try {
        $body = @{
            jurisdiction = $jurisdiction
            year = $currentYear
            quarter = $currentQuarter
            generatedBy = "seed-script"
        } | ConvertTo-Json

        $response = Invoke-RestMethod -Uri "$apiBase/tax/report" -Method Post -Body $body -ContentType "application/json"
        Write-Host "  Generated tax report: $jurisdiction Q$currentQuarter $currentYear" -ForegroundColor Green
    } catch {
        Write-Host "  Note: $jurisdiction - $($_.Exception.Message)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Seed Complete!" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "You can now:" -ForegroundColor Yellow
Write-Host "  - View the dashboard at http://localhost:5173"
Write-Host "  - Browse events in EventStoreDB at http://localhost:2113"
Write-Host "  - Create more transactions via the UI"
Write-Host ""
