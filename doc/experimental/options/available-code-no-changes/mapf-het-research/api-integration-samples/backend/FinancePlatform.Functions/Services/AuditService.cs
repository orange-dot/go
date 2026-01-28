using System.Text.Json;
using EventStore.Client;

namespace FinancePlatform.Functions.Services;

public class AuditService
{
    private readonly EventStoreDbClient _eventStore;
    private readonly EventStoreClient _rawClient;
    private readonly ILogger<AuditService> _logger;

    public AuditService(
        EventStoreDbClient eventStore,
        EventStoreClient rawClient,
        ILogger<AuditService> logger)
    {
        _eventStore = eventStore;
        _rawClient = rawClient;
        _logger = logger;
    }

    public async Task<AuditTrail> GetAuditTrailAsync(
        AuditTrailQuery query,
        CancellationToken ct = default)
    {
        var entries = new List<AuditEntry>();

        try
        {
            // Build stream filter based on query
            var streamPrefix = query.StreamPrefix ?? string.Empty;

            if (!string.IsNullOrEmpty(query.EntityId))
            {
                // Read specific stream
                var streamName = $"{query.EntityType?.ToLowerInvariant() ?? "transaction"}-{query.EntityId}";
                var streamEntries = await ReadStreamAuditEntriesAsync(streamName, ct);
                entries.AddRange(streamEntries);
            }
            else
            {
                // Read from category stream or all streams with prefix
                var allEvents = await _eventStore.ReadAllEventsAsync(
                    streamPrefix,
                    query.MaxResults,
                    ct);

                foreach (var resolved in allEvents)
                {
                    var entry = MapToAuditEntry(resolved);
                    if (entry != null && MatchesFilter(entry, query))
                    {
                        entries.Add(entry);
                    }
                }
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error fetching audit trail");
        }

        // Apply filters
        var filtered = entries
            .Where(e => !query.FromDate.HasValue || e.Timestamp >= query.FromDate.Value)
            .Where(e => !query.ToDate.HasValue || e.Timestamp <= query.ToDate.Value)
            .Where(e => string.IsNullOrEmpty(query.UserId) || e.UserId == query.UserId)
            .Where(e => string.IsNullOrEmpty(query.EventType) || e.EventType == query.EventType)
            .OrderByDescending(e => e.Timestamp)
            .Take(query.MaxResults)
            .ToList();

        return new AuditTrail
        {
            Entries = filtered,
            TotalCount = filtered.Count,
            Query = query
        };
    }

    public async Task<EntityHistory> GetEntityHistoryAsync(
        string entityType,
        string entityId,
        CancellationToken ct = default)
    {
        var streamName = $"{entityType.ToLowerInvariant()}-{entityId}";
        var entries = await ReadStreamAuditEntriesAsync(streamName, ct);

        return new EntityHistory
        {
            EntityType = entityType,
            EntityId = entityId,
            Events = entries.OrderBy(e => e.Timestamp).ToList(),
            FirstEventAt = entries.MinBy(e => e.Timestamp)?.Timestamp,
            LastEventAt = entries.MaxBy(e => e.Timestamp)?.Timestamp,
            EventCount = entries.Count
        };
    }

    public async Task<object?> ReconstructStateAtAsync(
        string entityType,
        string entityId,
        DateTime pointInTime,
        CancellationToken ct = default)
    {
        var streamName = $"{entityType.ToLowerInvariant()}-{entityId}";

        try
        {
            var result = _rawClient.ReadStreamAsync(
                Direction.Forwards,
                streamName,
                StreamPosition.Start,
                cancellationToken: ct);

            var eventsUpToPoint = new List<(string Type, string Json)>();

            await foreach (var resolved in result)
            {
                var eventTime = resolved.Event.Created;
                if (eventTime > pointInTime)
                    break;

                eventsUpToPoint.Add((
                    resolved.Event.EventType,
                    System.Text.Encoding.UTF8.GetString(resolved.Event.Data.Span)
                ));
            }

            // Return the events for reconstruction
            // In a full implementation, this would apply events to rebuild state
            return new PointInTimeState
            {
                EntityType = entityType,
                EntityId = entityId,
                PointInTime = pointInTime,
                EventsApplied = eventsUpToPoint.Count,
                Events = eventsUpToPoint.Select(e => new
                {
                    Type = e.Type,
                    Data = JsonSerializer.Deserialize<JsonElement>(e.Json)
                }).ToList()
            };
        }
        catch (StreamNotFoundException)
        {
            return null;
        }
    }

    public async Task<ComplianceReport> GenerateComplianceReportAsync(
        DateOnly fromDate,
        DateOnly toDate,
        CancellationToken ct = default)
    {
        var query = new AuditTrailQuery
        {
            FromDate = fromDate.ToDateTime(TimeOnly.MinValue),
            ToDate = toDate.ToDateTime(TimeOnly.MaxValue),
            MaxResults = 10000
        };

        var auditTrail = await GetAuditTrailAsync(query, ct);

        var eventsByType = auditTrail.Entries
            .GroupBy(e => e.EventType)
            .ToDictionary(g => g.Key, g => g.Count());

        var eventsByUser = auditTrail.Entries
            .Where(e => !string.IsNullOrEmpty(e.UserId))
            .GroupBy(e => e.UserId!)
            .ToDictionary(g => g.Key, g => g.Count());

        var eventsByDay = auditTrail.Entries
            .GroupBy(e => e.Timestamp.Date)
            .OrderBy(g => g.Key)
            .ToDictionary(g => g.Key, g => g.Count());

        return new ComplianceReport
        {
            FromDate = fromDate,
            ToDate = toDate,
            TotalEvents = auditTrail.TotalCount,
            EventsByType = eventsByType,
            EventsByUser = eventsByUser,
            EventsByDay = eventsByDay,
            GeneratedAt = DateTime.UtcNow
        };
    }

    private async Task<List<AuditEntry>> ReadStreamAuditEntriesAsync(
        string streamName,
        CancellationToken ct)
    {
        var entries = new List<AuditEntry>();

        try
        {
            var result = _rawClient.ReadStreamAsync(
                Direction.Forwards,
                streamName,
                StreamPosition.Start,
                cancellationToken: ct);

            await foreach (var resolved in result)
            {
                var entry = MapToAuditEntry(resolved);
                if (entry != null)
                {
                    entries.Add(entry);
                }
            }
        }
        catch (StreamNotFoundException)
        {
            _logger.LogDebug("Stream {StreamName} not found", streamName);
        }

        return entries;
    }

    private AuditEntry? MapToAuditEntry(ResolvedEvent resolved)
    {
        try
        {
            var eventData = System.Text.Encoding.UTF8.GetString(resolved.Event.Data.Span);
            var metadata = resolved.Event.Metadata.Length > 0
                ? System.Text.Encoding.UTF8.GetString(resolved.Event.Metadata.Span)
                : null;

            string? userId = null;
            string? correlationId = null;

            if (!string.IsNullOrEmpty(metadata))
            {
                try
                {
                    using var doc = JsonDocument.Parse(metadata);
                    if (doc.RootElement.TryGetProperty("UserId", out var userIdProp))
                        userId = userIdProp.GetString();
                    if (doc.RootElement.TryGetProperty("CorrelationId", out var corrProp))
                        correlationId = corrProp.GetString();
                }
                catch { }
            }

            return new AuditEntry
            {
                EventId = resolved.Event.EventId.ToString(),
                StreamName = resolved.Event.EventStreamId,
                EventType = resolved.Event.EventType,
                Timestamp = resolved.Event.Created,
                UserId = userId,
                CorrelationId = correlationId,
                Data = eventData,
                Version = (long)resolved.Event.EventNumber.ToUInt64()
            };
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Failed to map event to audit entry");
            return null;
        }
    }

    private static bool MatchesFilter(AuditEntry entry, AuditTrailQuery query)
    {
        if (!string.IsNullOrEmpty(query.StreamPrefix) &&
            !entry.StreamName.StartsWith(query.StreamPrefix))
            return false;

        return true;
    }
}

public class AuditTrailQuery
{
    public string? StreamPrefix { get; init; }
    public string? EntityType { get; init; }
    public string? EntityId { get; init; }
    public string? EventType { get; init; }
    public string? UserId { get; init; }
    public DateTime? FromDate { get; init; }
    public DateTime? ToDate { get; init; }
    public int MaxResults { get; init; } = 100;
}

public class AuditEntry
{
    public string EventId { get; init; } = string.Empty;
    public string StreamName { get; init; } = string.Empty;
    public string EventType { get; init; } = string.Empty;
    public DateTime Timestamp { get; init; }
    public string? UserId { get; init; }
    public string? CorrelationId { get; init; }
    public string Data { get; init; } = string.Empty;
    public long Version { get; init; }
}

public class AuditTrail
{
    public List<AuditEntry> Entries { get; init; } = new();
    public int TotalCount { get; init; }
    public AuditTrailQuery Query { get; init; } = new();
}

public class EntityHistory
{
    public string EntityType { get; init; } = string.Empty;
    public string EntityId { get; init; } = string.Empty;
    public List<AuditEntry> Events { get; init; } = new();
    public DateTime? FirstEventAt { get; init; }
    public DateTime? LastEventAt { get; init; }
    public int EventCount { get; init; }
}

public class PointInTimeState
{
    public string EntityType { get; init; } = string.Empty;
    public string EntityId { get; init; } = string.Empty;
    public DateTime PointInTime { get; init; }
    public int EventsApplied { get; init; }
    public object? Events { get; init; }
}

public class ComplianceReport
{
    public DateOnly FromDate { get; init; }
    public DateOnly ToDate { get; init; }
    public int TotalEvents { get; init; }
    public Dictionary<string, int> EventsByType { get; init; } = new();
    public Dictionary<string, int> EventsByUser { get; init; } = new();
    public Dictionary<DateTime, int> EventsByDay { get; init; } = new();
    public DateTime GeneratedAt { get; init; }
}
