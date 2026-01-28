import { apiGet } from './client'
import type { Money } from './transactions'

export interface LedgerBalance {
  balancesByCurrency: Record<string, Record<string, number>>
  consolidatedBalances?: Record<string, number>
  baseCurrency?: string
  lastUpdated: string
  entryCount: number
}

export interface AccountBalance {
  accountId: string
  balances: Record<string, Money>
  recentEntries: AccountEntry[]
  totalEntries: number
}

export interface AccountEntry {
  entryId: string
  transactionId: string
  type: 'Debit' | 'Credit'
  amount: Money
  description?: string
  timestamp: string
}

export interface TrialBalance {
  currency: string
  entries: TrialBalanceEntry[]
  totalDebits: Money
  totalCredits: Money
  isBalanced: boolean
  generatedAt: string
}

export interface TrialBalanceEntry {
  accountId: string
  totalDebits: number
  totalCredits: number
  balance: number
}

export const ledgerApi = {
  getBalance: (baseCurrency?: string) =>
    apiGet<LedgerBalance>(`/ledger/balance${baseCurrency ? `?baseCurrency=${baseCurrency}` : ''}`),

  getAccountBalance: (accountId: string) =>
    apiGet<AccountBalance>(`/ledger/accounts/${accountId}`),

  getTrialBalance: (currency: string) =>
    apiGet<TrialBalance>(`/ledger/trial-balance/${currency}`),
}
