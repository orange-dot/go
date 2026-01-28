using FinancePlatform.Functions.Domain.Events;
using FinancePlatform.Functions.Domain.ValueObjects;

namespace FinancePlatform.Functions.Domain.Aggregates;

public class TransactionAggregate
{
    public Guid Id { get; private set; }
    public Money Amount { get; private set; }
    public string Description { get; private set; } = string.Empty;
    public string? Category { get; private set; }
    public string? SourceSystem { get; private set; }
    public string? Reference { get; private set; }
    public TransactionStatus Status { get; private set; }
    public string CreatedBy { get; private set; } = string.Empty;
    public DateTime CreatedAt { get; private set; }
    public string? ApprovedBy { get; private set; }
    public DateTime? ApprovedAt { get; private set; }
    public string? RejectedBy { get; private set; }
    public string? RejectionReason { get; private set; }
    public DateTime? RejectedAt { get; private set; }
    public int Version { get; private set; }

    private readonly List<TransactionEvent> _uncommittedEvents = new();
    public IReadOnlyList<TransactionEvent> UncommittedEvents => _uncommittedEvents.AsReadOnly();

    public static string GetStreamName(Guid id) => $"transaction-{id}";

    public static TransactionAggregate Create(
        Guid id,
        Money amount,
        string description,
        string createdBy,
        string? category = null,
        string? sourceSystem = null,
        string? reference = null)
    {
        var aggregate = new TransactionAggregate();
        var @event = new TransactionCreated
        {
            TransactionId = id,
            Amount = amount,
            Description = description,
            Category = category,
            SourceSystem = sourceSystem,
            Reference = reference,
            CreatedBy = createdBy
        };

        aggregate.Apply(@event);
        aggregate._uncommittedEvents.Add(@event);
        return aggregate;
    }

    public void Approve(string approvedBy, string? notes = null)
    {
        if (Status != TransactionStatus.Pending)
            throw new InvalidOperationException($"Cannot approve transaction in {Status} status");

        var @event = new TransactionApproved
        {
            TransactionId = Id,
            ApprovedBy = approvedBy,
            Notes = notes
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public void Reject(string rejectedBy, string reason)
    {
        if (Status != TransactionStatus.Pending)
            throw new InvalidOperationException($"Cannot reject transaction in {Status} status");

        var @event = new TransactionRejected
        {
            TransactionId = Id,
            RejectedBy = rejectedBy,
            Reason = reason
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public void Categorize(string category, string categorizedBy)
    {
        var @event = new TransactionCategorized
        {
            TransactionId = Id,
            Category = category,
            PreviousCategory = Category,
            CategorizedBy = categorizedBy
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public void CorrectAmount(Money correctedAmount, string correctedBy, string reason)
    {
        if (Status == TransactionStatus.Voided)
            throw new InvalidOperationException("Cannot correct voided transaction");

        var @event = new TransactionAmountCorrected
        {
            TransactionId = Id,
            OriginalAmount = Amount,
            CorrectedAmount = correctedAmount,
            CorrectedBy = correctedBy,
            Reason = reason
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public void Void(string voidedBy, string reason)
    {
        if (Status == TransactionStatus.Voided)
            throw new InvalidOperationException("Transaction already voided");

        var @event = new TransactionVoided
        {
            TransactionId = Id,
            VoidedBy = voidedBy,
            Reason = reason
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public void ClearUncommittedEvents() => _uncommittedEvents.Clear();

    public void LoadFromHistory(IEnumerable<TransactionEvent> history)
    {
        foreach (var @event in history)
        {
            Apply(@event);
            Version++;
        }
    }

    private void Apply(TransactionEvent @event)
    {
        switch (@event)
        {
            case TransactionCreated created:
                Id = created.TransactionId;
                Amount = created.Amount;
                Description = created.Description;
                Category = created.Category;
                SourceSystem = created.SourceSystem;
                Reference = created.Reference;
                CreatedBy = created.CreatedBy;
                CreatedAt = created.Timestamp;
                Status = TransactionStatus.Pending;
                break;

            case TransactionApproved approved:
                Status = TransactionStatus.Approved;
                ApprovedBy = approved.ApprovedBy;
                ApprovedAt = approved.Timestamp;
                break;

            case TransactionRejected rejected:
                Status = TransactionStatus.Rejected;
                RejectedBy = rejected.RejectedBy;
                RejectionReason = rejected.Reason;
                RejectedAt = rejected.Timestamp;
                break;

            case TransactionCategorized categorized:
                Category = categorized.Category;
                break;

            case TransactionAmountCorrected corrected:
                Amount = corrected.CorrectedAmount;
                break;

            case TransactionVoided:
                Status = TransactionStatus.Voided;
                break;
        }
    }
}

public enum TransactionStatus
{
    Pending,
    Approved,
    Rejected,
    Voided
}
