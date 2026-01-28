using System.Net;
using FinancePlatform.Functions.Domain.ValueObjects;
using FinancePlatform.Functions.Services;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace FinancePlatform.Functions.Functions.Commands;

public class CalculateTaxHandler
{
    private readonly TaxCalculationService _taxService;
    private readonly ILogger<CalculateTaxHandler> _logger;

    public CalculateTaxHandler(
        TaxCalculationService taxService,
        ILogger<CalculateTaxHandler> logger)
    {
        _taxService = taxService;
        _logger = logger;
    }

    [Function("CalculateTax")]
    public async Task<IActionResult> Run(
        [HttpTrigger(AuthorizationLevel.Anonymous, "post", Route = "tax/calculate")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var command = await req.ReadFromJsonAsync<CalculateTaxCommand>(ct);

            if (command == null)
            {
                return new BadRequestObjectResult(new { error = "Invalid request body" });
            }

            if (command.TransactionId == Guid.Empty)
            {
                return new BadRequestObjectResult(new { error = "Transaction ID required" });
            }

            if (string.IsNullOrEmpty(command.Jurisdiction))
            {
                return new BadRequestObjectResult(new { error = "Jurisdiction required" });
            }

            var amount = new Money(command.Amount, command.Currency ?? "EUR");

            var result = await _taxService.CalculateTaxAsync(
                command.TransactionId,
                amount,
                command.Jurisdiction,
                command.Category,
                ct);

            return new OkObjectResult(new
            {
                transactionId = result.TransactionId,
                taxableAmount = new
                {
                    value = result.TaxableAmount.Amount,
                    currency = result.TaxableAmount.Currency.Code
                },
                taxAmount = new
                {
                    value = result.TaxAmount.Amount,
                    currency = result.TaxAmount.Currency.Code
                },
                taxRate = result.TaxRate?.Rate,
                taxRatePercentage = result.TaxRate?.AsPercentage,
                jurisdiction = result.TaxRate?.Jurisdiction,
                taxType = result.TaxRate?.TaxType.ToString(),
                periodId = result.PeriodId,
                isExempt = result.IsExempt,
                exemptionReason = result.ExemptionReason
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error calculating tax");
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("GenerateTaxReport")]
    public async Task<IActionResult> GenerateReport(
        [HttpTrigger(AuthorizationLevel.Anonymous, "post", Route = "tax/report")] HttpRequest req,
        CancellationToken ct)
    {
        try
        {
            var command = await req.ReadFromJsonAsync<GenerateTaxReportCommand>(ct);

            if (command == null)
            {
                return new BadRequestObjectResult(new { error = "Invalid request body" });
            }

            if (string.IsNullOrEmpty(command.Jurisdiction))
            {
                return new BadRequestObjectResult(new { error = "Jurisdiction required" });
            }

            var report = await _taxService.GenerateReportAsync(
                command.Jurisdiction,
                command.Year ?? DateTime.UtcNow.Year,
                command.Quarter ?? ((DateTime.UtcNow.Month - 1) / 3 + 1),
                command.GeneratedBy ?? "system",
                ct);

            return new OkObjectResult(new
            {
                reportId = report.ReportId,
                periodId = report.PeriodId,
                jurisdiction = report.Jurisdiction,
                year = report.Year,
                quarter = report.Quarter,
                totalTaxable = new
                {
                    value = report.TotalTaxable.Amount,
                    currency = report.TotalTaxable.Currency.Code
                },
                totalTax = new
                {
                    value = report.TotalTax.Amount,
                    currency = report.TotalTax.Currency.Code
                },
                transactionCount = report.TransactionCount,
                calculations = report.Calculations.Select(c => new
                {
                    transactionId = c.TransactionId,
                    taxableAmount = c.TaxableAmount.Amount,
                    taxAmount = c.TaxAmount.Amount,
                    rate = c.TaxRate.Rate,
                    calculatedAt = c.CalculatedAt
                }),
                thresholdAlerts = report.ThresholdAlerts.Select(a => new
                {
                    type = a.ThresholdType.ToString(),
                    currentValue = a.CurrentValue.Amount,
                    thresholdValue = a.ThresholdValue.Amount,
                    exceededAt = a.Timestamp
                }),
                generatedAt = report.GeneratedAt,
                generatedBy = report.GeneratedBy
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error generating tax report");
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("GetTaxRates")]
    public IActionResult GetRates(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "tax/rates/{jurisdiction}")] HttpRequest req,
        string jurisdiction)
    {
        var rates = _taxService.GetAllRates(jurisdiction);

        if (!rates.Any())
        {
            return new NotFoundObjectResult(new
            {
                error = $"No tax rates found for jurisdiction: {jurisdiction}"
            });
        }

        return new OkObjectResult(rates.Select(r => new
        {
            rate = r.Rate,
            percentage = r.AsPercentage,
            jurisdiction = r.Jurisdiction,
            taxType = r.TaxType.ToString(),
            category = r.Category,
            effectiveFrom = r.EffectiveFrom,
            effectiveTo = r.EffectiveTo
        }));
    }
}

public record CalculateTaxCommand
{
    public Guid TransactionId { get; init; }
    public decimal Amount { get; init; }
    public string? Currency { get; init; }
    public string Jurisdiction { get; init; } = string.Empty;
    public string? Category { get; init; }
}

public record GenerateTaxReportCommand
{
    public string Jurisdiction { get; init; } = string.Empty;
    public int? Year { get; init; }
    public int? Quarter { get; init; }
    public string? GeneratedBy { get; init; }
}
