using System.Net;
using FinancePlatform.Functions.Projections;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace FinancePlatform.Functions.Functions.Queries;

public class GetTaxReportHandler
{
    private readonly TaxLiabilityProjection _projection;
    private readonly ILogger<GetTaxReportHandler> _logger;

    public GetTaxReportHandler(
        TaxLiabilityProjection projection,
        ILogger<GetTaxReportHandler> logger)
    {
        _projection = projection;
        _logger = logger;
    }

    [Function("GetTaxLiability")]
    public async Task<IActionResult> Run(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "tax/liability/{jurisdiction}/{year}")] HttpRequest req,
        string jurisdiction,
        int year,
        CancellationToken ct)
    {
        try
        {
            if (string.IsNullOrEmpty(jurisdiction))
            {
                return new BadRequestObjectResult(new { error = "Jurisdiction required" });
            }

            if (year < 2000 || year > 2100)
            {
                return new BadRequestObjectResult(new { error = "Invalid year" });
            }

            var result = await _projection.GetTaxLiabilityAsync(jurisdiction, year, ct);

            return new OkObjectResult(new
            {
                jurisdiction = result.Jurisdiction,
                year = result.Year,
                quarters = result.Quarters.Select(q => new
                {
                    quarter = q.Quarter,
                    periodId = q.PeriodId,
                    totalTaxable = new { value = q.TotalTaxable.Amount, currency = q.TotalTaxable.Currency.Code },
                    totalTax = new { value = q.TotalTax.Amount, currency = q.TotalTax.Currency.Code },
                    paymentsMade = q.PaymentsMade != null
                        ? new { value = q.PaymentsMade.Value.Amount, currency = q.PaymentsMade.Value.Currency.Code }
                        : null,
                    outstandingTax = new { value = q.OutstandingTax.Amount, currency = q.OutstandingTax.Currency.Code },
                    transactionCount = q.TransactionCount,
                    reportGenerated = q.ReportGenerated,
                    reportGeneratedAt = q.ReportGeneratedAt,
                    alerts = q.Alerts.Select(a => new
                    {
                        type = a.Type,
                        message = a.Message,
                        timestamp = a.Timestamp
                    })
                }),
                yearlyTotals = new
                {
                    totalTaxable = new { value = result.YearlyTotalTaxable.Amount, currency = result.YearlyTotalTaxable.Currency.Code },
                    totalTax = new { value = result.YearlyTotalTax.Amount, currency = result.YearlyTotalTax.Currency.Code },
                    totalPaid = new { value = result.YearlyTotalPaid.Amount, currency = result.YearlyTotalPaid.Currency.Code },
                    outstanding = new { value = result.YearlyOutstanding.Amount, currency = result.YearlyOutstanding.Currency.Code }
                },
                totalTransactions = result.TotalTransactions,
                allReportsGenerated = result.AllReportsGenerated,
                alerts = result.Alerts.Select(a => new
                {
                    type = a.Type,
                    message = a.Message,
                    timestamp = a.Timestamp
                })
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting tax liability for {Jurisdiction}/{Year}", jurisdiction, year);
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }

    [Function("GetTaxPeriodDetail")]
    public async Task<IActionResult> GetPeriodDetail(
        [HttpTrigger(AuthorizationLevel.Anonymous, "get", Route = "tax/period/{periodId}")] HttpRequest req,
        string periodId,
        CancellationToken ct)
    {
        try
        {
            if (string.IsNullOrEmpty(periodId))
            {
                return new BadRequestObjectResult(new { error = "Period ID required" });
            }

            var result = await _projection.GetPeriodDetailAsync(periodId, ct);

            return new OkObjectResult(new
            {
                periodId = result.PeriodId,
                totalTaxable = new { value = result.TotalTaxable.Amount, currency = result.TotalTaxable.Currency.Code },
                totalTax = new { value = result.TotalTax.Amount, currency = result.TotalTax.Currency.Code },
                totalPaid = new { value = result.TotalPaid.Amount, currency = result.TotalPaid.Currency.Code },
                outstanding = new { value = result.Outstanding.Amount, currency = result.Outstanding.Currency.Code },
                calculations = result.Calculations.Select(c => new
                {
                    transactionId = c.TransactionId,
                    taxableAmount = new { value = c.TaxableAmount.Amount, currency = c.TaxableAmount.Currency.Code },
                    taxAmount = new { value = c.TaxAmount.Amount, currency = c.TaxAmount.Currency.Code },
                    rate = c.Rate,
                    jurisdiction = c.Jurisdiction,
                    calculatedAt = c.CalculatedAt
                }),
                payments = result.Payments.Select(p => new
                {
                    paymentId = p.PaymentId,
                    amount = new { value = p.Amount.Amount, currency = p.Amount.Currency.Code },
                    paymentDate = p.PaymentDate,
                    reference = p.Reference
                }),
                adjustments = result.Adjustments.Select(a => new
                {
                    adjustmentId = a.AdjustmentId,
                    amount = new { value = a.Amount.Amount, currency = a.Amount.Currency.Code },
                    reason = a.Reason,
                    appliedBy = a.AppliedBy,
                    appliedAt = a.AppliedAt
                }),
                alerts = result.Alerts.Select(a => new
                {
                    type = a.Type,
                    message = a.Message,
                    timestamp = a.Timestamp
                }),
                reportGenerated = result.ReportGenerated,
                reportGeneratedAt = result.ReportGeneratedAt
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting tax period detail for {PeriodId}", periodId);
            return new ObjectResult(new { error = "Internal server error" })
            {
                StatusCode = (int)HttpStatusCode.InternalServerError
            };
        }
    }
}
