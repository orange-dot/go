using FinancePlatform.Functions.Domain.Aggregates;
using FinancePlatform.Functions.Domain.Events;
using FinancePlatform.Functions.Domain.ValueObjects;

namespace FinancePlatform.Functions.Services;

public class ReconciliationService
{
    private readonly EventStoreDbClient _eventStore;
    private readonly ILogger<ReconciliationService> _logger;

    private const double ExactMatchThreshold = 1.0;
    private const double FuzzyMatchThreshold = 0.85;
    private const double MinimumMatchScore = 0.70;

    public ReconciliationService(EventStoreDbClient eventStore, ILogger<ReconciliationService> logger)
    {
        _eventStore = eventStore;
        _logger = logger;
    }

    public async Task<ReconciliationResult> ReconcileAsync(
        List<TransactionToReconcile> sourceA,
        List<TransactionToReconcile> sourceB,
        string sourceAName,
        string sourceBName,
        string startedBy,
        CancellationToken ct = default)
    {
        var batchId = Guid.NewGuid();
        var streamName = $"reconciliation-{batchId}";
        var startTime = DateTime.UtcNow;

        var events = new List<ReconciliationEvent>();

        // Start event
        events.Add(new ReconciliationStarted
        {
            BatchId = batchId,
            SourceA = sourceAName,
            SourceB = sourceBName,
            TransactionCount = sourceA.Count + sourceB.Count,
            StartedBy = startedBy
        });

        var matched = new List<MatchedPair>();
        var unmatchedA = new List<TransactionToReconcile>(sourceA);
        var unmatchedB = new List<TransactionToReconcile>(sourceB);

        // Try to match each transaction from source A
        foreach (var txA in sourceA.ToList())
        {
            var bestMatch = FindBestMatch(txA, unmatchedB);

            if (bestMatch != null && bestMatch.Score >= MinimumMatchScore)
            {
                var matchType = bestMatch.Score >= ExactMatchThreshold
                    ? MatchType.Exact
                    : MatchType.Fuzzy;

                events.Add(new TransactionsMatched
                {
                    BatchId = batchId,
                    TransactionIdA = txA.Id,
                    TransactionIdB = bestMatch.Transaction.Id,
                    MatchScore = (decimal)bestMatch.Score,
                    MatchType = matchType
                });

                matched.Add(new MatchedPair
                {
                    TransactionA = txA,
                    TransactionB = bestMatch.Transaction,
                    Score = bestMatch.Score,
                    MatchType = matchType
                });

                unmatchedA.Remove(txA);
                unmatchedB.Remove(bestMatch.Transaction);
            }
        }

        // Generate failure events for unmatched transactions
        foreach (var tx in unmatchedA)
        {
            var candidates = FindCandidates(tx, sourceB, 3);
            events.Add(new MatchingFailed
            {
                BatchId = batchId,
                TransactionId = tx.Id,
                Source = sourceAName,
                Reason = candidates.Any()
                    ? $"Best candidate score: {candidates.First().Score:P0}"
                    : "No similar transactions found",
                Candidates = candidates.Select(c => new MatchCandidate
                {
                    TransactionId = c.Transaction.Id,
                    Score = (decimal)c.Score
                }).ToList()
            });
        }

        foreach (var tx in unmatchedB)
        {
            events.Add(new MatchingFailed
            {
                BatchId = batchId,
                TransactionId = tx.Id,
                Source = sourceBName,
                Reason = "No matching transaction in source A"
            });
        }

        // Completion event
        events.Add(new ReconciliationCompleted
        {
            BatchId = batchId,
            TotalTransactions = sourceA.Count + sourceB.Count,
            MatchedCount = matched.Count,
            UnmatchedCount = unmatchedA.Count + unmatchedB.Count,
            Duration = DateTime.UtcNow - startTime
        });

        // Persist all events
        await _eventStore.AppendEventsAsync(streamName, events, ct: ct);

        _logger.LogInformation(
            "Reconciliation {BatchId} completed: {Matched} matched, {Unmatched} unmatched",
            batchId,
            matched.Count,
            unmatchedA.Count + unmatchedB.Count);

        return new ReconciliationResult
        {
            BatchId = batchId,
            SourceA = sourceAName,
            SourceB = sourceBName,
            TotalTransactions = sourceA.Count + sourceB.Count,
            MatchedPairs = matched,
            UnmatchedFromA = unmatchedA,
            UnmatchedFromB = unmatchedB,
            Duration = DateTime.UtcNow - startTime
        };
    }

    public async Task ApplyManualMatchAsync(
        Guid batchId,
        Guid transactionIdA,
        Guid transactionIdB,
        string matchedBy,
        string? notes = null,
        CancellationToken ct = default)
    {
        var streamName = $"reconciliation-{batchId}";

        var @event = new ManualMatchApplied
        {
            BatchId = batchId,
            TransactionIdA = transactionIdA,
            TransactionIdB = transactionIdB,
            MatchedBy = matchedBy,
            Notes = notes
        };

        await _eventStore.AppendEventsAsync(streamName, new[] { @event }, ct: ct);

        _logger.LogInformation(
            "Manual match applied in batch {BatchId}: {TxA} <-> {TxB}",
            batchId,
            transactionIdA,
            transactionIdB);
    }

    private MatchResult? FindBestMatch(
        TransactionToReconcile transaction,
        List<TransactionToReconcile> candidates)
    {
        MatchResult? bestMatch = null;

        foreach (var candidate in candidates)
        {
            var score = CalculateMatchScore(transaction, candidate);

            if (score >= MinimumMatchScore && (bestMatch == null || score > bestMatch.Score))
            {
                bestMatch = new MatchResult
                {
                    Transaction = candidate,
                    Score = score
                };

                // Early exit for exact match
                if (score >= ExactMatchThreshold)
                    break;
            }
        }

        return bestMatch;
    }

    private List<MatchResult> FindCandidates(
        TransactionToReconcile transaction,
        List<TransactionToReconcile> candidates,
        int maxCandidates)
    {
        return candidates
            .Select(c => new MatchResult
            {
                Transaction = c,
                Score = CalculateMatchScore(transaction, c)
            })
            .Where(r => r.Score > 0.3)
            .OrderByDescending(r => r.Score)
            .Take(maxCandidates)
            .ToList();
    }

    private double CalculateMatchScore(
        TransactionToReconcile a,
        TransactionToReconcile b)
    {
        double score = 0;
        double totalWeight = 0;

        // Amount match (weight: 40%)
        const double amountWeight = 0.4;
        if (a.Amount.Currency == b.Amount.Currency)
        {
            var amountDiff = Math.Abs(a.Amount.Amount - b.Amount.Amount);
            var maxAmount = Math.Max(Math.Abs(a.Amount.Amount), Math.Abs(b.Amount.Amount));

            if (maxAmount > 0)
            {
                var amountScore = 1 - (double)(amountDiff / maxAmount);
                score += amountScore * amountWeight;
            }
            else
            {
                score += amountWeight; // Both zero
            }
        }
        totalWeight += amountWeight;

        // Date match (weight: 25%)
        const double dateWeight = 0.25;
        var daysDiff = Math.Abs((a.Date - b.Date).TotalDays);
        var dateScore = Math.Max(0, 1 - (daysDiff / 7)); // Full score if same day, decreases over week
        score += dateScore * dateWeight;
        totalWeight += dateWeight;

        // Reference match (weight: 25%)
        const double referenceWeight = 0.25;
        if (!string.IsNullOrEmpty(a.Reference) && !string.IsNullOrEmpty(b.Reference))
        {
            var refScore = CalculateStringSimilarity(a.Reference, b.Reference);
            score += refScore * referenceWeight;
            totalWeight += referenceWeight;
        }

        // Description match (weight: 10%)
        const double descWeight = 0.1;
        if (!string.IsNullOrEmpty(a.Description) && !string.IsNullOrEmpty(b.Description))
        {
            var descScore = CalculateStringSimilarity(a.Description, b.Description);
            score += descScore * descWeight;
            totalWeight += descWeight;
        }

        return totalWeight > 0 ? score / totalWeight * (totalWeight / 1.0) : 0;
    }

    private static double CalculateStringSimilarity(string a, string b)
    {
        // Levenshtein distance-based similarity
        var distance = LevenshteinDistance(a.ToLowerInvariant(), b.ToLowerInvariant());
        var maxLength = Math.Max(a.Length, b.Length);
        return maxLength > 0 ? 1 - ((double)distance / maxLength) : 1;
    }

    private static int LevenshteinDistance(string a, string b)
    {
        var m = a.Length;
        var n = b.Length;
        var d = new int[m + 1, n + 1];

        for (var i = 0; i <= m; i++) d[i, 0] = i;
        for (var j = 0; j <= n; j++) d[0, j] = j;

        for (var i = 1; i <= m; i++)
        {
            for (var j = 1; j <= n; j++)
            {
                var cost = a[i - 1] == b[j - 1] ? 0 : 1;
                d[i, j] = Math.Min(
                    Math.Min(d[i - 1, j] + 1, d[i, j - 1] + 1),
                    d[i - 1, j - 1] + cost);
            }
        }

        return d[m, n];
    }
}

public class TransactionToReconcile
{
    public Guid Id { get; init; }
    public Money Amount { get; init; }
    public DateTime Date { get; init; }
    public string? Reference { get; init; }
    public string? Description { get; init; }
    public string Source { get; init; } = string.Empty;
}

public class MatchResult
{
    public TransactionToReconcile Transaction { get; init; } = null!;
    public double Score { get; init; }
}

public class MatchedPair
{
    public TransactionToReconcile TransactionA { get; init; } = null!;
    public TransactionToReconcile TransactionB { get; init; } = null!;
    public double Score { get; init; }
    public MatchType MatchType { get; init; }
}

public class ReconciliationResult
{
    public Guid BatchId { get; init; }
    public string SourceA { get; init; } = string.Empty;
    public string SourceB { get; init; } = string.Empty;
    public int TotalTransactions { get; init; }
    public List<MatchedPair> MatchedPairs { get; init; } = new();
    public List<TransactionToReconcile> UnmatchedFromA { get; init; } = new();
    public List<TransactionToReconcile> UnmatchedFromB { get; init; } = new();
    public TimeSpan Duration { get; init; }

    public double MatchRate => TotalTransactions > 0
        ? (double)MatchedPairs.Count * 2 / TotalTransactions
        : 0;
}
