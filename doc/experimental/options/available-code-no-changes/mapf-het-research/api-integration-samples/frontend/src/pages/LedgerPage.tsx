import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { ledgerApi } from '@/api/ledger'
import { BalanceCard, MoneyDisplay } from '@/components/common/MoneyDisplay'
import { CheckCircle, XCircle } from 'lucide-react'

const CURRENCIES = ['EUR', 'USD', 'GBP', 'RSD']

export function LedgerPage() {
  const [baseCurrency, setBaseCurrency] = useState('EUR')
  const [selectedCurrency, setSelectedCurrency] = useState('EUR')

  const { data: balances, isLoading: balancesLoading } = useQuery({
    queryKey: ['ledger', 'balance', baseCurrency],
    queryFn: () => ledgerApi.getBalance(baseCurrency),
  })

  const { data: trialBalance, isLoading: trialLoading } = useQuery({
    queryKey: ['ledger', 'trial-balance', selectedCurrency],
    queryFn: () => ledgerApi.getTrialBalance(selectedCurrency),
  })

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-gray-900">Multi-Currency Ledger</h1>
          <p className="text-gray-500">Double-entry bookkeeping with currency conversion</p>
        </div>

        <div className="flex items-center gap-4">
          <div>
            <label className="block text-xs text-gray-500 mb-1">Base Currency</label>
            <select
              value={baseCurrency}
              onChange={(e) => setBaseCurrency(e.target.value)}
              className="px-3 py-2 border border-gray-300 rounded-lg focus:ring-2 focus:ring-blue-500"
            >
              {CURRENCIES.map((c) => (
                <option key={c} value={c}>{c}</option>
              ))}
            </select>
          </div>
        </div>
      </div>

      {/* Consolidated Balances */}
      <div className="bg-white rounded-lg border border-gray-200 p-6">
        <h2 className="text-lg font-semibold text-gray-900 mb-4">
          Consolidated Balances (in {baseCurrency})
        </h2>

        {balancesLoading ? (
          <div className="animate-pulse grid grid-cols-4 gap-4">
            {Array.from({ length: 4 }).map((_, i) => (
              <div key={i} className="h-20 bg-gray-100 rounded-lg" />
            ))}
          </div>
        ) : balances?.consolidatedBalances ? (
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
            {Object.entries(balances.consolidatedBalances).map(([account, balance]) => (
              <BalanceCard
                key={account}
                title={account}
                amount={{ value: balance, currency: baseCurrency }}
                trend={balance > 0 ? 'up' : balance < 0 ? 'down' : 'neutral'}
              />
            ))}
          </div>
        ) : (
          <p className="text-gray-500">No balance data. Create transactions to see balances.</p>
        )}

        <div className="mt-4 text-sm text-gray-500">
          Last updated: {balances?.lastUpdated
            ? new Date(balances.lastUpdated).toLocaleString()
            : 'N/A'
          }
          {' | '}
          Total entries: {balances?.entryCount ?? 0}
        </div>
      </div>

      {/* Balances by Currency */}
      <div className="bg-white rounded-lg border border-gray-200 p-6">
        <h2 className="text-lg font-semibold text-gray-900 mb-4">
          Balances by Currency
        </h2>

        {balancesLoading ? (
          <div className="animate-pulse space-y-4">
            {Array.from({ length: 2 }).map((_, i) => (
              <div key={i} className="h-32 bg-gray-100 rounded-lg" />
            ))}
          </div>
        ) : balances?.balancesByCurrency && Object.keys(balances.balancesByCurrency).length > 0 ? (
          <div className="space-y-6">
            {Object.entries(balances.balancesByCurrency).map(([currency, accounts]) => (
              <div key={currency} className="border-l-4 border-blue-500 pl-4">
                <h3 className="font-medium text-gray-900 mb-2">{currency}</h3>
                <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
                  {Object.entries(accounts).map(([account, balance]) => (
                    <div key={account} className="bg-gray-50 rounded-lg p-3">
                      <p className="text-xs text-gray-500">{account}</p>
                      <MoneyDisplay
                        amount={{ value: balance, currency }}
                        size="md"
                        showSign
                      />
                    </div>
                  ))}
                </div>
              </div>
            ))}
          </div>
        ) : (
          <p className="text-gray-500">No currency-specific balances available.</p>
        )}
      </div>

      {/* Trial Balance */}
      <div className="bg-white rounded-lg border border-gray-200 p-6">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-lg font-semibold text-gray-900">Trial Balance</h2>
          <select
            value={selectedCurrency}
            onChange={(e) => setSelectedCurrency(e.target.value)}
            className="px-3 py-2 border border-gray-300 rounded-lg focus:ring-2 focus:ring-blue-500"
          >
            {CURRENCIES.map((c) => (
              <option key={c} value={c}>{c}</option>
            ))}
          </select>
        </div>

        {trialLoading ? (
          <div className="animate-pulse">
            <div className="h-64 bg-gray-100 rounded-lg" />
          </div>
        ) : trialBalance?.entries && trialBalance.entries.length > 0 ? (
          <>
            <table className="w-full">
              <thead>
                <tr className="border-b border-gray-200">
                  <th className="text-left py-2 text-sm font-medium text-gray-500">Account</th>
                  <th className="text-right py-2 text-sm font-medium text-gray-500">Debits</th>
                  <th className="text-right py-2 text-sm font-medium text-gray-500">Credits</th>
                  <th className="text-right py-2 text-sm font-medium text-gray-500">Balance</th>
                </tr>
              </thead>
              <tbody>
                {trialBalance.entries.map((entry) => (
                  <tr key={entry.accountId} className="border-b border-gray-100">
                    <td className="py-2 text-sm text-gray-900">{entry.accountId}</td>
                    <td className="py-2 text-right font-mono text-sm">
                      {entry.totalDebits.toFixed(2)}
                    </td>
                    <td className="py-2 text-right font-mono text-sm">
                      {entry.totalCredits.toFixed(2)}
                    </td>
                    <td className="py-2 text-right font-mono text-sm font-medium">
                      {entry.balance.toFixed(2)}
                    </td>
                  </tr>
                ))}
              </tbody>
              <tfoot>
                <tr className="font-semibold bg-gray-50">
                  <td className="py-2 text-sm">Total</td>
                  <td className="py-2 text-right font-mono text-sm">
                    <MoneyDisplay amount={trialBalance.totalDebits} size="sm" />
                  </td>
                  <td className="py-2 text-right font-mono text-sm">
                    <MoneyDisplay amount={trialBalance.totalCredits} size="sm" />
                  </td>
                  <td className="py-2 text-right">
                    {trialBalance.isBalanced ? (
                      <span className="inline-flex items-center gap-1 text-green-600">
                        <CheckCircle className="h-4 w-4" />
                        Balanced
                      </span>
                    ) : (
                      <span className="inline-flex items-center gap-1 text-red-600">
                        <XCircle className="h-4 w-4" />
                        Unbalanced
                      </span>
                    )}
                  </td>
                </tr>
              </tfoot>
            </table>
            <p className="mt-2 text-xs text-gray-400">
              Generated: {new Date(trialBalance.generatedAt).toLocaleString()}
            </p>
          </>
        ) : (
          <p className="text-gray-500">No trial balance data for {selectedCurrency}.</p>
        )}
      </div>
    </div>
  )
}
