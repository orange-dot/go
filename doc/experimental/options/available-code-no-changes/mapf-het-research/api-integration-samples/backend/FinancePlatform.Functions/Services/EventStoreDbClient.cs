using System.Text.Json;
using EventStore.Client;
using FinancePlatform.Functions.Domain.Events;

namespace FinancePlatform.Functions.Services;

public class EventStoreDbClient
{
    private readonly EventStoreClient _client;
    private readonly ILogger<EventStoreDbClient> _logger;
    private readonly JsonSerializerOptions _jsonOptions;

    public EventStoreDbClient(EventStoreClient client, ILogger<EventStoreDbClient> logger)
    {
        _client = client;
        _logger = logger;
        _jsonOptions = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = false
        };
    }

    public async Task AppendEventsAsync<TEvent>(
        string streamName,
        IEnumerable<TEvent> events,
        StreamRevision? expectedRevision = null,
        CancellationToken ct = default)
    {
        var eventDataList = events.Select(e => new EventData(
            Uuid.NewUuid(),
            e!.GetType().Name,
            JsonSerializer.SerializeToUtf8Bytes(e, _jsonOptions),
            JsonSerializer.SerializeToUtf8Bytes(new EventMetadata
            {
                EventType = e.GetType().Name,
                Timestamp = DateTime.UtcNow,
                CorrelationId = Guid.NewGuid().ToString()
            }, _jsonOptions)
        )).ToArray();

        if (expectedRevision.HasValue)
        {
            await _client.AppendToStreamAsync(
                streamName,
                expectedRevision.Value,
                eventDataList,
                cancellationToken: ct);
        }
        else
        {
            await _client.AppendToStreamAsync(
                streamName,
                StreamState.Any,
                eventDataList,
                cancellationToken: ct);
        }

        _logger.LogInformation(
            "Appended {Count} events to stream {StreamName}",
            eventDataList.Length,
            streamName);
    }

    public async Task<List<TEvent>> ReadStreamAsync<TEvent>(
        string streamName,
        CancellationToken ct = default) where TEvent : class
    {
        var events = new List<TEvent>();

        try
        {
            var result = _client.ReadStreamAsync(
                Direction.Forwards,
                streamName,
                StreamPosition.Start,
                cancellationToken: ct);

            await foreach (var resolvedEvent in result)
            {
                var eventType = resolvedEvent.Event.EventType;
                var json = System.Text.Encoding.UTF8.GetString(resolvedEvent.Event.Data.Span);

                var @event = DeserializeEvent<TEvent>(eventType, json);
                if (@event != null)
                {
                    events.Add(@event);
                }
            }
        }
        catch (StreamNotFoundException)
        {
            _logger.LogDebug("Stream {StreamName} not found", streamName);
        }

        return events;
    }

    public async Task<List<ResolvedEvent>> ReadAllEventsAsync(
        string streamPrefix,
        int maxCount = 100,
        CancellationToken ct = default)
    {
        var events = new List<ResolvedEvent>();

        try
        {
            // Read from $all stream with filter
            var filter = StreamFilter.Prefix(streamPrefix);
            var result = _client.ReadAllAsync(
                Direction.Backwards,
                Position.End,
                maxCount,
                filterOptions: new SubscriptionFilterOptions(filter),
                cancellationToken: ct);

            await foreach (var resolvedEvent in result)
            {
                events.Add(resolvedEvent);
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error reading events with prefix {Prefix}", streamPrefix);
        }

        return events;
    }

    public async IAsyncEnumerable<TEvent> SubscribeToStreamAsync<TEvent>(
        string streamName,
        [System.Runtime.CompilerServices.EnumeratorCancellation] CancellationToken ct = default)
        where TEvent : class
    {
        var subscription = _client.SubscribeToStream(
            streamName,
            FromStream.End,
            cancellationToken: ct);

        await foreach (var message in subscription.Messages.WithCancellation(ct))
        {
            if (message is StreamMessage.Event eventMessage)
            {
                var eventType = eventMessage.ResolvedEvent.Event.EventType;
                var json = System.Text.Encoding.UTF8.GetString(
                    eventMessage.ResolvedEvent.Event.Data.Span);

                var @event = DeserializeEvent<TEvent>(eventType, json);
                if (@event != null)
                {
                    yield return @event;
                }
            }
        }
    }

    public async Task<long> GetStreamRevisionAsync(string streamName, CancellationToken ct = default)
    {
        try
        {
            var result = _client.ReadStreamAsync(
                Direction.Backwards,
                streamName,
                StreamPosition.End,
                1,
                cancellationToken: ct);

            var lastEvent = await result.FirstOrDefaultAsync(ct);
            return (long)(lastEvent.Event?.EventNumber.ToUInt64() ?? 0);
        }
        catch (StreamNotFoundException)
        {
            return -1;
        }
    }

    private TEvent? DeserializeEvent<TEvent>(string eventType, string json) where TEvent : class
    {
        // Map event type names to actual types
        var type = eventType switch
        {
            nameof(TransactionCreated) => typeof(TransactionCreated),
            nameof(TransactionApproved) => typeof(TransactionApproved),
            nameof(TransactionRejected) => typeof(TransactionRejected),
            nameof(ReconciliationStarted) => typeof(ReconciliationStarted),
            nameof(TransactionsMatched) => typeof(TransactionsMatched),
            nameof(MatchingFailed) => typeof(MatchingFailed),
            nameof(TaxCalculated) => typeof(TaxCalculated),
            nameof(TaxReportGenerated) => typeof(TaxReportGenerated),
            nameof(LedgerEntryPosted) => typeof(LedgerEntryPosted),
            nameof(BalanceUpdated) => typeof(BalanceUpdated),
            nameof(CurrencyConverted) => typeof(CurrencyConverted),
            _ => null
        };

        if (type == null || !typeof(TEvent).IsAssignableFrom(type))
        {
            _logger.LogWarning("Unknown or incompatible event type: {EventType}", eventType);
            return null;
        }

        return JsonSerializer.Deserialize(json, type, _jsonOptions) as TEvent;
    }
}

public class EventMetadata
{
    public string EventType { get; set; } = string.Empty;
    public DateTime Timestamp { get; set; }
    public string? CorrelationId { get; set; }
    public string? CausationId { get; set; }
    public string? UserId { get; set; }
}
