namespace FinancePlatform.Functions.Domain.ValueObjects;

/// <summary>
/// Represents a monetary amount with currency.
/// Uses decimal for precise financial calculations.
/// </summary>
public readonly record struct Money
{
    public decimal Amount { get; init; }
    public Currency Currency { get; init; }

    public Money(decimal amount, Currency currency)
    {
        Amount = amount;
        Currency = currency;
    }

    public Money(decimal amount, string currencyCode)
        : this(amount, Currency.FromCode(currencyCode))
    {
    }

    public static Money Zero(Currency currency) => new(0, currency);
    public static Money Zero(string currencyCode) => new(0, currencyCode);

    public Money Add(Money other)
    {
        EnsureSameCurrency(other);
        return new Money(Amount + other.Amount, Currency);
    }

    public Money Subtract(Money other)
    {
        EnsureSameCurrency(other);
        return new Money(Amount - other.Amount, Currency);
    }

    public Money Multiply(decimal factor)
        => new(Amount * factor, Currency);

    public Money Negate()
        => new(-Amount, Currency);

    public Money Round(int decimals = 2)
        => new(Math.Round(Amount, decimals, MidpointRounding.AwayFromZero), Currency);

    public bool IsZero => Amount == 0;
    public bool IsPositive => Amount > 0;
    public bool IsNegative => Amount < 0;

    private void EnsureSameCurrency(Money other)
    {
        if (Currency != other.Currency)
        {
            throw new InvalidOperationException(
                $"Cannot perform operation on different currencies: {Currency} and {other.Currency}");
        }
    }

    public override string ToString()
        => $"{Amount:N2} {Currency.Code}";

    public static Money operator +(Money a, Money b) => a.Add(b);
    public static Money operator -(Money a, Money b) => a.Subtract(b);
    public static Money operator *(Money a, decimal b) => a.Multiply(b);
    public static Money operator -(Money a) => a.Negate();
}
