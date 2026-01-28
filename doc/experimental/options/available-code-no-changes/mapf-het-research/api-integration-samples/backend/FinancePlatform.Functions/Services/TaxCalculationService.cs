using FinancePlatform.Functions.Domain.Aggregates;
using FinancePlatform.Functions.Domain.Events;
using FinancePlatform.Functions.Domain.ValueObjects;

namespace FinancePlatform.Functions.Services;

public class TaxCalculationService
{
    private readonly EventStoreDbClient _eventStore;
    private readonly ILogger<TaxCalculationService> _logger;
    private readonly Dictionary<string, List<TaxRate>> _taxRates;

    public TaxCalculationService(EventStoreDbClient eventStore, ILogger<TaxCalculationService> logger)
    {
        _eventStore = eventStore;
        _logger = logger;
        _taxRates = InitializeTaxRates();
    }

    public async Task<TaxCalculationResult> CalculateTaxAsync(
        Guid transactionId,
        Money amount,
        string jurisdiction,
        string? category = null,
        CancellationToken ct = default)
    {
        var taxRate = GetTaxRate(jurisdiction, category, DateOnly.FromDateTime(DateTime.UtcNow));

        if (taxRate == null)
        {
            _logger.LogWarning(
                "No tax rate found for jurisdiction {Jurisdiction}, category {Category}",
                jurisdiction,
                category);

            return new TaxCalculationResult
            {
                TransactionId = transactionId,
                TaxableAmount = amount,
                TaxAmount = Money.Zero(amount.Currency),
                TaxRate = null,
                IsExempt = true,
                ExemptionReason = "No applicable tax rate"
            };
        }

        var taxAmount = taxRate.Value.CalculateTax(amount);

        // Get or create tax period
        var (year, quarter) = GetPeriod(DateTime.UtcNow);
        var periodId = TaxPeriodAggregate.GeneratePeriodId(jurisdiction, year, quarter);
        var streamName = TaxPeriodAggregate.GetStreamName(periodId);

        var events = await _eventStore.ReadStreamAsync<TaxEvent>(streamName, ct);
        var taxPeriod = TaxPeriodAggregate.Create(jurisdiction, year, quarter, amount.Currency.Code);
        taxPeriod.LoadFromHistory(events);

        // Record the calculation
        taxPeriod.CalculateTax(transactionId, amount, taxRate.Value);

        // Persist events
        await _eventStore.AppendEventsAsync(streamName, taxPeriod.UncommittedEvents, ct: ct);

        _logger.LogInformation(
            "Calculated tax for transaction {TransactionId}: {TaxAmount} ({Rate}%)",
            transactionId,
            taxAmount,
            taxRate.Value.AsPercentage);

        return new TaxCalculationResult
        {
            TransactionId = transactionId,
            TaxableAmount = amount,
            TaxAmount = taxAmount,
            TaxRate = taxRate.Value,
            PeriodId = periodId
        };
    }

    public async Task<TaxReport> GenerateReportAsync(
        string jurisdiction,
        int year,
        int quarter,
        string generatedBy,
        CancellationToken ct = default)
    {
        var periodId = TaxPeriodAggregate.GeneratePeriodId(jurisdiction, year, quarter);
        var streamName = TaxPeriodAggregate.GetStreamName(periodId);

        var events = await _eventStore.ReadStreamAsync<TaxEvent>(streamName, ct);

        if (!events.Any())
        {
            return new TaxReport
            {
                PeriodId = periodId,
                Jurisdiction = jurisdiction,
                Year = year,
                Quarter = quarter,
                TotalTaxable = Money.Zero("EUR"),
                TotalTax = Money.Zero("EUR"),
                TransactionCount = 0,
                GeneratedAt = DateTime.UtcNow,
                GeneratedBy = generatedBy
            };
        }

        var taxPeriod = TaxPeriodAggregate.Create(jurisdiction, year, quarter, "EUR");
        taxPeriod.LoadFromHistory(events);

        var reportId = taxPeriod.GenerateReport(generatedBy);
        await _eventStore.AppendEventsAsync(streamName, taxPeriod.UncommittedEvents, ct: ct);

        return new TaxReport
        {
            ReportId = reportId,
            PeriodId = periodId,
            Jurisdiction = jurisdiction,
            Year = year,
            Quarter = quarter,
            TotalTaxable = taxPeriod.TotalTaxable,
            TotalTax = taxPeriod.TotalTax,
            TransactionCount = taxPeriod.TransactionCount,
            Calculations = taxPeriod.Calculations.ToList(),
            ThresholdAlerts = taxPeriod.ThresholdAlerts.ToList(),
            GeneratedAt = DateTime.UtcNow,
            GeneratedBy = generatedBy
        };
    }

    public TaxRate? GetTaxRate(string jurisdiction, string? category, DateOnly date)
    {
        if (!_taxRates.TryGetValue(jurisdiction, out var rates))
            return null;

        return rates
            .Where(r => r.IsActiveOn(date))
            .Where(r => category == null || r.Category == null || r.Category == category)
            .OrderByDescending(r => r.Category != null) // Prefer specific category
            .FirstOrDefault();
    }

    public List<TaxRate> GetAllRates(string jurisdiction)
    {
        return _taxRates.TryGetValue(jurisdiction, out var rates)
            ? rates
            : new List<TaxRate>();
    }

    private static (int Year, int Quarter) GetPeriod(DateTime date)
    {
        var quarter = (date.Month - 1) / 3 + 1;
        return (date.Year, quarter);
    }

    private static Dictionary<string, List<TaxRate>> InitializeTaxRates()
    {
        return new Dictionary<string, List<TaxRate>>
        {
            ["RS"] = new()
            {
                TaxRate.SerbiaVAT,
                TaxRate.SerbiaReducedVAT
            },
            ["DE"] = new()
            {
                TaxRate.GermanyVAT,
                TaxRate.GermanyReducedVAT
            },
            ["GB"] = new()
            {
                TaxRate.UKVAT,
                TaxRate.UKReducedVAT
            },
            ["US"] = new()
            {
                new TaxRate(0.0825m, "US", TaxType.SalesTax), // Example: Texas
            },
            ["INTL"] = new()
            {
                TaxRate.ZeroRate
            }
        };
    }
}

public class TaxCalculationResult
{
    public Guid TransactionId { get; init; }
    public Money TaxableAmount { get; init; }
    public Money TaxAmount { get; init; }
    public TaxRate? TaxRate { get; init; }
    public string? PeriodId { get; init; }
    public bool IsExempt { get; init; }
    public string? ExemptionReason { get; init; }
}

public class TaxReport
{
    public Guid? ReportId { get; init; }
    public string PeriodId { get; init; } = string.Empty;
    public string Jurisdiction { get; init; } = string.Empty;
    public int Year { get; init; }
    public int Quarter { get; init; }
    public Money TotalTaxable { get; init; }
    public Money TotalTax { get; init; }
    public int TransactionCount { get; init; }
    public List<TaxCalculationEntry> Calculations { get; init; } = new();
    public List<ThresholdExceeded> ThresholdAlerts { get; init; } = new();
    public DateTime GeneratedAt { get; init; }
    public string GeneratedBy { get; init; } = string.Empty;
}
