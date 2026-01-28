using EventStore.Client;
using FinancePlatform.Functions.Services;
using FinancePlatform.Functions.Projections;
using FluentValidation;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Polly;
using Polly.Extensions.Http;

var host = new HostBuilder()
    .ConfigureFunctionsWebApplication()
    .ConfigureServices((context, services) =>
    {
        services.AddApplicationInsightsTelemetryWorkerService();
        services.ConfigureFunctionsApplicationInsights();

        // EventStoreDB Client
        var eventStoreConnectionString = context.Configuration["EventStoreDB:ConnectionString"]
            ?? "esdb://localhost:2113?tls=false";

        var settings = EventStoreClientSettings.Create(eventStoreConnectionString);
        services.AddSingleton(new EventStoreClient(settings));

        // Register Services
        services.AddSingleton<EventStoreDbClient>();
        services.AddSingleton<TaxCalculationService>();
        services.AddSingleton<ReconciliationService>();
        services.AddSingleton<ExchangeRateService>();
        services.AddSingleton<AuditService>();

        // Register Projections
        services.AddSingleton<LedgerBalanceProjection>();
        services.AddSingleton<TaxLiabilityProjection>();
        services.AddSingleton<ReconciliationStatusProjection>();

        // FluentValidation
        services.AddValidatorsFromAssemblyContaining<Program>();

        // HttpClient with Polly resilience
        services.AddHttpClient("ExchangeRate", client =>
        {
            client.BaseAddress = new Uri("https://api.exchangerate.host/");
            client.Timeout = TimeSpan.FromSeconds(10);
        })
        .AddTransientHttpErrorPolicy(policy => policy
            .WaitAndRetryAsync(3, retryAttempt =>
                TimeSpan.FromSeconds(Math.Pow(2, retryAttempt))))
        .AddTransientHttpErrorPolicy(policy => policy
            .CircuitBreakerAsync(5, TimeSpan.FromSeconds(30)));

        // JSON options
        services.Configure<JsonSerializerOptions>(options =>
        {
            options.PropertyNamingPolicy = JsonNamingPolicy.CamelCase;
            options.WriteIndented = false;
            options.DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull;
            options.Converters.Add(new JsonStringEnumConverter());
        });

        // CORS
        services.AddCors(options =>
        {
            options.AddDefaultPolicy(policy =>
            {
                policy.WithOrigins("http://localhost:5173")
                      .AllowAnyHeader()
                      .AllowAnyMethod()
                      .AllowCredentials();
            });
        });
    })
    .Build();

host.Run();
