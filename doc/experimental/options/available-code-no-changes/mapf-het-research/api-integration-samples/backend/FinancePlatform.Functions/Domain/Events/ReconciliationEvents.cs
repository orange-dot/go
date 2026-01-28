namespace FinancePlatform.Functions.Domain.Events;

public abstract record ReconciliationEvent
{
    public Guid BatchId { get; init; }
    public DateTime Timestamp { get; init; } = DateTime.UtcNow;
    public string EventType => GetType().Name;
}

public record ReconciliationStarted : ReconciliationEvent
{
    public required string SourceA { get; init; }
    public required string SourceB { get; init; }
    public required int TransactionCount { get; init; }
    public required string StartedBy { get; init; }
}

public record TransactionsMatched : ReconciliationEvent
{
    public required Guid TransactionIdA { get; init; }
    public required Guid TransactionIdB { get; init; }
    public required decimal MatchScore { get; init; }
    public MatchType MatchType { get; init; }
}

public record MatchingFailed : ReconciliationEvent
{
    public required Guid TransactionId { get; init; }
    public required string Source { get; init; }
    public required string Reason { get; init; }
    public List<MatchCandidate>? Candidates { get; init; }
}

public record ReconciliationCompleted : ReconciliationEvent
{
    public required int TotalTransactions { get; init; }
    public required int MatchedCount { get; init; }
    public required int UnmatchedCount { get; init; }
    public required TimeSpan Duration { get; init; }
}

public record ManualMatchApplied : ReconciliationEvent
{
    public required Guid TransactionIdA { get; init; }
    public required Guid TransactionIdB { get; init; }
    public required string MatchedBy { get; init; }
    public string? Notes { get; init; }
}

public record TransactionExcluded : ReconciliationEvent
{
    public required Guid TransactionId { get; init; }
    public required string ExcludedBy { get; init; }
    public required string Reason { get; init; }
}

public record MatchCandidate
{
    public Guid TransactionId { get; init; }
    public decimal Score { get; init; }
    public string? DifferenceReason { get; init; }
}

public enum MatchType
{
    Exact,
    Fuzzy,
    Manual
}
