import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { FileText, AlertTriangle } from 'lucide-react'
import { taxApi } from '@/api/tax'
import { MoneyDisplay } from '@/components/common/MoneyDisplay'
import { StatusBadge } from '@/components/common/StatusBadge'

const JURISDICTIONS = [
  { code: 'RS', name: 'Serbia', rate: '20%' },
  { code: 'DE', name: 'Germany', rate: '19%' },
  { code: 'GB', name: 'United Kingdom', rate: '20%' },
  { code: 'US', name: 'United States', rate: '8.25%' },
]

export function TaxReportPage() {
  const [jurisdiction, setJurisdiction] = useState('RS')
  const [year, setYear] = useState(new Date().getFullYear())
  const queryClient = useQueryClient()

  const { data: liability, isLoading } = useQuery({
    queryKey: ['tax', 'liability', jurisdiction, year],
    queryFn: () => taxApi.getLiability(jurisdiction, year),
  })

  const generateMutation = useMutation({
    mutationFn: (quarter: number) => taxApi.generateReport({
      jurisdiction,
      year,
      quarter,
      generatedBy: 'demo-user',
    }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['tax'] })
    },
  })

  const currentQuarter = Math.ceil((new Date().getMonth() + 1) / 3)

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-gray-900">Tax Reports</h1>
          <p className="text-gray-500">VAT calculation and compliance reporting</p>
        </div>

        <div className="flex items-center gap-4">
          <div>
            <label className="block text-xs text-gray-500 mb-1">Jurisdiction</label>
            <select
              value={jurisdiction}
              onChange={(e) => setJurisdiction(e.target.value)}
              className="px-3 py-2 border border-gray-300 rounded-lg focus:ring-2 focus:ring-blue-500"
            >
              {JURISDICTIONS.map((j) => (
                <option key={j.code} value={j.code}>
                  {j.name} ({j.rate})
                </option>
              ))}
            </select>
          </div>
          <div>
            <label className="block text-xs text-gray-500 mb-1">Year</label>
            <select
              value={year}
              onChange={(e) => setYear(parseInt(e.target.value))}
              className="px-3 py-2 border border-gray-300 rounded-lg focus:ring-2 focus:ring-blue-500"
            >
              {[2024, 2025, 2026].map((y) => (
                <option key={y} value={y}>{y}</option>
              ))}
            </select>
          </div>
        </div>
      </div>

      {/* Alerts */}
      {liability?.alerts && liability.alerts.length > 0 && (
        <div className="bg-yellow-50 border border-yellow-200 rounded-lg p-4">
          <div className="flex items-start gap-3">
            <AlertTriangle className="h-5 w-5 text-yellow-600 mt-0.5" />
            <div>
              <h3 className="font-medium text-yellow-800">Tax Alerts</h3>
              <ul className="mt-1 space-y-1">
                {liability.alerts.map((alert, i) => (
                  <li key={i} className="text-sm text-yellow-700">
                    <strong>{alert.type}:</strong> {alert.message}
                  </li>
                ))}
              </ul>
            </div>
          </div>
        </div>
      )}

      {/* Yearly Summary */}
      <div className="bg-white rounded-lg border border-gray-200 p-6">
        <h2 className="text-lg font-semibold text-gray-900 mb-4">
          {year} Tax Summary - {JURISDICTIONS.find(j => j.code === jurisdiction)?.name}
        </h2>

        {isLoading ? (
          <div className="animate-pulse grid grid-cols-4 gap-4">
            {Array.from({ length: 4 }).map((_, i) => (
              <div key={i} className="h-20 bg-gray-100 rounded-lg" />
            ))}
          </div>
        ) : liability?.yearlyTotals ? (
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
            <div className="bg-gray-50 rounded-lg p-4">
              <p className="text-sm text-gray-500">Total Taxable</p>
              <MoneyDisplay amount={liability.yearlyTotals.totalTaxable} size="lg" />
            </div>
            <div className="bg-gray-50 rounded-lg p-4">
              <p className="text-sm text-gray-500">Total Tax</p>
              <MoneyDisplay amount={liability.yearlyTotals.totalTax} size="lg" />
            </div>
            <div className="bg-gray-50 rounded-lg p-4">
              <p className="text-sm text-gray-500">Total Paid</p>
              <MoneyDisplay amount={liability.yearlyTotals.totalPaid} size="lg" />
            </div>
            <div className="bg-red-50 rounded-lg p-4">
              <p className="text-sm text-gray-500">Outstanding</p>
              <MoneyDisplay amount={liability.yearlyTotals.outstanding} size="lg" className="text-red-600" />
            </div>
          </div>
        ) : (
          <p className="text-gray-500">No tax data for this period.</p>
        )}

        <div className="mt-4 text-sm text-gray-500">
          Total transactions: {liability?.totalTransactions ?? 0}
          {' | '}
          Reports generated: {liability?.allReportsGenerated ? 'All' : 'Partial'}
        </div>
      </div>

      {/* Quarterly Breakdown */}
      <div className="bg-white rounded-lg border border-gray-200 p-6">
        <h2 className="text-lg font-semibold text-gray-900 mb-4">
          Quarterly Breakdown
        </h2>

        {isLoading ? (
          <div className="animate-pulse space-y-4">
            {Array.from({ length: 4 }).map((_, i) => (
              <div key={i} className="h-24 bg-gray-100 rounded-lg" />
            ))}
          </div>
        ) : (
          <div className="space-y-4">
            {[1, 2, 3, 4].map((quarter) => {
              const quarterData = liability?.quarters.find(q => q.quarter === quarter)
              const isPast = quarter < currentQuarter
              const isCurrent = quarter === currentQuarter

              return (
                <div
                  key={quarter}
                  className={`border rounded-lg p-4 ${isCurrent ? 'border-blue-300 bg-blue-50' : 'border-gray-200'}`}
                >
                  <div className="flex items-center justify-between mb-3">
                    <div className="flex items-center gap-2">
                      <h3 className="font-medium text-gray-900">Q{quarter} {year}</h3>
                      {isCurrent && (
                        <span className="px-2 py-0.5 text-xs bg-blue-100 text-blue-800 rounded-full">
                          Current
                        </span>
                      )}
                      {quarterData?.reportGenerated && (
                        <StatusBadge status="Completed" size="sm" />
                      )}
                    </div>
                    {!quarterData?.reportGenerated && (isPast || isCurrent) && (
                      <button
                        onClick={() => generateMutation.mutate(quarter)}
                        disabled={generateMutation.isPending}
                        className="flex items-center gap-1 px-3 py-1 text-sm bg-blue-600 text-white rounded-lg hover:bg-blue-700 disabled:opacity-50"
                      >
                        <FileText className="h-4 w-4" />
                        Generate Report
                      </button>
                    )}
                  </div>

                  {quarterData ? (
                    <div className="grid grid-cols-2 md:grid-cols-5 gap-4 text-sm">
                      <div>
                        <p className="text-gray-500">Taxable</p>
                        <MoneyDisplay amount={quarterData.totalTaxable} />
                      </div>
                      <div>
                        <p className="text-gray-500">Tax Due</p>
                        <MoneyDisplay amount={quarterData.totalTax} />
                      </div>
                      <div>
                        <p className="text-gray-500">Paid</p>
                        {quarterData.paymentsMade ? (
                          <MoneyDisplay amount={quarterData.paymentsMade} />
                        ) : (
                          <span className="text-gray-400">-</span>
                        )}
                      </div>
                      <div>
                        <p className="text-gray-500">Outstanding</p>
                        <MoneyDisplay amount={quarterData.outstandingTax} showSign />
                      </div>
                      <div>
                        <p className="text-gray-500">Transactions</p>
                        <span className="font-mono">{quarterData.transactionCount}</span>
                      </div>
                    </div>
                  ) : (
                    <p className="text-gray-400 text-sm">No data for this quarter</p>
                  )}

                  {quarterData?.alerts && quarterData.alerts.length > 0 && (
                    <div className="mt-2 text-sm text-yellow-700">
                      {quarterData.alerts.map((alert, i) => (
                        <span key={i}>{alert.message}</span>
                      ))}
                    </div>
                  )}
                </div>
              )
            })}
          </div>
        )}
      </div>

      {/* Tax Rates */}
      <div className="bg-white rounded-lg border border-gray-200 p-6">
        <h2 className="text-lg font-semibold text-gray-900 mb-4">
          Applicable Tax Rates
        </h2>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
          {JURISDICTIONS.map((j) => (
            <div
              key={j.code}
              className={`p-4 rounded-lg border ${jurisdiction === j.code ? 'border-blue-500 bg-blue-50' : 'border-gray-200'}`}
            >
              <p className="text-sm text-gray-500">{j.name}</p>
              <p className="text-2xl font-bold text-gray-900">{j.rate}</p>
              <p className="text-xs text-gray-400">Standard VAT</p>
            </div>
          ))}
        </div>
      </div>
    </div>
  )
}
