namespace FinancePlatform.Functions.Domain.ValueObjects;

/// <summary>
/// Represents a tax rate for a specific jurisdiction and category.
/// </summary>
public readonly record struct TaxRate
{
    public decimal Rate { get; }
    public string Jurisdiction { get; }
    public TaxType TaxType { get; }
    public string? Category { get; }
    public DateOnly EffectiveFrom { get; }
    public DateOnly? EffectiveTo { get; }

    public TaxRate(
        decimal rate,
        string jurisdiction,
        TaxType taxType,
        string? category = null,
        DateOnly? effectiveFrom = null,
        DateOnly? effectiveTo = null)
    {
        if (rate < 0 || rate > 1)
            throw new ArgumentOutOfRangeException(nameof(rate), "Tax rate must be between 0 and 1");

        Rate = rate;
        Jurisdiction = jurisdiction ?? throw new ArgumentNullException(nameof(jurisdiction));
        TaxType = taxType;
        Category = category;
        EffectiveFrom = effectiveFrom ?? DateOnly.MinValue;
        EffectiveTo = effectiveTo;
    }

    public bool IsActiveOn(DateOnly date)
        => date >= EffectiveFrom && (EffectiveTo is null || date <= EffectiveTo);

    public Money CalculateTax(Money amount)
        => amount.Multiply(Rate).Round();

    public Money CalculateNet(Money grossAmount)
        => grossAmount.Multiply(1 / (1 + Rate)).Round();

    public Money CalculateGross(Money netAmount)
        => netAmount.Multiply(1 + Rate).Round();

    public decimal AsPercentage => Rate * 100;

    public override string ToString()
        => $"{Jurisdiction} {TaxType}: {AsPercentage:F1}%";

    // Common tax rates
    public static TaxRate SerbiaVAT => new(0.20m, "RS", TaxType.VAT);
    public static TaxRate SerbiaReducedVAT => new(0.10m, "RS", TaxType.VAT, "Reduced");
    public static TaxRate GermanyVAT => new(0.19m, "DE", TaxType.VAT);
    public static TaxRate GermanyReducedVAT => new(0.07m, "DE", TaxType.VAT, "Reduced");
    public static TaxRate UKVAT => new(0.20m, "GB", TaxType.VAT);
    public static TaxRate UKReducedVAT => new(0.05m, "GB", TaxType.VAT, "Reduced");
    public static TaxRate ZeroRate => new(0m, "INTL", TaxType.VAT, "Zero-rated");
}

public enum TaxType
{
    VAT,
    GST,
    SalesTax,
    WithholdingTax,
    CorporateTax
}
