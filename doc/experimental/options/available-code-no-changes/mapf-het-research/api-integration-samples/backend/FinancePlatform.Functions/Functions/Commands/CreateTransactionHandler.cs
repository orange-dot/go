using System.Net;
using FluentValidation;
using FinancePlatform.Functions.Domain.Aggregates;
using FinancePlatform.Functions.Domain.ValueObjects;
using FinancePlatform.Functions.Services;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace FinancePlatform.Functions.Functions.Commands;

public class CreateTransactionHandler
{
    private readonly EventStoreDbClient _eventStore;
    private readonly TaxCalculationService _taxService;
    private readonly ILogger<CreateTransactionHandler> _logger;

    public CreateTransactionHandler(
        EventStoreDbClient eventStore,
        TaxCalculationService taxService,
        ILogger<CreateTransactionHandler> logger)
    {
        _eventStore = eventStore;
        _taxService = taxService;
        _logger = logger;
    }

    [Function("CreateTransaction")]
    public async Task<IActionResult> Run(
        [HttpTrigger(AuthorizationLevel.Anonymous, "post", Route = "transactions")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var command = await req.ReadFromJsonAsync<CreateTransactionCommand>(ct);

            if (command == null)
            {
                return new BadRequestObjectResult(new { error = "Invalid request body" });
            }

            // Validate
            var validator = new CreateTransactionCommandValidator();
            var validationResult = await validator.ValidateAsync(command, ct);

            if (!validationResult.IsValid)
            {
                return new BadRequestObjectResult(new
                {
                    error = "Validation failed",
                    details = validationResult.Errors.Select(e => new
                    {
                        field = e.PropertyName,
                        message = e.ErrorMessage
                    })
                });
            }

            // Create transaction
            var transactionId = Guid.NewGuid();
            var amount = new Money(command.Amount, command.Currency);

            var transaction = TransactionAggregate.Create(
                transactionId,
                amount,
                command.Description,
                command.CreatedBy ?? "system",
                command.Category,
                command.SourceSystem,
                command.Reference);

            // Persist to EventStoreDB
            var streamName = TransactionAggregate.GetStreamName(transactionId);
            await _eventStore.AppendEventsAsync(streamName, transaction.UncommittedEvents, ct: ct);

            _logger.LogInformation(
                "Created transaction {TransactionId}: {Amount} {Currency}",
                transactionId,
                command.Amount,
                command.Currency);

            // Calculate tax if jurisdiction provided
            TaxCalculationResult? taxResult = null;
            if (!string.IsNullOrEmpty(command.Jurisdiction))
            {
                taxResult = await _taxService.CalculateTaxAsync(
                    transactionId,
                    amount,
                    command.Jurisdiction,
                    command.Category,
                    ct);
            }

            return new CreatedResult($"/api/transactions/{transactionId}", new
            {
                transactionId,
                amount = new { value = command.Amount, currency = command.Currency },
                description = command.Description,
                category = command.Category,
                status = "Pending",
                createdAt = DateTime.UtcNow,
                tax = taxResult != null ? new
                {
                    taxAmount = taxResult.TaxAmount.Amount,
                    taxRate = taxResult.TaxRate?.Rate,
                    periodId = taxResult.PeriodId
                } : null
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error creating transaction");
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("ApproveTransaction")]
    public async Task<IActionResult> Approve(
        [HttpTrigger(AuthorizationLevel.Anonymous, "post", Route = "transactions/{id}/approve")] HttpRequest req,
        string id,
        CancellationToken ct)
    {
        if (!Guid.TryParse(id, out var transactionId))
        {
            return new BadRequestObjectResult(new { error = "Invalid transaction ID" });
        }

        var command = await req.ReadFromJsonAsync<ApproveTransactionCommand>(ct);

        var streamName = TransactionAggregate.GetStreamName(transactionId);
        var events = await _eventStore.ReadStreamAsync<Domain.Events.TransactionEvent>(streamName, ct);

        if (!events.Any())
        {
            return new NotFoundObjectResult(new { error = "Transaction not found" });
        }

        var transaction = new TransactionAggregate();
        transaction.LoadFromHistory(events);

        try
        {
            transaction.Approve(command?.ApprovedBy ?? "system", command?.Notes);
            await _eventStore.AppendEventsAsync(streamName, transaction.UncommittedEvents, ct: ct);

            return new OkObjectResult(new
            {
                transactionId,
                status = "Approved",
                approvedBy = command?.ApprovedBy ?? "system",
                approvedAt = DateTime.UtcNow
            });
        }
        catch (InvalidOperationException ex)
        {
            return new ConflictObjectResult(new { error = ex.Message });
        }
    }

    [Function("RejectTransaction")]
    public async Task<IActionResult> Reject(
        [HttpTrigger(AuthorizationLevel.Anonymous, "post", Route = "transactions/{id}/reject")] HttpRequest req,
        string id,
        CancellationToken ct)
    {
        if (!Guid.TryParse(id, out var transactionId))
        {
            return new BadRequestObjectResult(new { error = "Invalid transaction ID" });
        }

        var command = await req.ReadFromJsonAsync<RejectTransactionCommand>(ct);

        if (string.IsNullOrEmpty(command?.Reason))
        {
            return new BadRequestObjectResult(new { error = "Rejection reason is required" });
        }

        var streamName = TransactionAggregate.GetStreamName(transactionId);
        var events = await _eventStore.ReadStreamAsync<Domain.Events.TransactionEvent>(streamName, ct);

        if (!events.Any())
        {
            return new NotFoundObjectResult(new { error = "Transaction not found" });
        }

        var transaction = new TransactionAggregate();
        transaction.LoadFromHistory(events);

        try
        {
            transaction.Reject(command.RejectedBy ?? "system", command.Reason);
            await _eventStore.AppendEventsAsync(streamName, transaction.UncommittedEvents, ct: ct);

            return new OkObjectResult(new
            {
                transactionId,
                status = "Rejected",
                rejectedBy = command.RejectedBy ?? "system",
                reason = command.Reason,
                rejectedAt = DateTime.UtcNow
            });
        }
        catch (InvalidOperationException ex)
        {
            return new ConflictObjectResult(new { error = ex.Message });
        }
    }
}

public record CreateTransactionCommand
{
    public decimal Amount { get; init; }
    public string Currency { get; init; } = "EUR";
    public string Description { get; init; } = string.Empty;
    public string? Category { get; init; }
    public string? SourceSystem { get; init; }
    public string? Reference { get; init; }
    public string? CreatedBy { get; init; }
    public string? Jurisdiction { get; init; }
}

public record ApproveTransactionCommand
{
    public string? ApprovedBy { get; init; }
    public string? Notes { get; init; }
}

public record RejectTransactionCommand
{
    public string? RejectedBy { get; init; }
    public string Reason { get; init; } = string.Empty;
}

public class CreateTransactionCommandValidator : AbstractValidator<CreateTransactionCommand>
{
    public CreateTransactionCommandValidator()
    {
        RuleFor(x => x.Amount)
            .NotEqual(0)
            .WithMessage("Amount cannot be zero");

        RuleFor(x => x.Currency)
            .NotEmpty()
            .Length(3)
            .Matches("^[A-Z]{3}$")
            .WithMessage("Currency must be a valid 3-letter ISO code");

        RuleFor(x => x.Description)
            .NotEmpty()
            .MaximumLength(500)
            .WithMessage("Description is required and must be under 500 characters");

        RuleFor(x => x.Category)
            .MaximumLength(100)
            .When(x => x.Category != null);

        RuleFor(x => x.Reference)
            .MaximumLength(100)
            .When(x => x.Reference != null);
    }
}
