using FinancePlatform.Functions.Domain.Events;
using FinancePlatform.Functions.Domain.ValueObjects;

namespace FinancePlatform.Functions.Domain.Aggregates;

public class LedgerAggregate
{
    public string Currency { get; private set; } = string.Empty;
    private readonly Dictionary<string, Money> _balances = new();
    private readonly List<LedgerEntry> _entries = new();
    public IReadOnlyDictionary<string, Money> Balances => _balances.AsReadOnly();
    public IReadOnlyList<LedgerEntry> Entries => _entries.AsReadOnly();
    public int Version { get; private set; }

    private readonly List<LedgerEvent> _uncommittedEvents = new();
    public IReadOnlyList<LedgerEvent> UncommittedEvents => _uncommittedEvents.AsReadOnly();

    public static string GetStreamName(string currency) => $"ledger-{currency}";

    public static LedgerAggregate Create(string currency)
    {
        var aggregate = new LedgerAggregate { Currency = currency };
        return aggregate;
    }

    public void PostEntry(
        Guid transactionId,
        string debitAccount,
        string creditAccount,
        Money amount,
        string postedBy,
        string? description = null)
    {
        if (amount.Currency.Code != Currency)
            throw new InvalidOperationException(
                $"Cannot post {amount.Currency} entry to {Currency} ledger");

        var entryId = Guid.NewGuid();
        var @event = new LedgerEntryPosted
        {
            Currency = Currency,
            EntryId = entryId,
            TransactionId = transactionId,
            DebitAccount = debitAccount,
            CreditAccount = creditAccount,
            Amount = amount,
            Description = description,
            PostedBy = postedBy
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);

        // Generate balance update events
        EmitBalanceUpdate(debitAccount, entryId);
        EmitBalanceUpdate(creditAccount, entryId);
    }

    public void RecordConversion(
        Money fromAmount,
        Money toAmount,
        decimal exchangeRate,
        string rateSource,
        Guid? relatedTransactionId = null)
    {
        var @event = new CurrencyConverted
        {
            Currency = Currency,
            ConversionId = Guid.NewGuid(),
            FromAmount = fromAmount,
            ToAmount = toAmount,
            ExchangeRate = exchangeRate,
            RateSource = rateSource,
            RelatedTransactionId = relatedTransactionId
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public void CreateAccount(
        string accountId,
        string accountName,
        AccountType accountType,
        string createdBy,
        string? parentAccountId = null)
    {
        if (_balances.ContainsKey(accountId))
            throw new InvalidOperationException($"Account {accountId} already exists");

        var @event = new AccountCreated
        {
            Currency = Currency,
            AccountId = accountId,
            AccountName = accountName,
            AccountType = accountType,
            CreatedBy = createdBy,
            ParentAccountId = parentAccountId
        };

        Apply(@event);
        _uncommittedEvents.Add(@event);
    }

    public Money GetBalance(string accountId)
    {
        return _balances.TryGetValue(accountId, out var balance)
            ? balance
            : Money.Zero(Currency);
    }

    public void ClearUncommittedEvents() => _uncommittedEvents.Clear();

    public void LoadFromHistory(IEnumerable<LedgerEvent> history)
    {
        foreach (var @event in history)
        {
            Apply(@event);
            Version++;
        }
    }

    private void EmitBalanceUpdate(string accountId, Guid triggeringEntryId)
    {
        var previousBalance = _balances.TryGetValue(accountId, out var prev)
            ? prev
            : Money.Zero(Currency);

        var @event = new BalanceUpdated
        {
            Currency = Currency,
            AccountId = accountId,
            PreviousBalance = previousBalance,
            NewBalance = GetBalance(accountId),
            TriggeringEntryId = triggeringEntryId
        };

        _uncommittedEvents.Add(@event);
    }

    private void Apply(LedgerEvent @event)
    {
        switch (@event)
        {
            case LedgerEntryPosted posted:
                var entry = new LedgerEntry
                {
                    EntryId = posted.EntryId,
                    TransactionId = posted.TransactionId,
                    DebitAccount = posted.DebitAccount,
                    CreditAccount = posted.CreditAccount,
                    Amount = posted.Amount,
                    Description = posted.Description,
                    PostedAt = posted.Timestamp
                };
                _entries.Add(entry);

                // Update debit account balance (increase)
                UpdateBalance(posted.DebitAccount, posted.Amount);
                // Update credit account balance (decrease for asset/expense, increase for liability/equity/revenue)
                UpdateBalance(posted.CreditAccount, posted.Amount.Negate());
                break;

            case AccountCreated created:
                _balances[created.AccountId] = Money.Zero(Currency);
                break;

            case AccountClosed closed:
                // Keep balance for historical queries, but mark as closed
                break;
        }
    }

    private void UpdateBalance(string accountId, Money amount)
    {
        if (!_balances.ContainsKey(accountId))
            _balances[accountId] = Money.Zero(Currency);

        _balances[accountId] = _balances[accountId].Add(amount);
    }
}

public class LedgerEntry
{
    public Guid EntryId { get; init; }
    public Guid TransactionId { get; init; }
    public string DebitAccount { get; init; } = string.Empty;
    public string CreditAccount { get; init; } = string.Empty;
    public Money Amount { get; init; }
    public string? Description { get; init; }
    public DateTime PostedAt { get; init; }
}
