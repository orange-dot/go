using FinancePlatform.Functions.Domain.Events;
using FinancePlatform.Functions.Domain.ValueObjects;
using FinancePlatform.Functions.Services;

namespace FinancePlatform.Functions.Projections;

public class LedgerBalanceProjection
{
    private readonly EventStoreDbClient _eventStore;
    private readonly ExchangeRateService _exchangeRateService;
    private readonly ILogger<LedgerBalanceProjection> _logger;

    public LedgerBalanceProjection(
        EventStoreDbClient eventStore,
        ExchangeRateService exchangeRateService,
        ILogger<LedgerBalanceProjection> logger)
    {
        _eventStore = eventStore;
        _exchangeRateService = exchangeRateService;
        _logger = logger;
    }

    public async Task<LedgerBalanceView> GetBalancesAsync(
        string? baseCurrency = null,
        CancellationToken ct = default)
    {
        var balancesByCurrency = new Dictionary<string, Dictionary<string, decimal>>();
        var allEvents = new List<LedgerEntryPosted>();

        // Read from all ledger streams
        foreach (var currency in new[] { "EUR", "USD", "GBP", "RSD" })
        {
            var streamName = $"ledger-{currency}";
            var events = await _eventStore.ReadStreamAsync<LedgerEvent>(streamName, ct);

            var currencyBalances = new Dictionary<string, decimal>();

            foreach (var @event in events)
            {
                if (@event is LedgerEntryPosted posted)
                {
                    allEvents.Add(posted);

                    // Update debit account (increase)
                    if (!currencyBalances.ContainsKey(posted.DebitAccount))
                        currencyBalances[posted.DebitAccount] = 0;
                    currencyBalances[posted.DebitAccount] += posted.Amount.Amount;

                    // Update credit account (decrease)
                    if (!currencyBalances.ContainsKey(posted.CreditAccount))
                        currencyBalances[posted.CreditAccount] = 0;
                    currencyBalances[posted.CreditAccount] -= posted.Amount.Amount;
                }
            }

            if (currencyBalances.Any())
            {
                balancesByCurrency[currency] = currencyBalances;
            }
        }

        // Convert to base currency if requested
        Dictionary<string, decimal>? consolidatedBalances = null;
        if (!string.IsNullOrEmpty(baseCurrency))
        {
            consolidatedBalances = await ConsolidateBalancesAsync(
                balancesByCurrency,
                baseCurrency,
                ct);
        }

        return new LedgerBalanceView
        {
            BalancesByCurrency = balancesByCurrency,
            ConsolidatedBalances = consolidatedBalances,
            BaseCurrency = baseCurrency,
            LastUpdated = allEvents.Any()
                ? allEvents.Max(e => e.Timestamp)
                : DateTime.UtcNow,
            EntryCount = allEvents.Count
        };
    }

    public async Task<AccountBalanceView> GetAccountBalanceAsync(
        string accountId,
        CancellationToken ct = default)
    {
        var balances = new Dictionary<string, Money>();
        var entries = new List<AccountEntry>();

        foreach (var currency in new[] { "EUR", "USD", "GBP", "RSD" })
        {
            var streamName = $"ledger-{currency}";
            var events = await _eventStore.ReadStreamAsync<LedgerEvent>(streamName, ct);

            decimal balance = 0;

            foreach (var @event in events)
            {
                if (@event is LedgerEntryPosted posted)
                {
                    if (posted.DebitAccount == accountId)
                    {
                        balance += posted.Amount.Amount;
                        entries.Add(new AccountEntry
                        {
                            EntryId = posted.EntryId,
                            TransactionId = posted.TransactionId,
                            Type = "Debit",
                            Amount = posted.Amount,
                            Description = posted.Description,
                            Timestamp = posted.Timestamp
                        });
                    }
                    else if (posted.CreditAccount == accountId)
                    {
                        balance -= posted.Amount.Amount;
                        entries.Add(new AccountEntry
                        {
                            EntryId = posted.EntryId,
                            TransactionId = posted.TransactionId,
                            Type = "Credit",
                            Amount = posted.Amount,
                            Description = posted.Description,
                            Timestamp = posted.Timestamp
                        });
                    }
                }
            }

            if (balance != 0 || entries.Any(e => e.Amount.Currency.Code == currency))
            {
                balances[currency] = new Money(balance, currency);
            }
        }

        return new AccountBalanceView
        {
            AccountId = accountId,
            Balances = balances,
            RecentEntries = entries.OrderByDescending(e => e.Timestamp).Take(50).ToList(),
            TotalEntries = entries.Count
        };
    }

    public async Task<TrialBalanceView> GetTrialBalanceAsync(
        string currency,
        CancellationToken ct = default)
    {
        var streamName = $"ledger-{currency}";
        var events = await _eventStore.ReadStreamAsync<LedgerEvent>(streamName, ct);

        var accounts = new Dictionary<string, TrialBalanceEntry>();

        foreach (var @event in events)
        {
            if (@event is LedgerEntryPosted posted)
            {
                // Debit side
                if (!accounts.ContainsKey(posted.DebitAccount))
                {
                    accounts[posted.DebitAccount] = new TrialBalanceEntry
                    {
                        AccountId = posted.DebitAccount
                    };
                }
                accounts[posted.DebitAccount].TotalDebits += posted.Amount.Amount;

                // Credit side
                if (!accounts.ContainsKey(posted.CreditAccount))
                {
                    accounts[posted.CreditAccount] = new TrialBalanceEntry
                    {
                        AccountId = posted.CreditAccount
                    };
                }
                accounts[posted.CreditAccount].TotalCredits += posted.Amount.Amount;
            }
        }

        var entries = accounts.Values.OrderBy(e => e.AccountId).ToList();
        var totalDebits = entries.Sum(e => e.TotalDebits);
        var totalCredits = entries.Sum(e => e.TotalCredits);

        return new TrialBalanceView
        {
            Currency = currency,
            Entries = entries,
            TotalDebits = new Money(totalDebits, currency),
            TotalCredits = new Money(totalCredits, currency),
            IsBalanced = totalDebits == totalCredits,
            GeneratedAt = DateTime.UtcNow
        };
    }

    private async Task<Dictionary<string, decimal>> ConsolidateBalancesAsync(
        Dictionary<string, Dictionary<string, decimal>> balancesByCurrency,
        string baseCurrency,
        CancellationToken ct)
    {
        var consolidated = new Dictionary<string, decimal>();

        foreach (var (currency, accounts) in balancesByCurrency)
        {
            var rate = currency == baseCurrency
                ? 1m
                : await _exchangeRateService.GetExchangeRateAsync(
                    Currency.FromCode(currency),
                    Currency.FromCode(baseCurrency),
                    ct);

            foreach (var (account, balance) in accounts)
            {
                if (!consolidated.ContainsKey(account))
                    consolidated[account] = 0;

                consolidated[account] += balance * rate;
            }
        }

        return consolidated;
    }
}

public class LedgerBalanceView
{
    public Dictionary<string, Dictionary<string, decimal>> BalancesByCurrency { get; init; } = new();
    public Dictionary<string, decimal>? ConsolidatedBalances { get; init; }
    public string? BaseCurrency { get; init; }
    public DateTime LastUpdated { get; init; }
    public int EntryCount { get; init; }
}

public class AccountBalanceView
{
    public string AccountId { get; init; } = string.Empty;
    public Dictionary<string, Money> Balances { get; init; } = new();
    public List<AccountEntry> RecentEntries { get; init; } = new();
    public int TotalEntries { get; init; }
}

public class AccountEntry
{
    public Guid EntryId { get; init; }
    public Guid TransactionId { get; init; }
    public string Type { get; init; } = string.Empty;
    public Money Amount { get; init; }
    public string? Description { get; init; }
    public DateTime Timestamp { get; init; }
}

public class TrialBalanceView
{
    public string Currency { get; init; } = string.Empty;
    public List<TrialBalanceEntry> Entries { get; init; } = new();
    public Money TotalDebits { get; init; }
    public Money TotalCredits { get; init; }
    public bool IsBalanced { get; init; }
    public DateTime GeneratedAt { get; init; }
}

public class TrialBalanceEntry
{
    public string AccountId { get; init; } = string.Empty;
    public decimal TotalDebits { get; set; }
    public decimal TotalCredits { get; set; }
    public decimal Balance => TotalDebits - TotalCredits;
}
