import { apiGet, apiPost } from './client'

export interface ReconciliationResult {
  batchId: string
  sourceA: string
  sourceB: string
  totalTransactions: number
  matched: number
  unmatched: number
  matchRate: number
  duration: number
  matchedPairs: MatchedPair[]
  unmatchedFromA: UnmatchedTransaction[]
  unmatchedFromB: UnmatchedTransaction[]
}

export interface MatchedPair {
  transactionIdA: string
  transactionIdB: string
  score: number
  matchType: 'Exact' | 'Fuzzy' | 'Manual'
}

export interface UnmatchedTransaction {
  id: string
  amount: number
  currency: string
  date: string
  reference?: string
}

export interface ReconciliationStatus {
  batchId: string
  sourceA: string
  sourceB: string
  status: 'Pending' | 'InProgress' | 'Completed' | 'Failed'
  totalTransactions: number
  matchedCount: number
  unmatchedCount: number
  matchRate: number
  matchedPairs: MatchedPairDetail[]
  unmatchedItems: UnmatchedItemDetail[]
  excludedItems: UnmatchedItemDetail[]
  startedAt?: string
  startedBy?: string
  completedAt?: string
  duration?: number
}

export interface MatchedPairDetail {
  transactionIdA: string
  transactionIdB: string
  matchScore: number
  matchType: string
  matchedAt: string
  matchedBy?: string
  notes?: string
}

export interface UnmatchedItemDetail {
  transactionId: string
  source: string
  reason: string
  candidates: { transactionId: string; score: number }[]
  failedAt: string
  isExcluded: boolean
  excludedBy?: string
  exclusionReason?: string
}

export interface TransactionInput {
  id?: string
  amount: number
  currency?: string
  date?: string
  reference?: string
  description?: string
  source?: string
}

export const reconciliationApi = {
  start: (input: {
    sourceA: TransactionInput[]
    sourceB: TransactionInput[]
    sourceAName?: string
    sourceBName?: string
    startedBy?: string
  }) => apiPost<ReconciliationResult>('/reconciliation/start', input),

  getStatus: (batchId: string) =>
    apiGet<ReconciliationStatus>(`/reconciliation/${batchId}/status`),

  applyManualMatch: (
    batchId: string,
    transactionIdA: string,
    transactionIdB: string,
    matchedBy?: string,
    notes?: string
  ) =>
    apiPost(`/reconciliation/${batchId}/match`, {
      transactionIdA,
      transactionIdB,
      matchedBy,
      notes,
    }),
}
