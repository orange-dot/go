namespace FinancePlatform.Functions.Domain.ValueObjects;

/// <summary>
/// Represents an ISO 4217 currency code.
/// </summary>
public readonly record struct Currency
{
    public string Code { get; }
    public string Name { get; }
    public int DecimalPlaces { get; }

    private Currency(string code, string name, int decimalPlaces)
    {
        Code = code;
        Name = name;
        DecimalPlaces = decimalPlaces;
    }

    // Common currencies
    public static readonly Currency USD = new("USD", "US Dollar", 2);
    public static readonly Currency EUR = new("EUR", "Euro", 2);
    public static readonly Currency GBP = new("GBP", "British Pound", 2);
    public static readonly Currency RSD = new("RSD", "Serbian Dinar", 2);
    public static readonly Currency CHF = new("CHF", "Swiss Franc", 2);
    public static readonly Currency JPY = new("JPY", "Japanese Yen", 0);
    public static readonly Currency CNY = new("CNY", "Chinese Yuan", 2);

    private static readonly Dictionary<string, Currency> KnownCurrencies = new()
    {
        ["USD"] = USD,
        ["EUR"] = EUR,
        ["GBP"] = GBP,
        ["RSD"] = RSD,
        ["CHF"] = CHF,
        ["JPY"] = JPY,
        ["CNY"] = CNY,
    };

    public static Currency FromCode(string code)
    {
        if (string.IsNullOrWhiteSpace(code))
            throw new ArgumentException("Currency code cannot be empty", nameof(code));

        code = code.ToUpperInvariant();

        if (KnownCurrencies.TryGetValue(code, out var currency))
            return currency;

        // Unknown currency - assume 2 decimal places
        return new Currency(code, code, 2);
    }

    public static bool IsValid(string code)
        => !string.IsNullOrWhiteSpace(code) && code.Length == 3;

    public override string ToString() => Code;

    public static implicit operator string(Currency currency) => currency.Code;
}
