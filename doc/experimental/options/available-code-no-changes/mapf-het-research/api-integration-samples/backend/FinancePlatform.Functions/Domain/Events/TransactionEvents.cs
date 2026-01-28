using FinancePlatform.Functions.Domain.ValueObjects;

namespace FinancePlatform.Functions.Domain.Events;

public abstract record TransactionEvent
{
    public Guid TransactionId { get; init; }
    public DateTime Timestamp { get; init; } = DateTime.UtcNow;
    public string EventType => GetType().Name;
}

public record TransactionCreated : TransactionEvent
{
    public required Money Amount { get; init; }
    public required string Description { get; init; }
    public string? Category { get; init; }
    public string? SourceSystem { get; init; }
    public string? Reference { get; init; }
    public required string CreatedBy { get; init; }
}

public record TransactionApproved : TransactionEvent
{
    public required string ApprovedBy { get; init; }
    public string? Notes { get; init; }
}

public record TransactionRejected : TransactionEvent
{
    public required string RejectedBy { get; init; }
    public required string Reason { get; init; }
}

public record TransactionCategorized : TransactionEvent
{
    public required string Category { get; init; }
    public string? PreviousCategory { get; init; }
    public required string CategorizedBy { get; init; }
}

public record TransactionAmountCorrected : TransactionEvent
{
    public required Money OriginalAmount { get; init; }
    public required Money CorrectedAmount { get; init; }
    public required string CorrectedBy { get; init; }
    public required string Reason { get; init; }
}

public record TransactionVoided : TransactionEvent
{
    public required string VoidedBy { get; init; }
    public required string Reason { get; init; }
}
