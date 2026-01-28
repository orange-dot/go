using FinancePlatform.Functions.Domain.Events;
using FinancePlatform.Functions.Domain.ValueObjects;
using FinancePlatform.Functions.Services;

namespace FinancePlatform.Functions.Projections;

public class TaxLiabilityProjection
{
    private readonly EventStoreDbClient _eventStore;
    private readonly ILogger<TaxLiabilityProjection> _logger;

    public TaxLiabilityProjection(EventStoreDbClient eventStore, ILogger<TaxLiabilityProjection> logger)
    {
        _eventStore = eventStore;
        _logger = logger;
    }

    public async Task<TaxLiabilityView> GetTaxLiabilityAsync(
        string jurisdiction,
        int year,
        CancellationToken ct = default)
    {
        var quarters = new List<QuarterlyTaxSummary>();

        for (int quarter = 1; quarter <= 4; quarter++)
        {
            var periodId = $"{jurisdiction}-{year}-Q{quarter}";
            var streamName = $"tax-{periodId}";

            var events = await _eventStore.ReadStreamAsync<TaxEvent>(streamName, ct);

            var summary = new QuarterlyTaxSummary
            {
                Quarter = quarter,
                PeriodId = periodId
            };

            Money? taxable = null;
            Money? tax = null;

            foreach (var @event in events)
            {
                switch (@event)
                {
                    case TaxCalculated calculated:
                        taxable = taxable?.Add(calculated.TaxableAmount)
                            ?? calculated.TaxableAmount;
                        tax = tax?.Add(calculated.TaxAmount)
                            ?? calculated.TaxAmount;
                        summary.TransactionCount++;
                        break;

                    case TaxReportGenerated report:
                        summary.ReportGenerated = true;
                        summary.ReportGeneratedAt = report.Timestamp;
                        break;

                    case TaxPaymentRecorded payment:
                        summary.PaymentsMade = (summary.PaymentsMade ?? Money.Zero(payment.Amount.Currency))
                            .Add(payment.Amount);
                        break;

                    case ThresholdExceeded threshold:
                        summary.Alerts.Add(new TaxAlert
                        {
                            Type = threshold.ThresholdType.ToString(),
                            Message = $"Threshold exceeded: {threshold.CurrentValue}",
                            Timestamp = threshold.Timestamp
                        });
                        break;
                }
            }

            summary.TotalTaxable = taxable ?? Money.Zero("EUR");
            summary.TotalTax = tax ?? Money.Zero("EUR");
            summary.OutstandingTax = summary.TotalTax.Subtract(
                summary.PaymentsMade ?? Money.Zero(summary.TotalTax.Currency));

            quarters.Add(summary);
        }

        var currency = quarters.FirstOrDefault(q => !q.TotalTaxable.IsZero)?.TotalTaxable.Currency
            ?? Currency.EUR;

        return new TaxLiabilityView
        {
            Jurisdiction = jurisdiction,
            Year = year,
            Quarters = quarters,
            YearlyTotalTaxable = new Money(quarters.Sum(q => q.TotalTaxable.Amount), currency),
            YearlyTotalTax = new Money(quarters.Sum(q => q.TotalTax.Amount), currency),
            YearlyTotalPaid = new Money(
                quarters.Sum(q => q.PaymentsMade?.Amount ?? 0),
                currency),
            YearlyOutstanding = new Money(
                quarters.Sum(q => q.OutstandingTax.Amount),
                currency),
            TotalTransactions = quarters.Sum(q => q.TransactionCount),
            AllReportsGenerated = quarters.All(q => q.ReportGenerated),
            Alerts = quarters.SelectMany(q => q.Alerts).ToList()
        };
    }

    public async Task<TaxPeriodDetailView> GetPeriodDetailAsync(
        string periodId,
        CancellationToken ct = default)
    {
        var streamName = $"tax-{periodId}";
        var events = await _eventStore.ReadStreamAsync<TaxEvent>(streamName, ct);

        var calculations = new List<TaxCalculationDetail>();
        var payments = new List<TaxPaymentDetail>();
        var adjustments = new List<TaxAdjustmentDetail>();
        var alerts = new List<TaxAlert>();

        Money totalTaxable = Money.Zero("EUR");
        Money totalTax = Money.Zero("EUR");
        Money totalPaid = Money.Zero("EUR");
        bool reportGenerated = false;
        DateTime? reportGeneratedAt = null;

        foreach (var @event in events)
        {
            switch (@event)
            {
                case TaxCalculated calculated:
                    totalTaxable = totalTaxable.Currency.Code == "EUR" && totalTaxable.IsZero
                        ? calculated.TaxableAmount
                        : totalTaxable.Add(calculated.TaxableAmount);
                    totalTax = totalTax.Currency.Code == "EUR" && totalTax.IsZero
                        ? calculated.TaxAmount
                        : totalTax.Add(calculated.TaxAmount);

                    calculations.Add(new TaxCalculationDetail
                    {
                        TransactionId = calculated.TransactionId,
                        TaxableAmount = calculated.TaxableAmount,
                        TaxAmount = calculated.TaxAmount,
                        Rate = calculated.TaxRate.Rate,
                        Jurisdiction = calculated.TaxRate.Jurisdiction,
                        CalculatedAt = calculated.Timestamp
                    });
                    break;

                case TaxPaymentRecorded payment:
                    totalPaid = totalPaid.Currency.Code == "EUR" && totalPaid.IsZero
                        ? payment.Amount
                        : totalPaid.Add(payment.Amount);

                    payments.Add(new TaxPaymentDetail
                    {
                        PaymentId = payment.PaymentId,
                        Amount = payment.Amount,
                        PaymentDate = payment.PaymentDate,
                        Reference = payment.Reference
                    });
                    break;

                case TaxAdjustmentApplied adjustment:
                    totalTax = totalTax.Add(adjustment.Amount);

                    adjustments.Add(new TaxAdjustmentDetail
                    {
                        AdjustmentId = adjustment.AdjustmentId,
                        Amount = adjustment.Amount,
                        Reason = adjustment.Reason,
                        AppliedBy = adjustment.AppliedBy,
                        AppliedAt = adjustment.Timestamp
                    });
                    break;

                case TaxReportGenerated report:
                    reportGenerated = true;
                    reportGeneratedAt = report.Timestamp;
                    break;

                case ThresholdExceeded threshold:
                    alerts.Add(new TaxAlert
                    {
                        Type = threshold.ThresholdType.ToString(),
                        Message = threshold.RecommendedAction ?? "Threshold exceeded",
                        Timestamp = threshold.Timestamp
                    });
                    break;
            }
        }

        return new TaxPeriodDetailView
        {
            PeriodId = periodId,
            TotalTaxable = totalTaxable,
            TotalTax = totalTax,
            TotalPaid = totalPaid,
            Outstanding = totalTax.Subtract(totalPaid),
            Calculations = calculations,
            Payments = payments,
            Adjustments = adjustments,
            Alerts = alerts,
            ReportGenerated = reportGenerated,
            ReportGeneratedAt = reportGeneratedAt
        };
    }
}

public class TaxLiabilityView
{
    public string Jurisdiction { get; init; } = string.Empty;
    public int Year { get; init; }
    public List<QuarterlyTaxSummary> Quarters { get; init; } = new();
    public Money YearlyTotalTaxable { get; init; }
    public Money YearlyTotalTax { get; init; }
    public Money YearlyTotalPaid { get; init; }
    public Money YearlyOutstanding { get; init; }
    public int TotalTransactions { get; init; }
    public bool AllReportsGenerated { get; init; }
    public List<TaxAlert> Alerts { get; init; } = new();
}

public class QuarterlyTaxSummary
{
    public int Quarter { get; init; }
    public string PeriodId { get; init; } = string.Empty;
    public Money TotalTaxable { get; set; }
    public Money TotalTax { get; set; }
    public Money? PaymentsMade { get; set; }
    public Money OutstandingTax { get; set; }
    public int TransactionCount { get; set; }
    public bool ReportGenerated { get; set; }
    public DateTime? ReportGeneratedAt { get; set; }
    public List<TaxAlert> Alerts { get; init; } = new();
}

public class TaxAlert
{
    public string Type { get; init; } = string.Empty;
    public string Message { get; init; } = string.Empty;
    public DateTime Timestamp { get; init; }
}

public class TaxPeriodDetailView
{
    public string PeriodId { get; init; } = string.Empty;
    public Money TotalTaxable { get; init; }
    public Money TotalTax { get; init; }
    public Money TotalPaid { get; init; }
    public Money Outstanding { get; init; }
    public List<TaxCalculationDetail> Calculations { get; init; } = new();
    public List<TaxPaymentDetail> Payments { get; init; } = new();
    public List<TaxAdjustmentDetail> Adjustments { get; init; } = new();
    public List<TaxAlert> Alerts { get; init; } = new();
    public bool ReportGenerated { get; init; }
    public DateTime? ReportGeneratedAt { get; init; }
}

public class TaxCalculationDetail
{
    public Guid TransactionId { get; init; }
    public Money TaxableAmount { get; init; }
    public Money TaxAmount { get; init; }
    public decimal Rate { get; init; }
    public string Jurisdiction { get; init; } = string.Empty;
    public DateTime CalculatedAt { get; init; }
}

public class TaxPaymentDetail
{
    public Guid PaymentId { get; init; }
    public Money Amount { get; init; }
    public DateOnly PaymentDate { get; init; }
    public string? Reference { get; init; }
}

public class TaxAdjustmentDetail
{
    public Guid AdjustmentId { get; init; }
    public Money Amount { get; init; }
    public string Reason { get; init; } = string.Empty;
    public string AppliedBy { get; init; } = string.Empty;
    public DateTime AppliedAt { get; init; }
}
