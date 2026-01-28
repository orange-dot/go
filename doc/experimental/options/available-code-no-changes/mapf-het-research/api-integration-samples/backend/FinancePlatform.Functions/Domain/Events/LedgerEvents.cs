using FinancePlatform.Functions.Domain.ValueObjects;

namespace FinancePlatform.Functions.Domain.Events;

public abstract record LedgerEvent
{
    public string Currency { get; init; } = string.Empty;
    public DateTime Timestamp { get; init; } = DateTime.UtcNow;
    public string EventType => GetType().Name;
}

public record LedgerEntryPosted : LedgerEvent
{
    public required Guid EntryId { get; init; }
    public required Guid TransactionId { get; init; }
    public required string DebitAccount { get; init; }
    public required string CreditAccount { get; init; }
    public required Money Amount { get; init; }
    public string? Description { get; init; }
    public required string PostedBy { get; init; }
}

public record BalanceUpdated : LedgerEvent
{
    public required string AccountId { get; init; }
    public required Money PreviousBalance { get; init; }
    public required Money NewBalance { get; init; }
    public required Guid TriggeringEntryId { get; init; }
}

public record CurrencyConverted : LedgerEvent
{
    public required Guid ConversionId { get; init; }
    public required Money FromAmount { get; init; }
    public required Money ToAmount { get; init; }
    public required decimal ExchangeRate { get; init; }
    public required string RateSource { get; init; }
    public Guid? RelatedTransactionId { get; init; }
}

public record AccountCreated : LedgerEvent
{
    public required string AccountId { get; init; }
    public required string AccountName { get; init; }
    public required AccountType AccountType { get; init; }
    public required string CreatedBy { get; init; }
    public string? ParentAccountId { get; init; }
}

public record AccountClosed : LedgerEvent
{
    public required string AccountId { get; init; }
    public required string ClosedBy { get; init; }
    public required string Reason { get; init; }
    public Money? FinalBalance { get; init; }
}

public record JournalEntryPosted : LedgerEvent
{
    public required Guid JournalId { get; init; }
    public required List<LedgerLine> Lines { get; init; }
    public required string Description { get; init; }
    public required string PostedBy { get; init; }
    public DateOnly? EffectiveDate { get; init; }
}

public record LedgerLine
{
    public required string AccountId { get; init; }
    public required Money Debit { get; init; }
    public required Money Credit { get; init; }
}

public enum AccountType
{
    Asset,
    Liability,
    Equity,
    Revenue,
    Expense
}
