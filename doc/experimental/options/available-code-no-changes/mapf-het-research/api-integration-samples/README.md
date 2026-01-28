# Event-Driven Finance Platform Demo

A comprehensive full-stack finance platform demonstrating event sourcing patterns with AsyncAPI, EventStoreDB, Azure Functions, and React.

## Architecture Overview

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   React + Vite  │────▶│  Azure Functions │────▶│  EventStoreDB   │
│   TanStack      │◀────│  .NET 10         │◀────│  Event Sourcing │
└─────────────────┘     └──────────────────┘     └─────────────────┘
     :5173                    :7071                    :2113
```

## Features

### 1. Tax Compliance Automation
- VAT calculation engine with configurable rates
- Jurisdiction-based tax rules
- Automated tax reporting events
- Registration threshold monitoring

### 2. Transaction Reconciliation
- Multi-source transaction ingestion
- Fuzzy matching algorithm
- Unmatched transaction alerts
- Real-time reconciliation status

### 3. Audit Trail + Compliance
- Immutable event log via EventStoreDB
- Complete user action tracking
- Compliance report generation
- Point-in-time state reconstruction

### 4. Multi-Currency Ledger
- Double-entry bookkeeping
- Real-time exchange rates
- Currency conversion events
- Per-currency balance projections

## Tech Stack

### Backend
| Technology | Version | Purpose |
|------------|---------|---------|
| .NET | 10 | Runtime |
| Azure Functions | v4 Isolated | Serverless compute |
| EventStoreDB | 24.x | Event sourcing |
| Polly | 8.x | Resilience |
| FluentValidation | 11.x | Input validation |

### Frontend
| Technology | Version | Purpose |
|------------|---------|---------|
| React | 19 | UI framework |
| Vite | 6 | Build tool |
| TanStack Query | 5 | Data fetching |
| TanStack Router | 1 | Type-safe routing |
| TanStack Table | 8 | Data tables |
| Tailwind CSS | 4 | Styling |
| TypeScript | 5.7 | Type safety |

## Quick Start

### Prerequisites
- Docker Desktop
- Node.js 22+
- .NET 10 SDK

### Start Infrastructure

```bash
# Windows PowerShell
.\scripts\setup.ps1

# Or manually with Docker Compose
docker-compose up -d
```

### Access Points

| Service | URL | Credentials |
|---------|-----|-------------|
| Frontend | http://localhost:5173 | - |
| Backend API | http://localhost:7071/api | - |
| EventStoreDB UI | http://localhost:2113 | admin / changeit |

### Seed Test Data

```bash
.\scripts\seed-data.ps1
```

## API Endpoints

### Commands (POST)
- `POST /api/transactions` - Create transaction
- `POST /api/transactions/{id}/reconcile` - Reconcile transactions
- `POST /api/tax/calculate` - Calculate tax for period

### Queries (GET)
- `GET /api/ledger/balance` - Get ledger balances
- `GET /api/audit-trail` - Get audit trail
- `GET /api/tax/report` - Get tax report

### Streams (WebSocket)
- `WS /api/events/stream` - Real-time event stream

## Event Channels

Defined in `asyncapi.yaml`:

| Channel | Events |
|---------|--------|
| `transactions` | TransactionCreated, TransactionApproved, TransactionRejected |
| `reconciliation` | ReconciliationStarted, TransactionsMatched, MatchingFailed |
| `tax` | TaxCalculated, TaxReportGenerated, ThresholdExceeded |
| `ledger` | LedgerEntryPosted, BalanceUpdated, CurrencyConverted |

## Project Structure

```
api-integration-samples/
├── README.md                 # This file
├── docker-compose.yml        # Infrastructure
├── asyncapi.yaml             # Event specification
│
├── backend/
│   └── FinancePlatform.Functions/
│       ├── Functions/        # HTTP triggers
│       ├── Domain/           # Events, Aggregates, ValueObjects
│       ├── Services/         # Business logic
│       └── Projections/      # Read models
│
├── frontend/
│   └── src/
│       ├── api/              # API clients
│       ├── pages/            # Route components
│       ├── components/       # UI components
│       └── hooks/            # Custom hooks
│
└── scripts/                  # Setup & seed scripts
```

## Development

### Backend

```bash
cd backend/FinancePlatform.Functions
dotnet restore
dotnet build
func start
```

### Frontend

```bash
cd frontend
npm install
npm run dev
```

### Run Tests

```bash
# Backend
cd backend/FinancePlatform.Functions
dotnet test

# Frontend
cd frontend
npm test
```

## Demo Workflow

1. **Create Transaction** - Add a new transaction in any currency
2. **Watch Events** - See real-time events in the dashboard
3. **View Ledger** - Check balances auto-converted to base currency
4. **Run Reconciliation** - Match transactions from different sources
5. **Generate Tax Report** - Create compliance report for a period
6. **Browse Audit Trail** - Full history with point-in-time reconstruction

## EventStoreDB Streams

- `transaction-{id}` - Individual transaction lifecycle
- `ledger-{currency}` - Per-currency ledger entries
- `tax-{period}` - Tax calculation events per period
- `reconciliation-{batchId}` - Reconciliation batch events
- `$ce-transactions` - All transaction events (category)

## License

MIT
