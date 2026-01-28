using System.Text.Json;
using FinancePlatform.Functions.Domain.ValueObjects;
using Polly;
using Polly.CircuitBreaker;
using Polly.Retry;

namespace FinancePlatform.Functions.Services;

public class ExchangeRateService
{
    private readonly IHttpClientFactory _httpClientFactory;
    private readonly ILogger<ExchangeRateService> _logger;
    private readonly Dictionary<string, CachedRate> _rateCache = new();
    private readonly TimeSpan _cacheDuration = TimeSpan.FromMinutes(15);

    // Fallback rates when API is unavailable
    private static readonly Dictionary<string, decimal> FallbackRates = new()
    {
        ["EUR_USD"] = 1.08m,
        ["EUR_GBP"] = 0.86m,
        ["EUR_RSD"] = 117.0m,
        ["EUR_CHF"] = 0.96m,
        ["EUR_JPY"] = 162.0m,
        ["EUR_CNY"] = 7.85m,
        ["USD_EUR"] = 0.93m,
        ["USD_GBP"] = 0.80m,
        ["USD_RSD"] = 108.0m,
        ["GBP_EUR"] = 1.16m,
        ["GBP_USD"] = 1.25m,
        ["RSD_EUR"] = 0.0085m
    };

    public ExchangeRateService(IHttpClientFactory httpClientFactory, ILogger<ExchangeRateService> logger)
    {
        _httpClientFactory = httpClientFactory;
        _logger = logger;
    }

    public async Task<decimal> GetExchangeRateAsync(
        Currency from,
        Currency to,
        CancellationToken ct = default)
    {
        if (from == to)
            return 1m;

        var cacheKey = $"{from.Code}_{to.Code}";

        // Check cache
        if (_rateCache.TryGetValue(cacheKey, out var cached) &&
            DateTime.UtcNow - cached.Timestamp < _cacheDuration)
        {
            return cached.Rate;
        }

        // Try to fetch from API
        try
        {
            var rate = await FetchRateFromApiAsync(from.Code, to.Code, ct);
            _rateCache[cacheKey] = new CachedRate { Rate = rate, Timestamp = DateTime.UtcNow };
            return rate;
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Failed to fetch exchange rate, using fallback");
            return GetFallbackRate(from.Code, to.Code);
        }
    }

    public async Task<Money> ConvertAsync(
        Money amount,
        Currency targetCurrency,
        CancellationToken ct = default)
    {
        if (amount.Currency == targetCurrency)
            return amount;

        var rate = await GetExchangeRateAsync(amount.Currency, targetCurrency, ct);
        return new Money(amount.Amount * rate, targetCurrency).Round();
    }

    public async Task<Dictionary<string, decimal>> GetRatesForBaseCurrencyAsync(
        Currency baseCurrency,
        IEnumerable<Currency> targetCurrencies,
        CancellationToken ct = default)
    {
        var rates = new Dictionary<string, decimal>();

        foreach (var target in targetCurrencies)
        {
            var rate = await GetExchangeRateAsync(baseCurrency, target, ct);
            rates[target.Code] = rate;
        }

        return rates;
    }

    public ExchangeRateInfo GetRateInfo(Currency from, Currency to)
    {
        var cacheKey = $"{from.Code}_{to.Code}";
        var isCached = _rateCache.TryGetValue(cacheKey, out var cached);

        return new ExchangeRateInfo
        {
            FromCurrency = from.Code,
            ToCurrency = to.Code,
            Rate = isCached ? cached!.Rate : GetFallbackRate(from.Code, to.Code),
            Source = isCached ? "API" : "Fallback",
            Timestamp = isCached ? cached!.Timestamp : DateTime.UtcNow,
            IsCached = isCached
        };
    }

    private async Task<decimal> FetchRateFromApiAsync(
        string from,
        string to,
        CancellationToken ct)
    {
        var client = _httpClientFactory.CreateClient("ExchangeRate");

        // Using exchangerate.host (free API)
        var response = await client.GetAsync(
            $"latest?base={from}&symbols={to}",
            ct);

        response.EnsureSuccessStatusCode();

        var json = await response.Content.ReadAsStringAsync(ct);
        using var doc = JsonDocument.Parse(json);

        if (doc.RootElement.TryGetProperty("rates", out var rates) &&
            rates.TryGetProperty(to, out var rateElement))
        {
            return rateElement.GetDecimal();
        }

        throw new InvalidOperationException($"Rate not found for {from}/{to}");
    }

    private decimal GetFallbackRate(string from, string to)
    {
        var directKey = $"{from}_{to}";
        if (FallbackRates.TryGetValue(directKey, out var directRate))
            return directRate;

        var reverseKey = $"{to}_{from}";
        if (FallbackRates.TryGetValue(reverseKey, out var reverseRate))
            return 1m / reverseRate;

        // Try through EUR as intermediary
        var fromEurKey = $"{from}_EUR";
        var eurToKey = $"EUR_{to}";

        if (FallbackRates.TryGetValue(fromEurKey, out var fromEur) &&
            FallbackRates.TryGetValue(eurToKey, out var eurTo))
        {
            return fromEur * eurTo;
        }

        _logger.LogWarning("No fallback rate for {From}/{To}, using 1:1", from, to);
        return 1m;
    }

    private class CachedRate
    {
        public decimal Rate { get; init; }
        public DateTime Timestamp { get; init; }
    }
}

public class ExchangeRateInfo
{
    public string FromCurrency { get; init; } = string.Empty;
    public string ToCurrency { get; init; } = string.Empty;
    public decimal Rate { get; init; }
    public string Source { get; init; } = string.Empty;
    public DateTime Timestamp { get; init; }
    public bool IsCached { get; init; }
}
