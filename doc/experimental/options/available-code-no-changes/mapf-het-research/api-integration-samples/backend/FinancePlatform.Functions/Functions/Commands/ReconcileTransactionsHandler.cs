using System.Net;
using FinancePlatform.Functions.Domain.ValueObjects;
using FinancePlatform.Functions.Services;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace FinancePlatform.Functions.Functions.Commands;

public class ReconcileTransactionsHandler
{
    private readonly ReconciliationService _reconciliationService;
    private readonly ILogger<ReconcileTransactionsHandler> _logger;

    public ReconcileTransactionsHandler(
        ReconciliationService reconciliationService,
        ILogger<ReconcileTransactionsHandler> logger)
    {
        _reconciliationService = reconciliationService;
        _logger = logger;
    }

    [Function("ReconcileTransactions")]
    public async Task<IActionResult> Run(
        [HttpTrigger(AuthorizationLevel.Anonymous, "post", Route = "reconciliation/start")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var command = await req.ReadFromJsonAsync<StartReconciliationCommand>(ct);

            if (command == null)
            {
                return new BadRequestObjectResult(new { error = "Invalid request body" });
            }

            if (command.SourceA == null || !command.SourceA.Any())
            {
                return new BadRequestObjectResult(new { error = "Source A transactions required" });
            }

            if (command.SourceB == null || !command.SourceB.Any())
            {
                return new BadRequestObjectResult(new { error = "Source B transactions required" });
            }

            var sourceATransactions = command.SourceA.Select(MapToReconcileTransaction).ToList();
            var sourceBTransactions = command.SourceB.Select(MapToReconcileTransaction).ToList();

            var result = await _reconciliationService.ReconcileAsync(
                sourceATransactions,
                sourceBTransactions,
                command.SourceAName ?? "Source A",
                command.SourceBName ?? "Source B",
                command.StartedBy ?? "system",
                ct);

            _logger.LogInformation(
                "Reconciliation {BatchId} completed: {Matched} matched, {Unmatched} unmatched",
                result.BatchId,
                result.MatchedPairs.Count,
                result.UnmatchedFromA.Count + result.UnmatchedFromB.Count);

            return new OkObjectResult(new
            {
                batchId = result.BatchId,
                sourceA = result.SourceA,
                sourceB = result.SourceB,
                totalTransactions = result.TotalTransactions,
                matched = result.MatchedPairs.Count,
                unmatched = result.UnmatchedFromA.Count + result.UnmatchedFromB.Count,
                matchRate = result.MatchRate,
                duration = result.Duration.TotalMilliseconds,
                matchedPairs = result.MatchedPairs.Select(p => new
                {
                    transactionIdA = p.TransactionA.Id,
                    transactionIdB = p.TransactionB.Id,
                    score = p.Score,
                    matchType = p.MatchType.ToString()
                }),
                unmatchedFromA = result.UnmatchedFromA.Select(t => new
                {
                    id = t.Id,
                    amount = t.Amount.Amount,
                    currency = t.Amount.Currency.Code,
                    date = t.Date,
                    reference = t.Reference
                }),
                unmatchedFromB = result.UnmatchedFromB.Select(t => new
                {
                    id = t.Id,
                    amount = t.Amount.Amount,
                    currency = t.Amount.Currency.Code,
                    date = t.Date,
                    reference = t.Reference
                })
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error during reconciliation");
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("ApplyManualMatch")]
    public async Task<IActionResult> ApplyManualMatch(
        [HttpTrigger(AuthorizationLevel.Anonymous, "post", Route = "reconciliation/{batchId}/match")] HttpRequest req,
        string batchId,
        CancellationToken ct)
    {
        if (!Guid.TryParse(batchId, out var batchGuid))
        {
            return new BadRequestObjectResult(new { error = "Invalid batch ID" });
        }

        var command = await req.ReadFromJsonAsync<ManualMatchCommand>(ct);

        if (command == null ||
            command.TransactionIdA == Guid.Empty ||
            command.TransactionIdB == Guid.Empty)
        {
            return new BadRequestObjectResult(new { error = "Transaction IDs required" });
        }

        await _reconciliationService.ApplyManualMatchAsync(
            batchGuid,
            command.TransactionIdA,
            command.TransactionIdB,
            command.MatchedBy ?? "system",
            command.Notes,
            ct);

        return new OkObjectResult(new
        {
            batchId = batchGuid,
            transactionIdA = command.TransactionIdA,
            transactionIdB = command.TransactionIdB,
            matchType = "Manual",
            matchedBy = command.MatchedBy ?? "system",
            matchedAt = DateTime.UtcNow
        });
    }

    private static TransactionToReconcile MapToReconcileTransaction(TransactionInput input)
    {
        return new TransactionToReconcile
        {
            Id = input.Id ?? Guid.NewGuid(),
            Amount = new Money(input.Amount, input.Currency ?? "EUR"),
            Date = input.Date ?? DateTime.UtcNow,
            Reference = input.Reference,
            Description = input.Description,
            Source = input.Source ?? "unknown"
        };
    }
}

public record StartReconciliationCommand
{
    public List<TransactionInput>? SourceA { get; init; }
    public List<TransactionInput>? SourceB { get; init; }
    public string? SourceAName { get; init; }
    public string? SourceBName { get; init; }
    public string? StartedBy { get; init; }
}

public record TransactionInput
{
    public Guid? Id { get; init; }
    public decimal Amount { get; init; }
    public string? Currency { get; init; }
    public DateTime? Date { get; init; }
    public string? Reference { get; init; }
    public string? Description { get; init; }
    public string? Source { get; init; }
}

public record ManualMatchCommand
{
    public Guid TransactionIdA { get; init; }
    public Guid TransactionIdB { get; init; }
    public string? MatchedBy { get; init; }
    public string? Notes { get; init; }
}
