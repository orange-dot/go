using FinancePlatform.Functions.Domain.ValueObjects;

namespace FinancePlatform.Functions.Domain.Events;

public abstract record TaxEvent
{
    public string PeriodId { get; init; } = string.Empty;
    public DateTime Timestamp { get; init; } = DateTime.UtcNow;
    public string EventType => GetType().Name;
}

public record TaxCalculated : TaxEvent
{
    public required Guid TransactionId { get; init; }
    public required Money TaxableAmount { get; init; }
    public required Money TaxAmount { get; init; }
    public required TaxRate TaxRate { get; init; }
}

public record TaxReportGenerated : TaxEvent
{
    public required Guid ReportId { get; init; }
    public required Money TotalTaxable { get; init; }
    public required Money TotalTax { get; init; }
    public required int TransactionCount { get; init; }
    public required string GeneratedBy { get; init; }
    public string? ReportFormat { get; init; }
}

public record ThresholdExceeded : TaxEvent
{
    public required ThresholdType ThresholdType { get; init; }
    public required Money CurrentValue { get; init; }
    public required Money ThresholdValue { get; init; }
    public required string Jurisdiction { get; init; }
    public string? RecommendedAction { get; init; }
}

public record TaxRateChanged : TaxEvent
{
    public required string Jurisdiction { get; init; }
    public required TaxType TaxType { get; init; }
    public required decimal OldRate { get; init; }
    public required decimal NewRate { get; init; }
    public required DateOnly EffectiveDate { get; init; }
}

public record TaxPaymentRecorded : TaxEvent
{
    public required Guid PaymentId { get; init; }
    public required Money Amount { get; init; }
    public required string Jurisdiction { get; init; }
    public required DateOnly PaymentDate { get; init; }
    public string? Reference { get; init; }
}

public record TaxAdjustmentApplied : TaxEvent
{
    public required Guid AdjustmentId { get; init; }
    public required Money Amount { get; init; }
    public required string Reason { get; init; }
    public required string AppliedBy { get; init; }
    public Guid? RelatedTransactionId { get; init; }
}

public enum ThresholdType
{
    VATRegistration,
    QuarterlyLimit,
    AnnualLimit,
    ExportThreshold
}
