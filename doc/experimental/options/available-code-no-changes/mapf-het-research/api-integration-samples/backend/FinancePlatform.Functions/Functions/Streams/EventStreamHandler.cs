using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using FinancePlatform.Functions.Services;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace FinancePlatform.Functions.Functions.Streams;

public class EventStreamHandler
{
    private readonly EventStoreDbClient _eventStore;
    private readonly ILogger<EventStreamHandler> _logger;
    private readonly JsonSerializerOptions _jsonOptions;

    public EventStreamHandler(
        EventStoreDbClient eventStore,
        ILogger<EventStreamHandler> logger)
    {
        _eventStore = eventStore;
        _logger = logger;
        _jsonOptions = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = false
        };
    }

    [Function("GetRecentEvents")]
    public async Task<IActionResult> GetRecentEvents(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "events/recent")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var streamPrefix = req.Query["prefix"].FirstOrDefault() ?? "";
            var limitStr = req.Query["limit"].FirstOrDefault();
            var limit = int.TryParse(limitStr, out var l) ? Math.Min(l, 100) : 50;

            var events = await _eventStore.ReadAllEventsAsync(streamPrefix, limit, ct);

            var result = events.Select(e => new
            {
                eventId = e.Event.EventId.ToString(),
                streamName = e.Event.EventStreamId,
                eventType = e.Event.EventType,
                timestamp = e.Event.Created,
                position = e.Event.Position.CommitPosition,
                data = TryParseJson(Encoding.UTF8.GetString(e.Event.Data.Span))
            });

            return new OkObjectResult(new
            {
                events = result,
                count = events.Count,
                prefix = streamPrefix
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting recent events");
            return new StatusCodeResult(500);
        }
    }

    [Function("GetStreamEvents")]
    public async Task<IActionResult> GetStreamEvents(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "events/stream/{streamName}")] HttpRequest req,
        string streamName,
        CancellationToken ct)
    {
        try
        {
            if (string.IsNullOrEmpty(streamName))
            {
                return new BadRequestObjectResult(new { error = "Stream name required" });
            }

            var events = await _eventStore.ReadStreamAsync<object>(streamName, ct);
            var revision = await _eventStore.GetStreamRevisionAsync(streamName, ct);

            return new OkObjectResult(new
            {
                streamName,
                revision,
                eventCount = events.Count,
                events = events.Select((e, i) => new
                {
                    version = i,
                    data = e
                })
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting stream events for {StreamName}", streamName);
            return new StatusCodeResult(500);
        }
    }

    [Function("ServerSentEvents")]
    public async Task<IActionResult> ServerSentEvents(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "events/sse")] HttpRequest req,
        CancellationToken ct)
    {
        // Note: Server-Sent Events require special handling in Azure Functions
        // This is a simplified implementation that returns recent events as SSE format
        // For real-time streaming, consider using SignalR or WebPubSub

        try
        {
            var streamPrefix = req.Query["prefix"].FirstOrDefault() ?? "transaction-";

            // Set SSE headers
            req.HttpContext.Response.Headers.Append("Content-Type", "text/event-stream");
            req.HttpContext.Response.Headers.Append("Cache-Control", "no-cache");
            req.HttpContext.Response.Headers.Append("Connection", "keep-alive");

            var events = await _eventStore.ReadAllEventsAsync(streamPrefix, 20, ct);

            var sb = new StringBuilder();
            foreach (var e in events.OrderBy(ev => ev.Event.Created))
            {
                var data = new
                {
                    eventId = e.Event.EventId.ToString(),
                    streamName = e.Event.EventStreamId,
                    eventType = e.Event.EventType,
                    timestamp = e.Event.Created,
                    data = TryParseJson(Encoding.UTF8.GetString(e.Event.Data.Span))
                };

                sb.AppendLine($"id: {e.Event.EventId}");
                sb.AppendLine($"event: {e.Event.EventType}");
                sb.AppendLine($"data: {JsonSerializer.Serialize(data, _jsonOptions)}");
                sb.AppendLine();
            }

            return new ContentResult
            {
                Content = sb.ToString(),
                ContentType = "text/event-stream",
                StatusCode = 200
            };
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error in SSE endpoint");
            return new StatusCodeResult(500);
        }
    }

    [Function("EventStats")]
    public async Task<IActionResult> GetEventStats(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "events/stats")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var stats = new Dictionary<string, StreamStats>();

            // Get stats for each stream type
            foreach (var prefix in new[] { "transaction-", "ledger-", "tax-", "reconciliation-" })
            {
                var events = await _eventStore.ReadAllEventsAsync(prefix, 1000, ct);

                var streamCount = events.Select(e => e.Event.EventStreamId).Distinct().Count();
                var eventsByType = events
                    .GroupBy(e => e.Event.EventType)
                    .ToDictionary(g => g.Key, g => g.Count());

                var firstEvent = events.OrderBy(e => e.Event.Created).FirstOrDefault();
                var lastEvent = events.OrderByDescending(e => e.Event.Created).FirstOrDefault();

                stats[prefix.TrimEnd('-')] = new StreamStats
                {
                    StreamCount = streamCount,
                    TotalEvents = events.Count,
                    EventsByType = eventsByType,
                    FirstEventAt = firstEvent.Event?.Created,
                    LastEventAt = lastEvent.Event?.Created
                };
            }

            return new OkObjectResult(new
            {
                stats,
                generatedAt = DateTime.UtcNow
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting event stats");
            return new StatusCodeResult(500);
        }
    }

    private static object? TryParseJson(string json)
    {
        try
        {
            return JsonSerializer.Deserialize<JsonElement>(json);
        }
        catch
        {
            return json;
        }
    }

    private class StreamStats
    {
        public int StreamCount { get; set; }
        public int TotalEvents { get; set; }
        public Dictionary<string, int> EventsByType { get; set; } = new();
        public DateTime? FirstEventAt { get; set; }
        public DateTime? LastEventAt { get; set; }
    }
}
