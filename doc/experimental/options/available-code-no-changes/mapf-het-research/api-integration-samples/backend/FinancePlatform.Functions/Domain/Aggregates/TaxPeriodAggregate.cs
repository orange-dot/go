using FinancePlatform.Functions.Domain.Events;
using FinancePlatform.Functions.Domain.ValueObjects;

namespace FinancePlatform.Functions.Domain.Aggregates;

public class TaxPeriodAggregate
{
    public string PeriodId { get; private set; } = string.Empty;
    public string Jurisdiction { get; private set; } = string.Empty;
    public DateOnly StartDate { get; private set; }
    public DateOnly EndDate { get; private set; }
    public Money TotalTaxable { get; private set; }
    public Money TotalTax { get; private set; }
    public int TransactionCount { get; private set; }
    public TaxPeriodStatus Status { get; private set; }
    public int Version { get; private set; }

    private readonly List<TaxCalculationEntry> _calculations = new();
    private readonly List<ThresholdExceeded> _thresholdAlerts = new();
    public IReadOnlyList<TaxCalculationEntry> Calculations => _calculations.AsReadOnly();
    public IReadOnlyList<ThresholdExceeded> ThresholdAlerts => _thresholdAlerts.AsReadOnly();

    private readonly List<TaxEvent> _uncommittedEvents = new();
    public IReadOnlyList<TaxEvent> UncommittedEvents => _uncommittedEvents.AsReadOnly();

    public static string GetStreamName(string periodId) => $"tax-{periodId}";

    public static string GeneratePeriodId(string jurisdiction, int year, int quarter)
        => $"{jurisdiction}-{year}-Q{quarter}";

    public static TaxPeriodAggregate Create(
        string jurisdiction,
        int year,
        int quarter,
        string currency)
    {
        var periodId = GeneratePeriodId(jurisdiction, year, quarter);
        var startDate = new DateOnly(year, (quarter - 1) * 3 + 1, 1);
        var endDate = startDate.AddMonths(3).AddDays(-1);

        var aggregate = new TaxPeriodAggregate
        {
            PeriodId = periodId,
            Jurisdiction = jurisdiction,
            StartDate = startDate,
            EndDate = endDate,
            TotalTaxable = Money.Zero(currency),
            TotalTax = Money.Zero(currency),
            Status = TaxPeriodStatus.Open
        };

        return aggregate;
    }

    public void CalculateTax(
        Guid transactionId,
        Money taxableAmount,
        TaxRate taxRate)
    {
        if (Status == TaxPeriodStatus.Closed)
            throw new InvalidOperationException("Cannot add tax to closed period");

        var taxAmount = taxRate.CalculateTax(taxableAmount);

        var @event = new TaxCalculated
        {
            PeriodId = PeriodId,
            TransactionId = transactionId,
            TaxableAmount = taxableAmount,
            TaxAmount = taxAmount,
            TaxRate = taxRate
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);

        // Check thresholds
        CheckThresholds();
    }

    public Guid GenerateReport(string generatedBy, string? format = null)
    {
        var reportId = Guid.NewGuid();

        var @event = new TaxReportGenerated
        {
            PeriodId = PeriodId,
            ReportId = reportId,
            TotalTaxable = TotalTaxable,
            TotalTax = TotalTax,
            TransactionCount = TransactionCount,
            GeneratedBy = generatedBy,
            ReportFormat = format
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);

        return reportId;
    }

    public void RecordPayment(Money amount, DateOnly paymentDate, string? reference = null)
    {
        var @event = new TaxPaymentRecorded
        {
            PeriodId = PeriodId,
            PaymentId = Guid.NewGuid(),
            Amount = amount,
            Jurisdiction = Jurisdiction,
            PaymentDate = paymentDate,
            Reference = reference
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public void ApplyAdjustment(
        Money amount,
        string reason,
        string appliedBy,
        Guid? relatedTransactionId = null)
    {
        var @event = new TaxAdjustmentApplied
        {
            PeriodId = PeriodId,
            AdjustmentId = Guid.NewGuid(),
            Amount = amount,
            Reason = reason,
            AppliedBy = appliedBy,
            RelatedTransactionId = relatedTransactionId
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public void Close()
    {
        Status = TaxPeriodStatus.Closed;
    }

    public void ClearUncommittedEvents() => _uncommittedEvents.Clear();

    public void LoadFromHistory(IEnumerable<TaxEvent> history)
    {
        foreach (var @event in history)
        {
            Apply(@event);
            Version++;
        }
    }

    private void CheckThresholds()
    {
        // VAT Registration threshold (example: 8,000,000 RSD for Serbia)
        var vatThreshold = new Money(8_000_000m, TotalTaxable.Currency);

        if (TotalTaxable.Amount >= vatThreshold.Amount && !HasThresholdAlert(ThresholdType.VATRegistration))
        {
            var alert = new ThresholdExceeded
            {
                PeriodId = PeriodId,
                ThresholdType = ThresholdType.VATRegistration,
                CurrentValue = TotalTaxable,
                ThresholdValue = vatThreshold,
                Jurisdiction = Jurisdiction,
                RecommendedAction = "Register for VAT if not already registered"
            };

            Apply(alert);
            _uncommittedEvents.Add(alert);
        }
    }

    private bool HasThresholdAlert(ThresholdType type)
        => _thresholdAlerts.Any(a => a.ThresholdType == type);

    private void Apply(TaxEvent @event)
    {
        switch (@event)
        {
            case TaxCalculated calculated:
                var entry = new TaxCalculationEntry
                {
                    TransactionId = calculated.TransactionId,
                    TaxableAmount = calculated.TaxableAmount,
                    TaxAmount = calculated.TaxAmount,
                    TaxRate = calculated.TaxRate,
                    CalculatedAt = calculated.Timestamp
                };
                _calculations.Add(entry);
                TotalTaxable = TotalTaxable.Add(calculated.TaxableAmount);
                TotalTax = TotalTax.Add(calculated.TaxAmount);
                TransactionCount++;
                break;

            case TaxAdjustmentApplied adjustment:
                TotalTax = TotalTax.Add(adjustment.Amount);
                break;

            case ThresholdExceeded threshold:
                _thresholdAlerts.Add(threshold);
                break;

            case TaxReportGenerated:
                Status = TaxPeriodStatus.Reported;
                break;
        }
    }
}

public class TaxCalculationEntry
{
    public Guid TransactionId { get; init; }
    public Money TaxableAmount { get; init; }
    public Money TaxAmount { get; init; }
    public TaxRate TaxRate { get; init; }
    public DateTime CalculatedAt { get; init; }
}

public enum TaxPeriodStatus
{
    Open,
    Reported,
    Closed
}
