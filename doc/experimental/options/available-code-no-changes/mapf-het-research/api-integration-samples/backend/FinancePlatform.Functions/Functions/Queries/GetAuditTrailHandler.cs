using System.Net;
using FinancePlatform.Functions.Services;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace FinancePlatform.Functions.Functions.Queries;

public class GetAuditTrailHandler
{
    private readonly AuditService _auditService;
    private readonly ILogger<GetAuditTrailHandler> _logger;

    public GetAuditTrailHandler(
        AuditService auditService,
        ILogger<GetAuditTrailHandler> logger)
    {
        _auditService = auditService;
        _logger = logger;
    }

    [Function("GetAuditTrail")]
    public async Task<IActionResult> Run(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "audit-trail")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var query = new AuditTrailQuery
            {
                StreamPrefix = req.Query["streamPrefix"].FirstOrDefault(),
                EntityType = req.Query["entityType"].FirstOrDefault(),
                EntityId = req.Query["entityId"].FirstOrDefault(),
                EventType = req.Query["eventType"].FirstOrDefault(),
                UserId = req.Query["userId"].FirstOrDefault(),
                FromDate = TryParseDateTime(req.Query["fromDate"].FirstOrDefault()),
                ToDate = TryParseDateTime(req.Query["toDate"].FirstOrDefault()),
                MaxResults = TryParseInt(req.Query["limit"].FirstOrDefault()) ?? 100
            };

            var result = await _auditService.GetAuditTrailAsync(query, ct);

            return new OkObjectResult(new
            {
                entries = result.Entries.Select(e => new
                {
                    eventId = e.EventId,
                    streamName = e.StreamName,
                    eventType = e.EventType,
                    timestamp = e.Timestamp,
                    userId = e.UserId,
                    correlationId = e.CorrelationId,
                    data = TryParseJson(e.Data),
                    version = e.Version
                }),
                totalCount = result.TotalCount,
                query = new
                {
                    streamPrefix = query.StreamPrefix,
                    entityType = query.EntityType,
                    entityId = query.EntityId,
                    eventType = query.EventType,
                    fromDate = query.FromDate,
                    toDate = query.ToDate,
                    maxResults = query.MaxResults
                }
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting audit trail");
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("GetEntityHistory")]
    public async Task<IActionResult> GetHistory(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "audit-trail/{entityType}/{entityId}")] HttpRequest req,
        string entityType,
        string entityId,
        CancellationToken ct)
    {
        try
        {
            var result = await _auditService.GetEntityHistoryAsync(entityType, entityId, ct);

            return new OkObjectResult(new
            {
                entityType = result.EntityType,
                entityId = result.EntityId,
                events = result.Events.Select(e => new
                {
                    eventId = e.EventId,
                    eventType = e.EventType,
                    timestamp = e.Timestamp,
                    userId = e.UserId,
                    data = TryParseJson(e.Data),
                    version = e.Version
                }),
                firstEventAt = result.FirstEventAt,
                lastEventAt = result.LastEventAt,
                eventCount = result.EventCount
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting entity history for {EntityType}/{EntityId}", entityType, entityId);
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("ReconstructState")]
    public async Task<IActionResult> ReconstructState(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "audit-trail/{entityType}/{entityId}/state")] HttpRequest req,
        string entityType,
        string entityId,
        CancellationToken ct)
    {
        try
        {
            var pointInTimeStr = req.Query["pointInTime"].FirstOrDefault();
            var pointInTime = TryParseDateTime(pointInTimeStr) ?? DateTime.UtcNow;

            var result = await _auditService.ReconstructStateAtAsync(
                entityType,
                entityId,
                pointInTime,
                ct);

            if (result == null)
            {
                return new NotFoundObjectResult(new
                {
                    error = $"Entity {entityType}/{entityId} not found"
                });
            }

            return new OkObjectResult(result);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error reconstructing state for {EntityType}/{EntityId}", entityType, entityId);
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("GetComplianceReport")]
    public async Task<IActionResult> GetComplianceReport(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "audit-trail/compliance-report")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var fromDateStr = req.Query["fromDate"].FirstOrDefault();
            var toDateStr = req.Query["toDate"].FirstOrDefault();

            var fromDate = TryParseDateOnly(fromDateStr) ?? DateOnly.FromDateTime(DateTime.UtcNow.AddMonths(-1));
            var toDate = TryParseDateOnly(toDateStr) ?? DateOnly.FromDateTime(DateTime.UtcNow);

            var result = await _auditService.GenerateComplianceReportAsync(fromDate, toDate, ct);

            return new OkObjectResult(new
            {
                fromDate = result.FromDate,
                toDate = result.ToDate,
                totalEvents = result.TotalEvents,
                eventsByType = result.EventsByType,
                eventsByUser = result.EventsByUser,
                eventsByDay = result.EventsByDay.ToDictionary(
                    kvp => kvp.Key.ToString("yyyy-MM-dd"),
                    kvp => kvp.Value),
                generatedAt = result.GeneratedAt
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error generating compliance report");
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    private static DateTime? TryParseDateTime(string? value)
    {
        if (string.IsNullOrEmpty(value))
            return null;

        return DateTime.TryParse(value, out var result) ? result : null;
    }

    private static DateOnly? TryParseDateOnly(string? value)
    {
        if (string.IsNullOrEmpty(value))
            return null;

        return DateOnly.TryParse(value, out var result) ? result : null;
    }

    private static int? TryParseInt(string? value)
    {
        if (string.IsNullOrEmpty(value))
            return null;

        return int.TryParse(value, out var result) ? result : null;
    }

    private static object? TryParseJson(string json)
    {
        try
        {
            return System.Text.Json.JsonSerializer.Deserialize<System.Text.Json.JsonElement>(json);
        }
        catch
        {
            return json;
        }
    }
}
