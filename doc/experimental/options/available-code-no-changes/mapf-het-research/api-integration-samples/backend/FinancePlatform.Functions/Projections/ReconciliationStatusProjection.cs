using FinancePlatform.Functions.Domain.Events;
using FinancePlatform.Functions.Services;

namespace FinancePlatform.Functions.Projections;

public class ReconciliationStatusProjection
{
    private readonly EventStoreDbClient _eventStore;
    private readonly ILogger<ReconciliationStatusProjection> _logger;

    public ReconciliationStatusProjection(
        EventStoreDbClient eventStore,
        ILogger<ReconciliationStatusProjection> logger)
    {
        _eventStore = eventStore;
        _logger = logger;
    }

    public async Task<ReconciliationStatusView> GetStatusAsync(
        Guid batchId,
        CancellationToken ct = default)
    {
        var streamName = $"reconciliation-{batchId}";
        var events = await _eventStore.ReadStreamAsync<ReconciliationEvent>(streamName, ct);

        var view = new ReconciliationStatusView { BatchId = batchId };
        var matchedPairs = new List<MatchedPairView>();
        var unmatchedItems = new List<UnmatchedItemView>();

        foreach (var @event in events)
        {
            switch (@event)
            {
                case ReconciliationStarted started:
                    view.SourceA = started.SourceA;
                    view.SourceB = started.SourceB;
                    view.TotalTransactions = started.TransactionCount;
                    view.StartedAt = started.Timestamp;
                    view.StartedBy = started.StartedBy;
                    view.Status = ReconciliationStatus.InProgress;
                    break;

                case TransactionsMatched matched:
                    matchedPairs.Add(new MatchedPairView
                    {
                        TransactionIdA = matched.TransactionIdA,
                        TransactionIdB = matched.TransactionIdB,
                        MatchScore = matched.MatchScore,
                        MatchType = matched.MatchType.ToString(),
                        MatchedAt = matched.Timestamp
                    });
                    break;

                case MatchingFailed failed:
                    unmatchedItems.Add(new UnmatchedItemView
                    {
                        TransactionId = failed.TransactionId,
                        Source = failed.Source,
                        Reason = failed.Reason,
                        Candidates = failed.Candidates?.Select(c => new CandidateView
                        {
                            TransactionId = c.TransactionId,
                            Score = c.Score
                        }).ToList() ?? new(),
                        FailedAt = failed.Timestamp
                    });
                    break;

                case ManualMatchApplied manual:
                    // Update to show it was manually matched
                    var existingUnmatched = unmatchedItems
                        .FirstOrDefault(u => u.TransactionId == manual.TransactionIdA
                            || u.TransactionId == manual.TransactionIdB);
                    if (existingUnmatched != null)
                    {
                        unmatchedItems.Remove(existingUnmatched);
                    }

                    matchedPairs.Add(new MatchedPairView
                    {
                        TransactionIdA = manual.TransactionIdA,
                        TransactionIdB = manual.TransactionIdB,
                        MatchScore = 1.0m,
                        MatchType = "Manual",
                        MatchedAt = manual.Timestamp,
                        MatchedBy = manual.MatchedBy,
                        Notes = manual.Notes
                    });
                    break;

                case TransactionExcluded excluded:
                    var toExclude = unmatchedItems
                        .FirstOrDefault(u => u.TransactionId == excluded.TransactionId);
                    if (toExclude != null)
                    {
                        toExclude.IsExcluded = true;
                        toExclude.ExcludedBy = excluded.ExcludedBy;
                        toExclude.ExclusionReason = excluded.Reason;
                    }
                    break;

                case ReconciliationCompleted completed:
                    view.Status = ReconciliationStatus.Completed;
                    view.CompletedAt = completed.Timestamp;
                    view.Duration = completed.Duration;
                    break;
            }
        }

        view.MatchedPairs = matchedPairs;
        view.UnmatchedItems = unmatchedItems.Where(u => !u.IsExcluded).ToList();
        view.ExcludedItems = unmatchedItems.Where(u => u.IsExcluded).ToList();
        view.MatchedCount = matchedPairs.Count;
        view.UnmatchedCount = view.UnmatchedItems.Count;
        view.MatchRate = view.TotalTransactions > 0
            ? (double)view.MatchedCount * 2 / view.TotalTransactions
            : 0;

        return view;
    }

    public async Task<List<ReconciliationSummary>> GetRecentBatchesAsync(
        int count = 10,
        CancellationToken ct = default)
    {
        var summaries = new List<ReconciliationSummary>();

        // This is simplified - in production, you'd maintain a summary stream
        // or use EventStoreDB projections
        var allEvents = await _eventStore.ReadAllEventsAsync("reconciliation-", count * 10, ct);

        var batchGroups = allEvents
            .Where(e => e.Event.EventType == nameof(ReconciliationStarted)
                     || e.Event.EventType == nameof(ReconciliationCompleted))
            .GroupBy(e => e.Event.EventStreamId)
            .Take(count);

        foreach (var group in batchGroups)
        {
            var batchIdStr = group.Key.Replace("reconciliation-", "");
            if (!Guid.TryParse(batchIdStr, out var batchId))
                continue;

            var status = await GetStatusAsync(batchId, ct);

            summaries.Add(new ReconciliationSummary
            {
                BatchId = batchId,
                SourceA = status.SourceA,
                SourceB = status.SourceB,
                Status = status.Status,
                TotalTransactions = status.TotalTransactions,
                MatchedCount = status.MatchedCount,
                UnmatchedCount = status.UnmatchedCount,
                MatchRate = status.MatchRate,
                StartedAt = status.StartedAt,
                CompletedAt = status.CompletedAt
            });
        }

        return summaries.OrderByDescending(s => s.StartedAt).ToList();
    }

    public async Task<ReconciliationAnalytics> GetAnalyticsAsync(
        DateOnly fromDate,
        DateOnly toDate,
        CancellationToken ct = default)
    {
        var batches = await GetRecentBatchesAsync(100, ct);

        var filteredBatches = batches
            .Where(b => b.StartedAt.HasValue &&
                DateOnly.FromDateTime(b.StartedAt.Value) >= fromDate &&
                DateOnly.FromDateTime(b.StartedAt.Value) <= toDate)
            .ToList();

        return new ReconciliationAnalytics
        {
            FromDate = fromDate,
            ToDate = toDate,
            TotalBatches = filteredBatches.Count,
            TotalTransactionsProcessed = filteredBatches.Sum(b => b.TotalTransactions),
            TotalMatched = filteredBatches.Sum(b => b.MatchedCount),
            TotalUnmatched = filteredBatches.Sum(b => b.UnmatchedCount),
            AverageMatchRate = filteredBatches.Any()
                ? filteredBatches.Average(b => b.MatchRate)
                : 0,
            CompletedBatches = filteredBatches.Count(b => b.Status == ReconciliationStatus.Completed),
            InProgressBatches = filteredBatches.Count(b => b.Status == ReconciliationStatus.InProgress),
            MatchRateOverTime = filteredBatches
                .Where(b => b.StartedAt.HasValue)
                .GroupBy(b => DateOnly.FromDateTime(b.StartedAt!.Value))
                .OrderBy(g => g.Key)
                .ToDictionary(
                    g => g.Key,
                    g => g.Average(b => b.MatchRate))
        };
    }
}

public class ReconciliationStatusView
{
    public Guid BatchId { get; init; }
    public string SourceA { get; set; } = string.Empty;
    public string SourceB { get; set; } = string.Empty;
    public ReconciliationStatus Status { get; set; }
    public int TotalTransactions { get; set; }
    public int MatchedCount { get; set; }
    public int UnmatchedCount { get; set; }
    public double MatchRate { get; set; }
    public List<MatchedPairView> MatchedPairs { get; set; } = new();
    public List<UnmatchedItemView> UnmatchedItems { get; set; } = new();
    public List<UnmatchedItemView> ExcludedItems { get; set; } = new();
    public DateTime? StartedAt { get; set; }
    public string? StartedBy { get; set; }
    public DateTime? CompletedAt { get; set; }
    public TimeSpan? Duration { get; set; }
}

public class MatchedPairView
{
    public Guid TransactionIdA { get; init; }
    public Guid TransactionIdB { get; init; }
    public decimal MatchScore { get; init; }
    public string MatchType { get; init; } = string.Empty;
    public DateTime MatchedAt { get; init; }
    public string? MatchedBy { get; init; }
    public string? Notes { get; init; }
}

public class UnmatchedItemView
{
    public Guid TransactionId { get; init; }
    public string Source { get; init; } = string.Empty;
    public string Reason { get; init; } = string.Empty;
    public List<CandidateView> Candidates { get; init; } = new();
    public DateTime FailedAt { get; init; }
    public bool IsExcluded { get; set; }
    public string? ExcludedBy { get; set; }
    public string? ExclusionReason { get; set; }
}

public class CandidateView
{
    public Guid TransactionId { get; init; }
    public decimal Score { get; init; }
}

public class ReconciliationSummary
{
    public Guid BatchId { get; init; }
    public string SourceA { get; init; } = string.Empty;
    public string SourceB { get; init; } = string.Empty;
    public ReconciliationStatus Status { get; init; }
    public int TotalTransactions { get; init; }
    public int MatchedCount { get; init; }
    public int UnmatchedCount { get; init; }
    public double MatchRate { get; init; }
    public DateTime? StartedAt { get; init; }
    public DateTime? CompletedAt { get; init; }
}

public class ReconciliationAnalytics
{
    public DateOnly FromDate { get; init; }
    public DateOnly ToDate { get; init; }
    public int TotalBatches { get; init; }
    public int TotalTransactionsProcessed { get; init; }
    public int TotalMatched { get; init; }
    public int TotalUnmatched { get; init; }
    public double AverageMatchRate { get; init; }
    public int CompletedBatches { get; init; }
    public int InProgressBatches { get; init; }
    public Dictionary<DateOnly, double> MatchRateOverTime { get; init; } = new();
}

public enum ReconciliationStatus
{
    Pending,
    InProgress,
    Completed,
    Failed
}
