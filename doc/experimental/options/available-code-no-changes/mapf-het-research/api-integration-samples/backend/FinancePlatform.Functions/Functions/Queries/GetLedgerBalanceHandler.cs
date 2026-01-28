using System.Net;
using FinancePlatform.Functions.Projections;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace FinancePlatform.Functions.Functions.Queries;

public class GetLedgerBalanceHandler
{
    private readonly LedgerBalanceProjection _projection;
    private readonly ILogger<GetLedgerBalanceHandler> _logger;

    public GetLedgerBalanceHandler(
        LedgerBalanceProjection projection,
        ILogger<GetLedgerBalanceHandler> logger)
    {
        _projection = projection;
        _logger = logger;
    }

    [Function("GetLedgerBalance")]
    public async Task<IActionResult> Run(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "ledger/balance")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var baseCurrency = req.Query["baseCurrency"].FirstOrDefault();

            var result = await _projection.GetBalancesAsync(baseCurrency, ct);

            return new OkObjectResult(new
            {
                balancesByCurrency = result.BalancesByCurrency,
                consolidatedBalances = result.ConsolidatedBalances,
                baseCurrency = result.BaseCurrency,
                lastUpdated = result.LastUpdated,
                entryCount = result.EntryCount
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting ledger balance");
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("GetAccountBalance")]
    public async Task<IActionResult> GetAccount(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "ledger/accounts/{accountId}")] HttpRequest req,
        string accountId,
        CancellationToken ct)
    {
        try
        {
            if (string.IsNullOrEmpty(accountId))
            {
                return new BadRequestObjectResult(new { error = "Account ID required" });
            }

            var result = await _projection.GetAccountBalanceAsync(accountId, ct);

            return new OkObjectResult(new
            {
                accountId = result.AccountId,
                balances = result.Balances.ToDictionary(
                    kvp => kvp.Key,
                    kvp => new { value = kvp.Value.Amount, currency = kvp.Value.Currency.Code }
                ),
                recentEntries = result.RecentEntries.Select(e => new
                {
                    entryId = e.EntryId,
                    transactionId = e.TransactionId,
                    type = e.Type,
                    amount = new { value = e.Amount.Amount, currency = e.Amount.Currency.Code },
                    description = e.Description,
                    timestamp = e.Timestamp
                }),
                totalEntries = result.TotalEntries
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting account balance for {AccountId}", accountId);
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("GetTrialBalance")]
    public async Task<IActionResult> GetTrialBalance(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "ledger/trial-balance/{currency}")] HttpRequest req,
        string currency,
        CancellationToken ct)
    {
        try
        {
            if (string.IsNullOrEmpty(currency))
            {
                return new BadRequestObjectResult(new { error = "Currency required" });
            }

            var result = await _projection.GetTrialBalanceAsync(currency.ToUpperInvariant(), ct);

            return new OkObjectResult(new
            {
                currency = result.Currency,
                entries = result.Entries.Select(e => new
                {
                    accountId = e.AccountId,
                    totalDebits = e.TotalDebits,
                    totalCredits = e.TotalCredits,
                    balance = e.Balance
                }),
                totalDebits = new { value = result.TotalDebits.Amount, currency = result.TotalDebits.Currency.Code },
                totalCredits = new { value = result.TotalCredits.Amount, currency = result.TotalCredits.Currency.Code },
                isBalanced = result.IsBalanced,
                generatedAt = result.GeneratedAt
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting trial balance for {Currency}", currency);
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }
}
