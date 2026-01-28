import { useState } from 'react'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import { GitCompare, CheckCircle, XCircle, AlertCircle } from 'lucide-react'
import { reconciliationApi, type TransactionInput } from '@/api/reconciliation'
import { StatusBadge } from '@/components/common/StatusBadge'
import { MoneyDisplay } from '@/components/common/MoneyDisplay'

// Sample data for demo
const SAMPLE_SOURCE_A: TransactionInput[] = [
  { id: 'a1', amount: 1000, currency: 'EUR', date: '2024-01-15', reference: 'INV-001', description: 'Invoice payment' },
  { id: 'a2', amount: 500.50, currency: 'EUR', date: '2024-01-16', reference: 'INV-002', description: 'Service fee' },
  { id: 'a3', amount: 2500, currency: 'EUR', date: '2024-01-17', reference: 'INV-003', description: 'Product sale' },
  { id: 'a4', amount: 150, currency: 'EUR', date: '2024-01-18', reference: 'REF-001', description: 'Refund' },
]

const SAMPLE_SOURCE_B: TransactionInput[] = [
  { id: 'b1', amount: 1000, currency: 'EUR', date: '2024-01-15', reference: 'INV-001', description: 'Invoice payment' },
  { id: 'b2', amount: 500, currency: 'EUR', date: '2024-01-16', reference: 'INV-002', description: 'Service' }, // Slight difference
  { id: 'b3', amount: 2500, currency: 'EUR', date: '2024-01-18', reference: 'INV-003', description: 'Product' }, // Day difference
  { id: 'b4', amount: 300, currency: 'EUR', date: '2024-01-19', reference: 'NEW-001', description: 'New transaction' }, // No match
]

export function ReconciliationPage() {
  const [result, setResult] = useState<Awaited<ReturnType<typeof reconciliationApi.start>> | null>(null)
  const queryClient = useQueryClient()

  const reconcileMutation = useMutation({
    mutationFn: () => reconciliationApi.start({
      sourceA: SAMPLE_SOURCE_A,
      sourceB: SAMPLE_SOURCE_B,
      sourceAName: 'Accounting System',
      sourceBName: 'Bank Statement',
      startedBy: 'demo-user',
    }),
    onSuccess: (data) => {
      setResult(data)
      queryClient.invalidateQueries({ queryKey: ['events'] })
    },
  })

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-gray-900">Reconciliation</h1>
          <p className="text-gray-500">Match transactions from different sources</p>
        </div>

        <button
          onClick={() => reconcileMutation.mutate()}
          disabled={reconcileMutation.isPending}
          className="flex items-center gap-2 px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 disabled:opacity-50"
        >
          <GitCompare className="h-5 w-5" />
          {reconcileMutation.isPending ? 'Running...' : 'Run Reconciliation'}
        </button>
      </div>

      {/* Source Data Preview */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        <div className="bg-white rounded-lg border border-gray-200 p-4">
          <h3 className="font-medium text-gray-900 mb-3">Accounting System</h3>
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b">
                <th className="text-left py-2">Reference</th>
                <th className="text-right py-2">Amount</th>
                <th className="text-left py-2">Date</th>
              </tr>
            </thead>
            <tbody>
              {SAMPLE_SOURCE_A.map((tx) => (
                <tr key={tx.id} className="border-b border-gray-100">
                  <td className="py-2">{tx.reference}</td>
                  <td className="py-2 text-right font-mono">{tx.amount?.toFixed(2)}</td>
                  <td className="py-2 text-gray-500">{tx.date}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        <div className="bg-white rounded-lg border border-gray-200 p-4">
          <h3 className="font-medium text-gray-900 mb-3">Bank Statement</h3>
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b">
                <th className="text-left py-2">Reference</th>
                <th className="text-right py-2">Amount</th>
                <th className="text-left py-2">Date</th>
              </tr>
            </thead>
            <tbody>
              {SAMPLE_SOURCE_B.map((tx) => (
                <tr key={tx.id} className="border-b border-gray-100">
                  <td className="py-2">{tx.reference}</td>
                  <td className="py-2 text-right font-mono">{tx.amount?.toFixed(2)}</td>
                  <td className="py-2 text-gray-500">{tx.date}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>

      {/* Results */}
      {result && (
        <>
          {/* Summary */}
          <div className="bg-white rounded-lg border border-gray-200 p-6">
            <h2 className="text-lg font-semibold text-gray-900 mb-4">
              Reconciliation Results
            </h2>

            <div className="grid grid-cols-2 md:grid-cols-5 gap-4 mb-6">
              <div className="text-center p-4 bg-gray-50 rounded-lg">
                <p className="text-2xl font-bold text-gray-900">{result.totalTransactions}</p>
                <p className="text-sm text-gray-500">Total</p>
              </div>
              <div className="text-center p-4 bg-green-50 rounded-lg">
                <p className="text-2xl font-bold text-green-600">{result.matched}</p>
                <p className="text-sm text-gray-500">Matched</p>
              </div>
              <div className="text-center p-4 bg-red-50 rounded-lg">
                <p className="text-2xl font-bold text-red-600">{result.unmatched}</p>
                <p className="text-sm text-gray-500">Unmatched</p>
              </div>
              <div className="text-center p-4 bg-blue-50 rounded-lg">
                <p className="text-2xl font-bold text-blue-600">{(result.matchRate * 100).toFixed(0)}%</p>
                <p className="text-sm text-gray-500">Match Rate</p>
              </div>
              <div className="text-center p-4 bg-gray-50 rounded-lg">
                <p className="text-2xl font-bold text-gray-900">{result.duration.toFixed(0)}ms</p>
                <p className="text-sm text-gray-500">Duration</p>
              </div>
            </div>

            <p className="text-sm text-gray-500">
              Batch ID: <code className="bg-gray-100 px-1 rounded">{result.batchId}</code>
            </p>
          </div>

          {/* Matched Pairs */}
          <div className="bg-white rounded-lg border border-gray-200 p-6">
            <h3 className="font-medium text-gray-900 mb-4 flex items-center gap-2">
              <CheckCircle className="h-5 w-5 text-green-500" />
              Matched Pairs ({result.matchedPairs.length})
            </h3>

            {result.matchedPairs.length > 0 ? (
              <table className="w-full text-sm">
                <thead>
                  <tr className="border-b">
                    <th className="text-left py-2">Source A</th>
                    <th className="text-left py-2">Source B</th>
                    <th className="text-center py-2">Score</th>
                    <th className="text-center py-2">Type</th>
                  </tr>
                </thead>
                <tbody>
                  {result.matchedPairs.map((pair, i) => (
                    <tr key={i} className="border-b border-gray-100">
                      <td className="py-2 font-mono text-xs">{pair.transactionIdA}</td>
                      <td className="py-2 font-mono text-xs">{pair.transactionIdB}</td>
                      <td className="py-2 text-center">
                        <span className={`font-medium ${pair.score >= 0.95 ? 'text-green-600' : 'text-yellow-600'}`}>
                          {(pair.score * 100).toFixed(0)}%
                        </span>
                      </td>
                      <td className="py-2 text-center">
                        <StatusBadge status={pair.matchType} size="sm" />
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            ) : (
              <p className="text-gray-500">No matches found</p>
            )}
          </div>

          {/* Unmatched */}
          {(result.unmatchedFromA.length > 0 || result.unmatchedFromB.length > 0) && (
            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
              <div className="bg-white rounded-lg border border-red-200 p-6">
                <h3 className="font-medium text-gray-900 mb-4 flex items-center gap-2">
                  <XCircle className="h-5 w-5 text-red-500" />
                  Unmatched from Source A ({result.unmatchedFromA.length})
                </h3>
                {result.unmatchedFromA.length > 0 ? (
                  <div className="space-y-2">
                    {result.unmatchedFromA.map((tx) => (
                      <div key={tx.id} className="flex justify-between items-center p-2 bg-red-50 rounded">
                        <span className="text-sm">{tx.reference || tx.id}</span>
                        <MoneyDisplay amount={{ value: tx.amount, currency: tx.currency }} size="sm" />
                      </div>
                    ))}
                  </div>
                ) : (
                  <p className="text-gray-500 text-sm">All matched</p>
                )}
              </div>

              <div className="bg-white rounded-lg border border-red-200 p-6">
                <h3 className="font-medium text-gray-900 mb-4 flex items-center gap-2">
                  <AlertCircle className="h-5 w-5 text-red-500" />
                  Unmatched from Source B ({result.unmatchedFromB.length})
                </h3>
                {result.unmatchedFromB.length > 0 ? (
                  <div className="space-y-2">
                    {result.unmatchedFromB.map((tx) => (
                      <div key={tx.id} className="flex justify-between items-center p-2 bg-red-50 rounded">
                        <span className="text-sm">{tx.reference || tx.id}</span>
                        <MoneyDisplay amount={{ value: tx.amount, currency: tx.currency }} size="sm" />
                      </div>
                    ))}
                  </div>
                ) : (
                  <p className="text-gray-500 text-sm">All matched</p>
                )}
              </div>
            </div>
          )}
        </>
      )}
    </div>
  )
}
